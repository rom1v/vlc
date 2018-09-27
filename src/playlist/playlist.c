/*****************************************************************************
 * playlist.c
 *****************************************************************************
 * Copyright (C) 2018 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_playlist_new.h>

#include <assert.h>
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_input_item.h>
#include <vlc_list.h>
#include <vlc_threads.h>
#include <vlc_vector.h>
#include "input/player.h"
#include "libvlc.h" // for vlc_MetadataRequest()
#include "randomizer.h"

#ifdef TEST_PLAYLIST
/* disable vlc_assert_locked in tests since the symbol is not exported */
# define vlc_assert_locked(m) VLC_UNUSED(m)
# define vlc_player_assert_locked(p) VLC_UNUSED(p);
/* mock the player in tests */
# define vlc_player_New(a,b,c) (VLC_UNUSED(a), VLC_UNUSED(b), malloc(1))
# define vlc_player_Delete(p) free(p)
# define vlc_player_Lock(p) VLC_UNUSED(p)
# define vlc_player_Unlock(p) VLC_UNUSED(p)
# define vlc_player_AddListener(a,b,c) (VLC_UNUSED(b), malloc(1))
# define vlc_player_RemoveListener(a,b) free(b)
# define vlc_player_SetCurrentMedia(a,b) (VLC_UNUSED(b), VLC_SUCCESS)
# define vlc_player_InvalidateNextMedia(p) VLC_UNUSED(p)
#endif /* TEST_PLAYLIST */

struct vlc_playlist_listener_id
{
    const struct vlc_playlist_callbacks *cbs;
    void *userdata;
    struct vlc_list node; /**< node of vlc_playlist.listeners */
};

#define vlc_playlist_listener_foreach(listener, playlist) \
    vlc_list_foreach(listener, &(playlist)->listeners, node)

struct vlc_playlist_item
{
    input_item_t *media;
    vlc_atomic_rc_t rc;
};

static vlc_playlist_item_t *
vlc_playlist_item_New(input_item_t *media)
{
    vlc_playlist_item_t *item = malloc(sizeof(*item));
    if (unlikely(!item))
        return NULL;

    vlc_atomic_rc_init(&item->rc);
    item->media = media;
    input_item_Hold(media);
    return item;
}

void
vlc_playlist_item_Hold(vlc_playlist_item_t *item)
{
    vlc_atomic_rc_inc(&item->rc);
}

void
vlc_playlist_item_Release(vlc_playlist_item_t *item)
{
    if (vlc_atomic_rc_dec(&item->rc))
    {
        input_item_Release(item->media);
        free(item);
    }
}

input_item_t *
vlc_playlist_item_GetMedia(vlc_playlist_item_t *item)
{
    return item->media;
}

typedef struct VLC_VECTOR(vlc_playlist_item_t *) playlist_item_vector_t;

struct vlc_playlist
{
    vlc_player_t *player;
    /* all remaining fields are protected by the lock of the player */
    struct vlc_player_listener_id *player_listener;
    playlist_item_vector_t items;
    struct randomizer randomizer;
    ssize_t current;
    bool has_prev;
    bool has_next;
    struct vlc_list listeners; /**< list of vlc_playlist_listener_id.node */
    enum vlc_playlist_playback_repeat repeat;
    enum vlc_playlist_playback_order order;
};

static inline void
PlaylistAssertLocked(vlc_playlist_t *playlist)
{
    vlc_player_assert_locked(playlist->player);
}

#define PlaylistNotifyListener(playlist, listener, event, ...) \
do { \
    if (listener->cbs->event) \
        listener->cbs->event(playlist, ##__VA_ARGS__, listener->userdata); \
} while (0)

#define PlaylistNotify(playlist, event, ...) \
do { \
    PlaylistAssertLocked(playlist); \
    vlc_playlist_listener_id *listener; \
    vlc_playlist_listener_foreach(listener, playlist) \
        PlaylistNotifyListener(playlist, listener, event, ##__VA_ARGS__); \
} while(0)

/* Helper to notify several changes at once */
struct vlc_playlist_state {
    ssize_t current;
    bool has_prev;
    bool has_next;
};

static void
vlc_playlist_state_Save(vlc_playlist_t *playlist,
                        struct vlc_playlist_state *state)
{
    state->current = playlist->current;
    state->has_prev = playlist->has_prev;
    state->has_next = playlist->has_next;
}

static void
vlc_playlist_state_NotifyChanges(vlc_playlist_t *playlist,
                                 struct vlc_playlist_state *saved_state)
{
    if (saved_state->current != playlist->current)
        PlaylistNotify(playlist, on_current_index_changed, playlist->current);
    if (saved_state->has_prev != playlist->has_prev)
        PlaylistNotify(playlist, on_has_prev_changed, playlist->has_prev);
    if (saved_state->has_next != playlist->has_next)
        PlaylistNotify(playlist, on_has_next_changed, playlist->has_next);
}

static inline bool
PlaylistHasItemUpdatedListeners(vlc_playlist_t *playlist)
{
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_items_updated)
            return true;
    return false;
}

static inline void
NotifyMediaUpdated(vlc_playlist_t *playlist, input_item_t *media)
{
    PlaylistAssertLocked(playlist);
    if (!PlaylistHasItemUpdatedListeners(playlist))
        /* no need to find the index if there are no listeners */
        return;

    ssize_t index;
    if (playlist->current != -1 &&
            playlist->items.data[playlist->current]->media == media)
        /* the player typically sends events for the current item, so we can
         * often avoid to search */
        index = playlist->current;
    else
    {
        /* linear search */
        index = vlc_playlist_IndexOfMedia(playlist, media);
        if (index == -1)
            return;
    }
    PlaylistNotify(playlist, on_items_updated, index,
                   &playlist->items.data[index], 1);
}

static inline vlc_playlist_item_t *
PlaylistGetItem(vlc_playlist_t *playlist, ssize_t index)
{
    PlaylistAssertLocked(playlist);

    if (index == -1)
        return NULL;

    return playlist->items.data[index];
}

static inline input_item_t *
PlaylistGetMedia(vlc_playlist_t *playlist, ssize_t index)
{
    vlc_playlist_item_t *item = PlaylistGetItem(playlist, index);

    if (!item)
        return NULL;

    return item->media;
}

static inline void
PlaylistClear(vlc_playlist_t *playlist)
{
    vlc_playlist_item_t *item;
    vlc_vector_foreach(item, &playlist->items)
        vlc_playlist_item_Release(item);
    vlc_vector_clear(&playlist->items);
}

static inline int
PlaylistSetCurrentMedia(vlc_playlist_t *playlist, ssize_t index)
{
    PlaylistAssertLocked(playlist);

    vlc_playlist_item_t *item = PlaylistGetItem(playlist, index);
    input_item_t *media = item ? item->media : NULL;
    return vlc_player_SetCurrentMedia(playlist->player, media);
}

static inline bool
PlaylistNormalOrderHasPrev(vlc_playlist_t *playlist)
{
    if (playlist->current == -1)
        return false;

    if (playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_ALL)
        return true;

    return playlist->current > 0;
}

static inline size_t
PlaylistNormalOrderGetPrevIndex(vlc_playlist_t *playlist)
{
    switch (playlist->repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            return playlist->current - 1;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
            if (playlist->current == 0)
                return playlist->items.size - 1;
            return playlist->current + 1;
        default:
            vlc_assert_unreachable();
    }
}

static inline bool
PlaylistNormalOrderHasNext(vlc_playlist_t *playlist)
{
    if (playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_ALL)
        return true;

    /* also works if current == -1 or playlist->items.size == 0 */
    return playlist->current < (ssize_t) playlist->items.size - 1;
}

static inline size_t
PlaylistNormalOrderGetNextIndex(vlc_playlist_t *playlist)
{
    switch (playlist->repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            if (playlist->current >= (ssize_t) playlist->items.size - 1)
                return -1;
            return playlist->current + 1;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
                if (playlist->items.size == 0)
                    return -1;
            return (playlist->current + 1) % playlist->items.size;
        default:
            vlc_assert_unreachable();
    }
}

static inline bool
PlaylistRandomOrderHasPrev(vlc_playlist_t *playlist)
{
    return randomizer_HasPrev(&playlist->randomizer);
}

static inline size_t
PlaylistRandomOrderGetPrevIndex(vlc_playlist_t *playlist)
{
    vlc_playlist_item_t *prev = randomizer_PeekPrev(&playlist->randomizer);
    assert(prev);
    ssize_t index = vlc_playlist_IndexOf(playlist, prev);
    assert(index != -1);
    return (size_t) index;
}

static inline bool
PlaylistRandomOrderHasNext(vlc_playlist_t *playlist)
{
    if (playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_ALL)
        return playlist->items.size > 0;
    return randomizer_HasNext(&playlist->randomizer);
}

static inline size_t
PlaylistRandomOrderGetNextIndex(vlc_playlist_t *playlist)
{
    vlc_playlist_item_t *next = randomizer_PeekNext(&playlist->randomizer);
    assert(next);
    ssize_t index = vlc_playlist_IndexOf(playlist, next);
    assert(index != -1);
    return (size_t) index;
}

static size_t
PlaylistGetPrevIndex(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    switch (playlist->order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return PlaylistNormalOrderGetPrevIndex(playlist);
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return PlaylistRandomOrderGetPrevIndex(playlist);
        default:
            vlc_assert_unreachable();
    }
}

static size_t
PlaylistGetNextIndex(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    switch (playlist->order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return PlaylistNormalOrderGetNextIndex(playlist);
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return PlaylistRandomOrderGetNextIndex(playlist);
        default:
            vlc_assert_unreachable();
    }
}

static bool
PlaylistHasPrev(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    switch (playlist->order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return PlaylistNormalOrderHasPrev(playlist);
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return PlaylistRandomOrderHasPrev(playlist);
        default:
            vlc_assert_unreachable();
    }
}

static bool
PlaylistHasNext(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    switch (playlist->order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return PlaylistNormalOrderHasNext(playlist);
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return PlaylistRandomOrderHasNext(playlist);
        default:
            vlc_assert_unreachable();
    }
}

static void
PlaylistPlaybackOrderChanged(vlc_playlist_t *playlist)
{
    if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
    {
        /* randomizer is expected to be empty at this point */
        assert(randomizer_Count(&playlist->randomizer) == 0);
        randomizer_Add(&playlist->randomizer, playlist->items.data,
                       playlist->items.size);

        bool loop = playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_ALL;
        randomizer_SetLoop(&playlist->randomizer, loop);
    }
    else
        /* we don't use the randomizer anymore */
        randomizer_Clear(&playlist->randomizer);

    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);

    PlaylistNotify(playlist, on_playback_order_changed, playlist->order);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
PlaylistPlaybackRepeatChanged(vlc_playlist_t *playlist)
{
    if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
    {
        bool loop = playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_ALL;
        randomizer_SetLoop(&playlist->randomizer, loop);
    }

    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);

    PlaylistNotify(playlist, on_playback_repeat_changed, playlist->repeat);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
PlaylistItemsReset(vlc_playlist_t *playlist)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->current = -1;
    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);

    PlaylistNotify(playlist, on_items_reset, playlist->items.data,
                   playlist->items.size);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
PlaylistItemsInserted(vlc_playlist_t *playlist, size_t index, size_t count)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    if (playlist->current >= (ssize_t) index)
        playlist->current += count;
    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);

    vlc_player_InvalidateNextMedia(playlist->player);

    vlc_playlist_item_t **items = &playlist->items.data[index];
    PlaylistNotify(playlist, on_items_added, index, items, count);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
PlaylistItemsMoved(vlc_playlist_t *playlist, size_t index, size_t count,
                   size_t target)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    if (playlist->current != -1) {
        size_t current = (size_t) playlist->current;
        if (current >= index && current < target + count)
        {
            if (current < index + count)
                /* current item belongs the moved block */
                playlist->current += target - index;
            else
                /* current item was shifted due to the moved block */
                playlist->current -= count;
        }
    }

    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);

    vlc_player_InvalidateNextMedia(playlist->player);

    PlaylistNotify(playlist, on_items_moved, index, count, target);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
PlaylistItemsRemoved(vlc_playlist_t *playlist, size_t index, size_t count)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    bool invalidate_next_media = true;
    if (playlist->current != -1) {
        size_t current = (size_t) playlist->current;
        if (current >= index && current < index + count) {
            /* current item has been removed */
            if (index + count < playlist->items.size) {
                /* select the first item after the removed block */
                playlist->current = index;
            } else {
                /* no more items */
                playlist->current = -1;
            }
            /* change current playback */
            PlaylistSetCurrentMedia(playlist, playlist->current);
            /* we changed the current media, this already resets the next */
            invalidate_next_media = false;
        } else if (current >= index + count) {
            playlist->current -= count;
        }
    }
    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);

    PlaylistNotify(playlist, on_items_removed, index, count);
    vlc_playlist_state_NotifyChanges(playlist, &state);

    if (invalidate_next_media)
        vlc_player_InvalidateNextMedia(playlist->player);
}

static void
PlaylistSetCurrentIndex(vlc_playlist_t *playlist, ssize_t index)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->current = index;
    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);

    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
ChildrenToPlaylistItems(playlist_item_vector_t *dest, input_item_node_t *node)
{
    for (int i = 0; i < node->i_children; ++i)
    {
        input_item_node_t *child = node->pp_children[i];
        vlc_playlist_item_t *item = vlc_playlist_item_New(child->p_item);
        if (item)
            vlc_vector_push(dest, item);
        ChildrenToPlaylistItems(dest, child);
    }
}

static bool
ExpandItem(vlc_playlist_t *playlist, size_t index, input_item_node_t *node)
{
    vlc_playlist_RemoveOne(playlist, index);

    playlist_item_vector_t flatten = VLC_VECTOR_INITIALIZER;
    ChildrenToPlaylistItems(&flatten, node);

    if (vlc_vector_insert_all(&playlist->items, index, flatten.data,
                              flatten.size))
        PlaylistItemsInserted(playlist, index, flatten.size);

    vlc_vector_destroy(&flatten);
    return true;
}

static void
ExpandItemFromNode(vlc_playlist_t *playlist, input_item_node_t *subitems)
{
    input_item_t *media = subitems->p_item;
    ssize_t index = vlc_playlist_IndexOfMedia(playlist, media);
    if (index != -1)
        /* replace the item by its flatten subtree */
        ExpandItem(playlist, index, subitems);
}

/* called when the current media has changed _automatically_ (not on
 * SetCurrentItem) */
static void
player_on_current_media_changed(vlc_player_t *player, input_item_t *new_media,
                                void *userdata)
{
    VLC_UNUSED(player);
    vlc_playlist_t *playlist = userdata;

    /* the playlist and the player share the lock */
    PlaylistAssertLocked(playlist);

    input_item_t *media = PlaylistGetMedia(playlist, playlist->current);
    if (new_media == media)
        /* nothing to do */
        return;

    ssize_t index = new_media ? vlc_playlist_IndexOfMedia(playlist, new_media)
                              : -1;

    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->current = index;
    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);

    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static ssize_t
PlaylistGetNextMediaIndex(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    if (playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT)
        return playlist->current;
    if (!PlaylistHasNext(playlist))
        return -1;
    return PlaylistGetNextIndex(playlist);
}

static input_item_t *
player_get_next_media(vlc_player_t *player, void *userdata)
{
    VLC_UNUSED(player);
    vlc_playlist_t *playlist = userdata;

    /* the playlist and the player share the lock */
    PlaylistAssertLocked(playlist);

    ssize_t index = PlaylistGetNextMediaIndex(playlist);
    if (index == -1)
        return NULL;

    input_item_t *media = playlist->items.data[index]->media;
    input_item_Hold(media);
    return media;
}

static void
on_player_media_meta_changed(vlc_player_t *player, input_item_t *media,
                             void *userdata)
{
    VLC_UNUSED(player);
    vlc_playlist_t *playlist = userdata;

    /* the playlist and the player share the lock */
    PlaylistAssertLocked(playlist);

    NotifyMediaUpdated(playlist, media);
}

static void
on_player_media_length_changed(vlc_player_t *player, vlc_tick_t new_length,
                               void *userdata)
{
    VLC_UNUSED(player);
    VLC_UNUSED(new_length);
    vlc_playlist_t *playlist = userdata;

    /* the playlist and the player share the lock */
    PlaylistAssertLocked(playlist);

    input_item_t *media = vlc_player_GetCurrentMedia(player);
    assert(media);

    NotifyMediaUpdated(playlist, media);
}

static void
on_player_subitems_detected(vlc_player_t *player, input_item_node_t *subitems,
                            void *userdata)
{
    VLC_UNUSED(player);
    vlc_playlist_t *playlist = userdata;
    ExpandItemFromNode(playlist, subitems);
}

static const struct vlc_player_media_provider player_media_provider = {
    .get_next = player_get_next_media,
};

static const struct vlc_player_cbs player_callbacks = {
    .on_current_media_changed = player_on_current_media_changed,
    .on_media_meta_changed = on_player_media_meta_changed,
    .on_length_changed = on_player_media_length_changed,
    .on_subitems_changed = on_player_subitems_detected,
};

vlc_playlist_t *
vlc_playlist_New(vlc_object_t *parent)
{
    vlc_playlist_t *playlist = malloc(sizeof(*playlist));
    if (unlikely(!playlist))
        return NULL;

    playlist->player = vlc_player_New(parent, &player_media_provider, playlist);
    if (unlikely(!playlist->player))
    {
        free(playlist);
        return NULL;
    }

    vlc_player_Lock(playlist->player);
    /* the playlist and the player share the lock */
    PlaylistAssertLocked(playlist);
    playlist->player_listener = vlc_player_AddListener(playlist->player,
                                                       &player_callbacks,
                                                       playlist);
    vlc_player_Unlock(playlist->player);
    if (unlikely(!playlist->player_listener))
    {
        vlc_player_Delete(playlist->player);
        free(playlist);
        return NULL;
    }

    vlc_vector_init(&playlist->items);
    randomizer_Init(&playlist->randomizer);
    playlist->current = -1;
    playlist->has_prev = false;
    playlist->has_next = false;
    vlc_list_init(&playlist->listeners);
    playlist->repeat = VLC_PLAYLIST_PLAYBACK_REPEAT_NONE;
    playlist->order = VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL;

    return playlist;
}

void
vlc_playlist_Delete(vlc_playlist_t *playlist)
{
    assert(vlc_list_is_empty(&playlist->listeners));

    vlc_player_Lock(playlist->player);
    vlc_player_RemoveListener(playlist->player, playlist->player_listener);
    vlc_player_Unlock(playlist->player);

    vlc_player_Delete(playlist->player);
    randomizer_Destroy(&playlist->randomizer);

    PlaylistClear(playlist);

    free(playlist);
}

void
vlc_playlist_Lock(vlc_playlist_t *playlist)
{
    vlc_player_Lock(playlist->player);
}

void
vlc_playlist_Unlock(vlc_playlist_t *playlist)
{
    vlc_player_Unlock(playlist->player);
}

vlc_playlist_listener_id *
vlc_playlist_AddListener(vlc_playlist_t *playlist,
                         const struct vlc_playlist_callbacks *cbs,
                         void *userdata)
{
    PlaylistAssertLocked(playlist);

    vlc_playlist_listener_id *listener = malloc(sizeof(*listener));
    if (unlikely(!listener))
        return NULL;

    listener->cbs = cbs;
    listener->userdata = userdata;
    vlc_list_append(&listener->node, &playlist->listeners);

    return listener;
}

void
vlc_playlist_RemoveListener(vlc_playlist_t *playlist,
                            vlc_playlist_listener_id *listener)
{
    PlaylistAssertLocked(playlist);

    vlc_list_remove(&listener->node);
    free(listener);
}

size_t
vlc_playlist_Count(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->items.size;
}

vlc_playlist_item_t *
vlc_playlist_Get(vlc_playlist_t *playlist, size_t index)
{
    PlaylistAssertLocked(playlist);
    return playlist->items.data[index];
}

void
vlc_playlist_Clear(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);

    PlaylistClear(playlist);
    int ret = vlc_player_SetCurrentMedia(playlist->player, NULL);
    VLC_UNUSED(ret); /* what could we do? */

    PlaylistItemsReset(playlist);
}

static int
PlaylistMediaToItems(input_item_t *const media[], size_t count,
                     vlc_playlist_item_t *items[])
{
    size_t i;
    for (i = 0; i < count; ++i)
    {
        items[i] = vlc_playlist_item_New(media[i]);
        if (unlikely(!items[i]))
            break;
    }
    if (i < count)
    {
        /* allocation failure, release partial items */
        while (i)
            vlc_playlist_item_Release(items[--i]);
        return VLC_ENOMEM;
    }
    return VLC_SUCCESS;
}

int
vlc_playlist_Insert(vlc_playlist_t *playlist, size_t index,
                    input_item_t *const media[], size_t count)
{
    PlaylistAssertLocked(playlist);
    assert(index <= playlist->items.size);

    /* make space in the vector */
    if (!vlc_vector_insert_hole(&playlist->items, index, count))
        return VLC_ENOMEM;

    /* create playlist items in place */
    int ret = PlaylistMediaToItems(media, count, &playlist->items.data[index]);
    if (ret != VLC_SUCCESS)
    {
        /* we were optimistic, it failed, restore the vector state */
        vlc_vector_remove_slice(&playlist->items, index, count);
        return ret;
    }

    PlaylistItemsInserted(playlist, index, count);

    return VLC_SUCCESS;
}

void
vlc_playlist_Move(vlc_playlist_t *playlist, size_t index, size_t count,
                  size_t target)
{
    PlaylistAssertLocked(playlist);
    assert(index + count <= playlist->items.size);
    assert(target + count <= playlist->items.size);

    vlc_vector_move_slice(&playlist->items, index, count, target);

    PlaylistItemsMoved(playlist, index, count, target);
}

void
vlc_playlist_Remove(vlc_playlist_t *playlist, size_t index, size_t count)
{
    PlaylistAssertLocked(playlist);
    assert(index < playlist->items.size);

    for (size_t i = 0; i < count; ++i)
        vlc_playlist_item_Release(playlist->items.data[index + i]);

    vlc_vector_remove_slice(&playlist->items, index, count);

    PlaylistItemsRemoved(playlist, index, count);
}

ssize_t
vlc_playlist_IndexOf(vlc_playlist_t *playlist, const vlc_playlist_item_t *item)
{
    PlaylistAssertLocked(playlist);

    ssize_t index;
    vlc_vector_index_of(&playlist->items, item, &index);
    return index;
}

ssize_t
vlc_playlist_IndexOfMedia(vlc_playlist_t *playlist, const input_item_t *media)
{
    PlaylistAssertLocked(playlist);

    playlist_item_vector_t *items = &playlist->items;
    for (size_t i = 0; i < items->size; ++i)
        if (items->data[i]->media == media)
            return i;
    return -1;
}

enum vlc_playlist_playback_repeat
vlc_playlist_GetPlaybackRepeat(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->repeat;
}

enum vlc_playlist_playback_order
vlc_playlist_GetPlaybackOrder(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->order;
}

void
vlc_playlist_SetPlaybackRepeat(vlc_playlist_t *playlist,
                               enum vlc_playlist_playback_repeat repeat)
{
    PlaylistAssertLocked(playlist);

    if (playlist->repeat == repeat)
        return;

    playlist->repeat = repeat;
    PlaylistPlaybackRepeatChanged(playlist);
}

void
vlc_playlist_SetPlaybackOrder(vlc_playlist_t *playlist,
                              enum vlc_playlist_playback_order order)
{
    PlaylistAssertLocked(playlist);

    if (playlist->order == order)
        return;

    playlist->order = order;
    PlaylistPlaybackOrderChanged(playlist);
}

ssize_t
vlc_playlist_GetCurrentIndex(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->current;
}

bool
vlc_playlist_HasPrev(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->has_prev;
}

bool
vlc_playlist_HasNext(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->has_next;
}

int
vlc_playlist_Prev(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);

    if (!PlaylistHasPrev(playlist))
        return VLC_EGENERIC;

    ssize_t index = PlaylistGetPrevIndex(playlist);
    assert(index != -1);

    int ret = PlaylistSetCurrentMedia(playlist, index);
    if (ret != VLC_SUCCESS)
        return ret;

    if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
    {
        /* mark the item as selected in the randomizer */
        vlc_playlist_item_t *selected = randomizer_Prev(&playlist->randomizer);
        assert(selected == playlist->items.data[index]);
        VLC_UNUSED(selected);
    }

    PlaylistSetCurrentIndex(playlist, index);
    return VLC_SUCCESS;
}

int
vlc_playlist_Next(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);

    if (!PlaylistHasNext(playlist))
        return VLC_EGENERIC;

    ssize_t index = PlaylistGetNextIndex(playlist);
    assert(index != -1);

    int ret = PlaylistSetCurrentMedia(playlist, index);
    if (ret != VLC_SUCCESS)
        return ret;

    if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
    {
        /* mark the item as selected in the randomizer */
        vlc_playlist_item_t *selected = randomizer_Next(&playlist->randomizer);
        assert(selected == playlist->items.data[index]);
        VLC_UNUSED(selected);
    }

    PlaylistSetCurrentIndex(playlist, index);
    return VLC_SUCCESS;
}

int
vlc_playlist_GoTo(vlc_playlist_t *playlist, ssize_t index)
{
    PlaylistAssertLocked(playlist);
    assert(index >= -1);

    if (index != -1 && (size_t) index >= playlist->items.size)
        return VLC_EGENERIC;

    int ret = PlaylistSetCurrentMedia(playlist, index);
    if (ret != VLC_SUCCESS)
        return ret;

    if (index != -1 && playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
    {
        vlc_playlist_item_t *item = playlist->items.data[index];
        randomizer_Select(&playlist->randomizer, item);
    }

    PlaylistSetCurrentIndex(playlist, index);
    return VLC_SUCCESS;
}

vlc_player_t *
vlc_playlist_GetPlayer(vlc_playlist_t *playlist)
{
    return playlist->player;
}

int
vlc_playlist_Start(vlc_playlist_t *playlist)
{
    return vlc_player_Start(playlist->player);
}

void
vlc_playlist_Stop(vlc_playlist_t *playlist)
{
    vlc_player_Stop(playlist->player);
}

void
vlc_playlist_Pause(vlc_playlist_t *playlist)
{
    vlc_player_Pause(playlist->player);
}

void
vlc_playlist_Resume(vlc_playlist_t *playlist)
{
    vlc_player_Resume(playlist->player);
}

static void
on_subtree_added(input_item_t *media, input_item_node_t *subtree,
                 void *userdata)
{
    VLC_UNUSED(media); /* retrieved by subtree->p_item */
    vlc_playlist_t *playlist = userdata;

    vlc_playlist_Lock(playlist);
    ExpandItemFromNode(playlist, subtree);
    vlc_playlist_Unlock(playlist);
}

static const input_preparser_callbacks_t input_preparser_callbacks = {
    .on_subtree_added = on_subtree_added,
};

void
vlc_playlist_Preparse(vlc_playlist_t *playlist, libvlc_int_t *libvlc,
                      input_item_t *input)
{
#ifdef TEST_PLAYLIST
    VLC_UNUSED(playlist);
    VLC_UNUSED(libvlc);
    VLC_UNUSED(input);
    VLC_UNUSED(input_preparser_callbacks);
#else
    /* vlc_MetadataRequest is not exported */
    vlc_MetadataRequest(libvlc, input, META_REQUEST_OPTION_NONE,
                        &input_preparser_callbacks, playlist, -1, NULL);
#endif
}






/******************************************************************************
 *                                   TESTS                                    *
 ******************************************************************************/

#ifdef TEST_PLAYLIST
/* the playlist lock is the one of the player */
# define vlc_playlist_Lock(p) VLC_UNUSED(p);
# define vlc_playlist_Unlock(p) VLC_UNUSED(p);

static input_item_t *
CreateDummyMedia(int num)
{
    char *url;
    char *name;

    int res = asprintf(&url, "vlc://item-%d", num);
    if (res == -1)
        return NULL;

    res = asprintf(&name, "item-%d", num);
    if (res == -1)
        return NULL;

    input_item_t *media = input_item_New(url, name);
    free(url);
    free(name);
    return media;
}

static void
CreateDummyMediaArray(input_item_t *out[], size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        out[i] = CreateDummyMedia(i);
        assert(out[i]);
    }
}

static void
DestroyMediaArray(input_item_t *const array[], size_t count)
{
    for (size_t i = 0; i < count; ++i)
        input_item_Release(array[i]);
}

#define EXPECT_AT(index, id) \
    assert(vlc_playlist_Get(playlist, index)->media == media[id])

static void
test_append(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* append one by one */
    for (int i = 0; i < 5; ++i)
    {
        int ret = vlc_playlist_AppendOne(playlist, media[i]);
        assert(ret == VLC_SUCCESS);
    }

    /* append several at once */
    int ret = vlc_playlist_Append(playlist, &media[5], 5);
    assert(ret == VLC_SUCCESS);

    assert(vlc_playlist_Count(playlist) == 10);
    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 3);
    EXPECT_AT(4, 4);
    EXPECT_AT(5, 5);
    EXPECT_AT(6, 6);
    EXPECT_AT(7, 7);
    EXPECT_AT(8, 8);
    EXPECT_AT(9, 9);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_insert(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[15];
    CreateDummyMediaArray(media, 15);

    /* initial playlist with 5 items */
    int ret = vlc_playlist_Append(playlist, media, 5);
    assert(ret == VLC_SUCCESS);

    /* insert one by one */
    for (int i = 0; i < 5; ++i)
    {
        ret = vlc_playlist_InsertOne(playlist, 2, media[i + 5]);
        assert(ret == VLC_SUCCESS);
    }

    /* insert several at once */
    ret = vlc_playlist_Insert(playlist, 6, &media[10], 5);
    assert(ret == VLC_SUCCESS);

    assert(vlc_playlist_Count(playlist) == 15);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);

    EXPECT_AT(2, 9);
    EXPECT_AT(3, 8);
    EXPECT_AT(4, 7);
    EXPECT_AT(5, 6);

    EXPECT_AT(6, 10);
    EXPECT_AT(7, 11);
    EXPECT_AT(8, 12);
    EXPECT_AT(9, 13);
    EXPECT_AT(10, 14);

    EXPECT_AT(11, 5);
    EXPECT_AT(12, 2);
    EXPECT_AT(13, 3);
    EXPECT_AT(14, 4);

    DestroyMediaArray(media, 15);
    vlc_playlist_Delete(playlist);
}

static void
test_move(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    /* move slice {3, 4, 5, 6} so that its new position is 5 */
    vlc_playlist_Move(playlist, 3, 4, 5);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 7);
    EXPECT_AT(4, 8);
    EXPECT_AT(5, 3);
    EXPECT_AT(6, 4);
    EXPECT_AT(7, 5);
    EXPECT_AT(8, 6);
    EXPECT_AT(9, 9);

    /* move it back to its original position */
    vlc_playlist_Move(playlist, 5, 4, 3);

    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 2);
    EXPECT_AT(3, 3);
    EXPECT_AT(4, 4);
    EXPECT_AT(5, 5);
    EXPECT_AT(6, 6);
    EXPECT_AT(7, 7);
    EXPECT_AT(8, 8);
    EXPECT_AT(9, 9);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_remove(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    /* remove one by one */
    for (int i = 0; i < 3; ++i)
        vlc_playlist_RemoveOne(playlist, 2);

    /* remove several at once */
    vlc_playlist_Remove(playlist, 3, 2);

    assert(vlc_playlist_Count(playlist) == 5);
    EXPECT_AT(0, 0);
    EXPECT_AT(1, 1);
    EXPECT_AT(2, 5);
    EXPECT_AT(3, 8);
    EXPECT_AT(4, 9);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_clear(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    assert(vlc_playlist_Count(playlist) == 10);
    vlc_playlist_Clear(playlist);
    assert(vlc_playlist_Count(playlist) == 0);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_expand_item(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[16];
    CreateDummyMediaArray(media, 16);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    /* create a subtree for item 8 with 4 children */
    input_item_t *item_to_expand = playlist->items.data[8]->media;
    input_item_node_t *root = input_item_node_Create(item_to_expand);
    for (int i = 0; i < 4; ++i)
    {
        input_item_node_t *node = input_item_node_AppendItem(root,
                                                             media[i + 10]);
        assert(node);
    }

    /* on the 3rd children, add 2 grand-children */
    input_item_node_t *parent = root->pp_children[2];
    for (int i = 0; i < 2; ++i)
    {
        input_item_node_t *node = input_item_node_AppendItem(parent,
                                                             media[i + 14]);
        assert(node);
    }

    bool ok = ExpandItem(playlist, 8, root);
    assert(ok);
    assert(vlc_playlist_Count(playlist) == 15);
    EXPECT_AT(7, 7);

    EXPECT_AT(8, 10);
    EXPECT_AT(9, 11);
    EXPECT_AT(10, 12);

    EXPECT_AT(11, 14);
    EXPECT_AT(12, 15);

    EXPECT_AT(13, 13);

    EXPECT_AT(14, 9);

    input_item_node_Delete(root);
    DestroyMediaArray(media, 16);
    vlc_playlist_Delete(playlist);
}

struct playlist_state
{
    size_t playlist_size;
    ssize_t current;
    bool has_prev;
    bool has_next;
};

static void
playlist_state_init(struct playlist_state *state, vlc_playlist_t *playlist)
{
    state->playlist_size = vlc_playlist_Count(playlist);
    state->current = vlc_playlist_GetCurrentIndex(playlist);
    state->has_prev = vlc_playlist_HasPrev(playlist);
    state->has_next = vlc_playlist_HasNext(playlist);
}

struct items_reset_report
{
    size_t count;
    struct playlist_state state;
};

struct items_added_report
{
    size_t index;
    size_t count;
    struct playlist_state state;
};

struct items_moved_report
{
    size_t index;
    size_t count;
    size_t target;
    struct playlist_state state;
};

struct items_removed_report
{
    size_t index;
    size_t count;
    struct playlist_state state;
};

struct playback_repeat_changed_report
{
    enum vlc_playlist_playback_repeat repeat;
};

struct playback_order_changed_report
{
    enum vlc_playlist_playback_order order;
};

struct current_index_changed_report
{
    ssize_t current;
};

struct has_prev_changed_report
{
    bool has_prev;
};

struct has_next_changed_report
{
    bool has_next;
};

struct callback_ctx
{
    struct VLC_VECTOR(struct items_reset_report)           vec_items_reset;
    struct VLC_VECTOR(struct items_added_report)           vec_items_added;
    struct VLC_VECTOR(struct items_moved_report)           vec_items_moved;
    struct VLC_VECTOR(struct items_removed_report)         vec_items_removed;
    struct VLC_VECTOR(struct playback_order_changed_report)
                                                  vec_playback_order_changed;
    struct VLC_VECTOR(struct playback_repeat_changed_report)
                                                  vec_playback_repeat_changed;
    struct VLC_VECTOR(struct current_index_changed_report)
                                                  vec_current_index_changed;
    struct VLC_VECTOR(struct has_prev_changed_report)      vec_has_prev_changed;
    struct VLC_VECTOR(struct has_next_changed_report)      vec_has_next_changed;
};

#define CALLBACK_CTX_INITIALIZER \
{ \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
    VLC_VECTOR_INITIALIZER, \
}

static inline void
callback_ctx_reset(struct callback_ctx *ctx)
{
    vlc_vector_clear(&ctx->vec_items_reset);
    vlc_vector_clear(&ctx->vec_items_added);
    vlc_vector_clear(&ctx->vec_items_moved);
    vlc_vector_clear(&ctx->vec_items_removed);
    vlc_vector_clear(&ctx->vec_playback_repeat_changed);
    vlc_vector_clear(&ctx->vec_playback_order_changed);
    vlc_vector_clear(&ctx->vec_current_index_changed);
    vlc_vector_clear(&ctx->vec_has_prev_changed);
    vlc_vector_clear(&ctx->vec_has_next_changed);
};

static inline void
callback_ctx_destroy(struct callback_ctx *ctx)
{
    vlc_vector_destroy(&ctx->vec_items_reset);
    vlc_vector_destroy(&ctx->vec_items_added);
    vlc_vector_destroy(&ctx->vec_items_moved);
    vlc_vector_destroy(&ctx->vec_items_removed);
    vlc_vector_destroy(&ctx->vec_playback_repeat_changed);
    vlc_vector_destroy(&ctx->vec_playback_order_changed);
    vlc_vector_destroy(&ctx->vec_current_index_changed);
    vlc_vector_destroy(&ctx->vec_has_prev_changed);
    vlc_vector_destroy(&ctx->vec_has_next_changed);
};

static void
callback_on_items_reset(vlc_playlist_t *playlist,
                        vlc_playlist_item_t *const items[], size_t count,
                        void *userdata)
{
    VLC_UNUSED(items);
    PlaylistAssertLocked(playlist);

    struct callback_ctx *ctx = userdata;

    struct items_reset_report report;
    report.count = count;
    playlist_state_init(&report.state, playlist);
    vlc_vector_push(&ctx->vec_items_reset, report);
}

static void
callback_on_items_added(vlc_playlist_t *playlist, size_t index,
                        vlc_playlist_item_t *const items[], size_t count,
                        void *userdata)
{
    VLC_UNUSED(items);
    PlaylistAssertLocked(playlist);

    struct callback_ctx *ctx = userdata;

    struct items_added_report report;
    report.index = index;
    report.count = count;
    playlist_state_init(&report.state, playlist);
    vlc_vector_push(&ctx->vec_items_added, report);
}

static void
callback_on_items_moved(vlc_playlist_t *playlist, size_t index, size_t count,
                        size_t target, void *userdata)
{
    PlaylistAssertLocked(playlist);

    struct callback_ctx *ctx = userdata;

    struct items_moved_report report;
    report.index = index;
    report.count = count;
    report.target = target;
    playlist_state_init(&report.state, playlist);
    vlc_vector_push(&ctx->vec_items_moved, report);
}

static void
callback_on_items_removed(vlc_playlist_t *playlist, size_t index, size_t count,
                          void *userdata)
{
    PlaylistAssertLocked(playlist);

    struct callback_ctx *ctx = userdata;

    struct items_removed_report report;
    report.index = index;
    report.count = count;
    playlist_state_init(&report.state, playlist);
    vlc_vector_push(&ctx->vec_items_removed, report);
}

static void
callback_on_playback_repeat_changed(vlc_playlist_t *playlist,
                                    enum vlc_playlist_playback_repeat repeat,
                                    void *userdata)
{
    PlaylistAssertLocked(playlist);
    struct callback_ctx *ctx = userdata;

    struct playback_repeat_changed_report report;
    report.repeat = repeat;
    vlc_vector_push(&ctx->vec_playback_repeat_changed, report);
}

static void
callback_on_playback_order_changed(vlc_playlist_t *playlist,
                                   enum vlc_playlist_playback_order order,
                                   void *userdata)
{
    PlaylistAssertLocked(playlist);
    struct callback_ctx *ctx = userdata;

    struct playback_order_changed_report report;
    report.order = order;
    vlc_vector_push(&ctx->vec_playback_order_changed, report);
}

static void
callback_on_current_index_changed(vlc_playlist_t *playlist, ssize_t index,
                                  void *userdata)
{
    PlaylistAssertLocked(playlist);
    struct callback_ctx *ctx = userdata;

    struct current_index_changed_report report;
    report.current = index;
    vlc_vector_push(&ctx->vec_current_index_changed, report);
}

static void
callback_on_has_prev_changed(vlc_playlist_t *playlist, bool has_prev,
                             void *userdata)
{
    PlaylistAssertLocked(playlist);
    struct callback_ctx *ctx = userdata;

    struct has_prev_changed_report report;
    report.has_prev = has_prev;
    vlc_vector_push(&ctx->vec_has_prev_changed, report);
}

static void
callback_on_has_next_changed(vlc_playlist_t *playlist, bool has_next,
                             void *userdata)
{
    PlaylistAssertLocked(playlist);
    struct callback_ctx *ctx = userdata;

    struct has_next_changed_report report;
    report.has_next = has_next;
    vlc_vector_push(&ctx->vec_has_next_changed, report);
}

static void
test_items_added_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    struct vlc_playlist_callbacks cbs = {
        .on_items_added = callback_on_items_added,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    int ret = vlc_playlist_AppendOne(playlist, media[0]);
    assert(ret == VLC_SUCCESS);

    /* the callbacks must be called with *all* values up to date */
    assert(ctx.vec_items_added.size == 1);
    assert(ctx.vec_items_added.data[0].index == 0);
    assert(ctx.vec_items_added.data[0].count == 1);
    assert(ctx.vec_items_added.data[0].state.playlist_size == 1);
    assert(ctx.vec_items_added.data[0].state.current == -1);
    assert(!ctx.vec_items_added.data[0].state.has_prev);
    assert(ctx.vec_items_added.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 0);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_reset(&ctx);

    /* set the only item as current */
    playlist->current = 0;
    playlist->has_prev = false;
    playlist->has_next = false;

    /* insert before the current item */
    ret = vlc_playlist_Insert(playlist, 0, &media[1], 4);
    assert(ret == VLC_SUCCESS);

    assert(ctx.vec_items_added.size == 1);
    assert(ctx.vec_items_added.data[0].index == 0);
    assert(ctx.vec_items_added.data[0].count == 4);
    assert(ctx.vec_items_added.data[0].state.playlist_size == 5);
    assert(ctx.vec_items_added.data[0].state.current == 4); /* shifted */
    assert(ctx.vec_items_added.data[0].state.has_prev);
    assert(!ctx.vec_items_added.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 4);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* append (after the current item) */
    ret = vlc_playlist_Append(playlist, &media[5], 5);
    assert(ret == VLC_SUCCESS);

    assert(ctx.vec_items_added.size == 1);
    assert(ctx.vec_items_added.data[0].index == 5);
    assert(ctx.vec_items_added.data[0].count == 5);
    assert(ctx.vec_items_added.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_added.data[0].state.current == 4);
    assert(ctx.vec_items_added.data[0].state.has_prev);
    assert(ctx.vec_items_added.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 0);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_items_moved_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_items_moved = callback_on_items_moved,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    vlc_playlist_Move(playlist, 2, 3, 5);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 2);
    assert(ctx.vec_items_moved.data[0].count == 3);
    assert(ctx.vec_items_moved.data[0].target == 5);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_moved.data[0].state.current == -1);
    assert(!ctx.vec_items_moved.data[0].state.has_prev);
    assert(ctx.vec_items_moved.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 0);
    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    playlist->current = 3;
    playlist->has_prev = true;
    playlist->has_next = true;

    callback_ctx_reset(&ctx);

    /* the current index belongs to the moved slice */
    vlc_playlist_Move(playlist, 1, 3, 5);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 1);
    assert(ctx.vec_items_moved.data[0].count == 3);
    assert(ctx.vec_items_moved.data[0].target == 5);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_moved.data[0].state.current == 7);
    assert(ctx.vec_items_moved.data[0].state.has_prev);
    assert(ctx.vec_items_moved.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 7);

    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* as a result of this move, the current item (7) will be at index 0 */
    vlc_playlist_Move(playlist, 0, 7, 1);

    assert(ctx.vec_items_moved.size == 1);
    assert(ctx.vec_items_moved.data[0].index == 0);
    assert(ctx.vec_items_moved.data[0].count == 7);
    assert(ctx.vec_items_moved.data[0].target == 1);
    assert(ctx.vec_items_moved.data[0].state.playlist_size == 10);
    assert(ctx.vec_items_moved.data[0].state.current == 0);
    assert(!ctx.vec_items_moved.data[0].state.has_prev);
    assert(ctx.vec_items_moved.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 0);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_items_removed_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_items_removed = callback_on_items_removed,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    vlc_playlist_RemoveOne(playlist, 4);

    assert(ctx.vec_items_removed.size == 1);
    assert(ctx.vec_items_removed.data[0].index == 4);
    assert(ctx.vec_items_removed.data[0].count == 1);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 9);
    assert(ctx.vec_items_removed.data[0].state.current == -1);
    assert(!ctx.vec_items_removed.data[0].state.has_prev);
    assert(ctx.vec_items_removed.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 0);
    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    playlist->current = 7;
    playlist->has_prev = true;
    playlist->has_next = true;

    callback_ctx_reset(&ctx);

    /* remove items before the current */
    vlc_playlist_Remove(playlist, 2, 4);

    assert(ctx.vec_items_removed.size == 1);
    assert(ctx.vec_items_removed.data[0].index == 2);
    assert(ctx.vec_items_removed.data[0].count == 4);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 5);
    assert(ctx.vec_items_removed.data[0].state.current == 3); /* shifted */
    assert(ctx.vec_items_removed.data[0].state.has_prev);
    assert(ctx.vec_items_removed.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 3);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* remove the remaining items (without Clear) */
    vlc_playlist_Remove(playlist, 0, 5);

    assert(ctx.vec_items_removed.size == 1);
    assert(ctx.vec_items_removed.data[0].index == 0);
    assert(ctx.vec_items_removed.data[0].count == 5);
    assert(ctx.vec_items_removed.data[0].state.playlist_size == 0);
    assert(ctx.vec_items_removed.data[0].state.current == -1);
    assert(!ctx.vec_items_removed.data[0].state.has_prev);
    assert(!ctx.vec_items_removed.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == -1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(!ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_items_reset_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_items_reset = callback_on_items_reset,
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    callback_ctx_reset(&ctx);

    playlist->current = 9; /* last item */
    playlist->has_prev = true;
    playlist->has_next = false;

    vlc_playlist_Clear(playlist);

    assert(ctx.vec_items_reset.size == 1);
    assert(ctx.vec_items_reset.data[0].count == 0);
    assert(ctx.vec_items_reset.data[0].state.playlist_size == 0);
    assert(ctx.vec_items_reset.data[0].state.current == -1);
    assert(!ctx.vec_items_reset.data[0].state.has_prev);
    assert(!ctx.vec_items_reset.data[0].state.has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == -1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_playback_repeat_changed_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    playlist->repeat = VLC_PLAYLIST_PLAYBACK_REPEAT_NONE;

    struct vlc_playlist_callbacks cbs = {
        .on_playback_repeat_changed = callback_on_playback_repeat_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    vlc_playlist_SetPlaybackRepeat(playlist, VLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    assert(vlc_playlist_GetPlaybackRepeat(playlist) ==
                                            VLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    assert(ctx.vec_playback_repeat_changed.size == 1);
    assert(ctx.vec_playback_repeat_changed.data[0].repeat ==
                                            VLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    vlc_playlist_Delete(playlist);
}

static void
test_playback_order_changed_callbacks(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    playlist->order = VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL;

    struct vlc_playlist_callbacks cbs = {
        .on_playback_order_changed = callback_on_playback_order_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    vlc_playlist_SetPlaybackOrder(playlist, VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    assert(vlc_playlist_GetPlaybackOrder(playlist) ==
                                            VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    assert(ctx.vec_playback_order_changed.size == 1);
    assert(ctx.vec_playback_order_changed.data[0].order ==
                                            VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    vlc_playlist_Delete(playlist);
}

static void
test_index_of(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 9 items (1 is not added) */
    int ret = vlc_playlist_Append(playlist, media, 9);
    assert(ret == VLC_SUCCESS);

    assert(vlc_playlist_IndexOfMedia(playlist, media[4]) == 4);
    /* only items 0 to 8 were added */
    assert(vlc_playlist_IndexOfMedia(playlist, media[9]) == -1);

    vlc_playlist_item_t *item = vlc_playlist_Get(playlist, 4);
    assert(vlc_playlist_IndexOf(playlist, item) == 4);

    vlc_playlist_item_Hold(item);
    vlc_playlist_RemoveOne(playlist, 4);
    assert(vlc_playlist_IndexOf(playlist, item) == -1);
    vlc_playlist_item_Release(item);

    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

static void
test_prev(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[4];
    CreateDummyMediaArray(media, 4);

    /* initial playlist with 3 items */
    int ret = vlc_playlist_Append(playlist, media, 3);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    playlist->current = 2; /* last item */
    playlist->has_prev = true;
    playlist->has_next = false;

    /* go to the previous item (at index 1) */
    assert(vlc_playlist_HasPrev(playlist));
    ret = vlc_playlist_Prev(playlist);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 1);
    assert(playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 1);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_reset(&ctx);

    /* go to the previous item (at index 0) */
    assert(vlc_playlist_HasPrev(playlist));
    ret = vlc_playlist_Prev(playlist);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 0);
    assert(!playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 0);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    /* no more previous item */
    assert(!vlc_playlist_HasPrev(playlist));

    /* returns an error, but does not crash */
    assert(vlc_playlist_Prev(playlist) == VLC_EGENERIC);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 4);
    vlc_playlist_Delete(playlist);
}

static void
test_next(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[3];
    CreateDummyMediaArray(media, 3);

    /* initial playlist with 3 items */
    int ret = vlc_playlist_Append(playlist, media, 3);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    playlist->current = 0; /* first item */
    playlist->has_prev = false;
    playlist->has_next = true;

    /* go to the next item (at index 1) */
    assert(vlc_playlist_HasNext(playlist));
    ret = vlc_playlist_Next(playlist);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 1);
    assert(playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the next item (at index 2) */
    assert(vlc_playlist_HasNext(playlist));
    ret = vlc_playlist_Next(playlist);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 2);
    assert(playlist->has_prev);
    assert(!playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 2);

    assert(ctx.vec_has_prev_changed.size == 0);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(!ctx.vec_has_next_changed.data[0].has_next);

    /* no more next item */
    assert(!vlc_playlist_HasNext(playlist));

    /* returns an error, but does not crash */
    assert(vlc_playlist_Next(playlist) == VLC_EGENERIC);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 3);
    vlc_playlist_Delete(playlist);
}

static void
test_goto(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[10];
    CreateDummyMediaArray(media, 10);

    /* initial playlist with 10 items */
    int ret = vlc_playlist_Append(playlist, media, 10);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    /* go to an item in the middle */
    ret = vlc_playlist_GoTo(playlist, 4);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 4);
    assert(playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 4);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the same item */
    ret = vlc_playlist_GoTo(playlist, 4);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 4);
    assert(playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 0);
    assert(ctx.vec_has_prev_changed.size == 0);
    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the first item */
    ret = vlc_playlist_GoTo(playlist, 0);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 0);
    assert(!playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 0);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 0);

    callback_ctx_reset(&ctx);

    /* go to the last item */
    ret = vlc_playlist_GoTo(playlist, 9);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == 9);
    assert(playlist->has_prev);
    assert(!playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == 9);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(!ctx.vec_has_next_changed.data[0].has_next);

    callback_ctx_reset(&ctx);

    /* deselect current */
    ret = vlc_playlist_GoTo(playlist, -1);
    assert(ret == VLC_SUCCESS);

    assert(playlist->current == -1);
    assert(!playlist->has_prev);
    assert(playlist->has_next);

    assert(ctx.vec_current_index_changed.size == 1);
    assert(ctx.vec_current_index_changed.data[0].current == -1);

    assert(ctx.vec_has_prev_changed.size == 1);
    assert(!ctx.vec_has_prev_changed.data[0].has_prev);

    assert(ctx.vec_has_next_changed.size == 1);
    assert(ctx.vec_has_next_changed.data[0].has_next);

    /* returns an error, but does not crash */
    assert(vlc_playlist_GoTo(playlist, 1234) == VLC_EGENERIC);

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 10);
    vlc_playlist_Delete(playlist);
}

/* this only tests that the randomizer is correctly managed by the playlist,
 * for further tests on randomization properties, see randomizer tests. */
static void
test_random(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    input_item_t *media[5];
    CreateDummyMediaArray(media, 5);

    /* initial playlist with 5 items */
    int ret = vlc_playlist_Append(playlist, media, 5);
    assert(ret == VLC_SUCCESS);

    struct vlc_playlist_callbacks cbs = {
        .on_current_index_changed = callback_on_current_index_changed,
        .on_has_prev_changed = callback_on_has_prev_changed,
        .on_has_next_changed = callback_on_has_next_changed,
    };

    struct callback_ctx ctx = CALLBACK_CTX_INITIALIZER;
    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist, &cbs, &ctx);
    assert(listener);

    assert(!vlc_playlist_HasPrev(playlist));
    assert(vlc_playlist_HasNext(playlist));

    for (int i = 0; i < 3; ++i)
    {
        assert(vlc_playlist_HasNext(playlist));
        ret = vlc_playlist_Next(playlist);
        assert(ret == VLC_SUCCESS);
    }

    assert(vlc_playlist_HasPrev(playlist));
    vlc_playlist_SetPlaybackOrder(playlist, VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);

    /* in random order, previous uses the history of randomly selected items */
    assert(!vlc_playlist_HasPrev(playlist));

    bool selected[5] = {};
    for (int i = 0; i < 5; ++i)
    {
        assert(vlc_playlist_HasNext(playlist));
        ret = vlc_playlist_Next(playlist);
        assert(ret == VLC_SUCCESS);
        ssize_t index = vlc_playlist_GetCurrentIndex(playlist);
        assert(index != -1);
        assert(!selected[index]); /* not selected twice */
        selected[index] = true;
    }

    assert(!vlc_playlist_HasNext(playlist));

    /* enable repeat */
    vlc_playlist_SetPlaybackRepeat(playlist, VLC_PLAYLIST_PLAYBACK_REPEAT_ALL);

    /* now there are more items */
    assert(vlc_playlist_HasNext(playlist));

    /* once again */
    memset(selected, 0, sizeof(selected));
    for (int i = 0; i < 5; ++i)
    {
        assert(vlc_playlist_HasNext(playlist));
        ret = vlc_playlist_Next(playlist);
        assert(ret == VLC_SUCCESS);
        ssize_t index = vlc_playlist_GetCurrentIndex(playlist);
        assert(index != -1);
        assert(!selected[index]); /* not selected twice */
        selected[index] = true;
    }

    /* there are always more items */
    assert(vlc_playlist_HasNext(playlist));

    /* move to the middle of the random array */
    for (int i = 0; i < 3; ++i)
    {
        assert(vlc_playlist_HasNext(playlist));
        ret = vlc_playlist_Next(playlist);
        assert(ret == VLC_SUCCESS);
    }

    memset(selected, 0, sizeof(selected));
    int actual[5]; /* store the selected items (by their index) */

    ssize_t current = vlc_playlist_GetCurrentIndex(playlist);
    assert(current != -1);
    actual[4] = current;

    for (int i = 3; i >= 0; --i)
    {
        assert(vlc_playlist_HasPrev(playlist));
        ret = vlc_playlist_Prev(playlist);
        assert(ret == VLC_SUCCESS);
        ssize_t index = vlc_playlist_GetCurrentIndex(playlist);
        assert(index != -1);
        actual[i] = index;
        assert(!selected[index]); /* not selected twice */
        selected[index] = true;
    }

    /* no more previous, the history may only contain each item once */
    assert(!vlc_playlist_HasPrev(playlist));

    /* we should get the same items in the reverse order going forward */
    for (int i = 1; i < 5; ++i)
    {
        assert(vlc_playlist_HasNext(playlist));
        ret = vlc_playlist_Next(playlist);
        assert(ret == VLC_SUCCESS);
        ssize_t index = vlc_playlist_GetCurrentIndex(playlist);
        assert(index != -1);
        assert(index == actual[i]);
    }

    /* there are always more items */
    assert(vlc_playlist_HasNext(playlist));

    callback_ctx_destroy(&ctx);
    vlc_playlist_RemoveListener(playlist, listener);
    DestroyMediaArray(media, 5);
    vlc_playlist_Delete(playlist);
}

#undef EXPECT_AT

int main(void)
{
    test_append();
    test_insert();
    test_move();
    test_remove();
    test_clear();
    test_expand_item();
    test_items_added_callbacks();
    test_items_moved_callbacks();
    test_items_removed_callbacks();
    test_items_reset_callbacks();
    test_playback_repeat_changed_callbacks();
    test_playback_order_changed_callbacks();
    test_index_of();
    test_prev();
    test_next();
    test_goto();
    test_random();
    return 0;
}
#endif /* TEST_PLAYLIST */
