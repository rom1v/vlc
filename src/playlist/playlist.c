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
#include <vlc_player.h>
#include <vlc_threads.h>
#include <vlc_vector.h>
#include "libvlc.h" // for vlc_assert_lock()

#ifdef TEST
/* disable vlc_assert_locked in tests since the symbol is not exported */
# define vlc_assert_locked(m) (void) m
#endif

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
    vlc_mutex_t lock;
    vlc_player_t *player;
    struct vlc_player_listener_id *player_listener;
    playlist_item_vector_t items;
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
    vlc_assert_locked(&playlist->lock);
}

#define PlaylistNotify(playlist, event, ...) \
do { \
    PlaylistAssertLocked(playlist); \
    vlc_playlist_listener_id *listener; \
    vlc_playlist_listener_foreach(listener, playlist) \
        if (listener->cbs->event) \
            listener->cbs->event(playlist, ##__VA_ARGS__, listener->userdata); \
} while(0)

/* Helper to notify several changes at once */
struct vlc_playlist_state {
    ssize_t current;
    bool has_prev;
    bool has_next;
}

void vlc_playlist_state_Save(vlc_playlist_t *playlist,
                             struct vlc_playlist_state *state)
{
    state->current = playlist->current;
    state->has_prev = playlist->has_prev;
    state->has_next = playlist->has_next;
}

void vlc_playlist_state_NotifyChanged(vlc_playlist_t *playlist,
                                      struct vlc_playlist_state *saved_state)
{
    if (saved_state->current != playlist->current)
        PlaylistNotify(playlist, on_current_item_changed, current);
    if (saved_state->has_prev != playlist->has_prev)
        PlaylistNotify(playlist, on_has_prev_changed, has_prev);
    if (saved_state->has_next != playlist->has_next)
        PlaylistNotify(playlist, on_has_next_changed, has_next);
}

static inline bool
PlaylistHasItemUpdatedListeners(vlc_playlist_t *playlist)
{
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_item_updated)
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
    PlaylistNotify(playlist, on_item_updated, index,
                   playlist->items.data[index]);
}

static vlc_playlist_item_t *
PlaylistGetItem(vlc_playlist_t *playlist, ssize_t index)
{
    PlaylistAssertLocked(playlist);

    if (index == -1)
        return NULL;

    return playlist->items.data[index];
}

static input_item_t *
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
    PlaylistAssertLocked(playlist);

    vlc_playlist_item_t *item;
    vlc_vector_foreach(item, &playlist->items)
        vlc_playlist_item_Release(item);
    vlc_vector_clear(&playlist->items);
}

static inline int
PlaylistSetCurrentMedia(vlc_playlist_t *playlist, ssize_t current)
{
    PlaylistAssertLocked(playlist);

    vlc_playlist_item_t *item = PlaylistGetItem(playlist, current);
    return vlc_player_SetCurrentMedia(playlist->player, item->media);
}

static inline bool
PlaylistHasPrev(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    switch (playlist->repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
                return false; /* TODO*/
            /* not the first item */
            return playlist->current > 0;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
            if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
                return false; /* TODO*/
            /* not empty */
            return playlist->items.size != 0;
        default:
            vlc_assert_unreachable();
    }
}

static inline bool
PlaylistHasNext(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    switch (playlist->repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
                return false; /* TODO*/
            /* not the last item */
            return playlist->current < (ssize_t) playlist->items.size - 1;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
            if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
                return false; /* TODO*/
            /* not empty */
            return playlist->items.size != 0;
        default:
            vlc_assert_unreachable();
    }
}

static size_t
PlaylistGetPrevIndex(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    assert(PlaylistHasPrev(playlist));
    switch (playlist->repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
                return 0; /* TODO */
            return playlist->current - 1;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
            if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
                return 0; /* TODO */
            if (playlist->current == 0)
                return playlist->items.size - 1;
            return playlist->current - 1;
        default:
            vlc_assert_unreachable();
    }
}

static size_t
PlaylistGetNextIndex(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    assert(PlaylistHasNext(playlist));
    switch (playlist->repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
                return 0; /* TODO */
            return playlist->current + 1;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
            if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
                return 0; /* TODO */
            return (playlist->current + 1) % playlist->items.size;
        default:
            vlc_assert_unreachable();
    }
}

/* called when the current media has changed _automatically_ (not on
 * SetCurrentItem) */
static void player_on_current_media_changed(vlc_player_t *player,
                                            input_item_t *new_media,
                                            void *userdata)
{
    VLC_UNUSED(player);
    vlc_playlist_t *playlist = userdata;

    input_item_t *media = PlaylistGetMedia(playlist, playlist->current);
    if (new_media == media)
        /* nothing to do */
        return;

    ssize_t index = vlc_playlist_IndexOfMedia(playlist, new_media);
    PlaylistLocalSetCurrent(playlist, index);
}

static input_item_t *
player_get_next_media(vlc_player_t *player, void *userdata)
{
    VLC_UNUSED(player);
    vlc_playlist_t *playlist = userdata;

    vlc_playlist_Lock(playlist);
    input_item_t *media;
    if (PlaylistHasNext(playlist))
    {
        size_t index = PlaylistGetNextIndex(playlist);
        media = playlist->items.data[index]->media;
    }
    else
        media = NULL;
    vlc_playlist_Unlock(playlist);
    return media;
}

static void
on_player_media_meta_changed(vlc_player_t *player, input_item_t *media,
                             void *userdata)
{
    VLC_UNUSED(player);
    vlc_playlist_t *playlist = userdata;

    vlc_playlist_Lock(playlist);
    NotifyMediaUpdated(playlist, media);
    vlc_playlist_Unlock(playlist);
}

static void
on_player_media_length_changed(vlc_player_t *player, vlc_tick_t new_length,
                               void *userdata)
{
    VLC_UNUSED(player);
    VLC_UNUSED(new_length);
    vlc_playlist_t *playlist = userdata;

    input_item_t *media = vlc_player_GetCurrentMedia(player);
    assert(media);

    vlc_playlist_Lock(playlist);
    NotifyMediaUpdated(playlist, media);
    vlc_playlist_Unlock(playlist);
}

static const struct vlc_player_owner_cbs player_owner_callbacks = {
    .on_current_media_changed = player_on_current_media_changed,
    .get_next_media = player_get_next_media,
};

static const struct vlc_player_cbs player_callbacks = {
    .on_item_meta_changed = on_player_media_meta_changed,
    .on_length_changed = on_player_media_length_changed,
};

vlc_playlist_t *
vlc_playlist_New(vlc_object_t *parent)
{
    vlc_playlist_t *playlist = malloc(sizeof(*playlist));
    if (unlikely(!playlist))
        return NULL;

    playlist->player = vlc_player_New(parent, &player_owner_callbacks,
                                      playlist);
    if (unlikely(!playlist->player))
    {
        free(playlist);
        return NULL;
    }

    /* no need to lock, the player is not visible to other threads yet */
    playlist->player_listener = vlc_player_AddListener(playlist->player,
                                                       &player_callbacks,
                                                       playlist);
    if (unlikely(!playlist->player_listener))
    {
        vlc_player_Delete(playlist->player);
        free(playlist);
        return NULL;
    }

    vlc_mutex_init(&playlist->lock);
    vlc_vector_init(&playlist->items);
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
    vlc_playlist_listener_id *listener;
    vlc_list_foreach(listener, &playlist->listeners, node)
        free(listener);
    vlc_list_init(&playlist->listeners); /* reset */

    vlc_player_RemoveListener(playlist->player, playlist->player_listener);
    vlc_player_Delete(playlist->player);

    PlaylistClear(playlist);
    vlc_mutex_destroy(&playlist->lock);

    free(playlist);
}

void
vlc_playlist_Lock(vlc_playlist_t *playlist)
{
    vlc_mutex_lock(&playlist->lock);
}

void
vlc_playlist_Unlock(vlc_playlist_t *playlist)
{
    vlc_mutex_unlock(&playlist->lock);
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
    vlc_player_SetCurrentMedia(playlist->player, NULL);

    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->current = -1;
    playlist->has_prev = false;
    playlist->has_next = false;

    PlaylistNotify(playlist, on_items_reset, NULL, 0);
    vlc_playlist_state_NotifyChanges(playlist, &state);
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
vlc_playlist_Append(vlc_playlist_t *playlist, input_item_t *media)
{
    return vlc_playlist_AppendAll(playlist, &media, 1);
}

int
vlc_playlist_AppendAll(vlc_playlist_t *playlist, input_item_t *const media[],
                       size_t count)
{
    PlaylistAssertLocked(playlist);
    return vlc_playlist_InsertAll(playlist, playlist->items.size, media, count);
}

int
vlc_playlist_Insert(vlc_playlist_t *playlist, size_t index,
                    input_item_t *media)
{
    return vlc_playlist_InsertAll(playlist, index, &media, 1);
}

int
vlc_playlist_InsertAll(vlc_playlist_t *playlist, size_t index,
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

    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);
    if (playlist->current >= (ssize_t) index)
        playlist->current += count;

    PlaylistNotify(playlist, on_items_added, index,
                   &playlist->items.data[index], count);
    vlc_playlist_state_NotifyChanges(playlist, &state);

    vlc_player_InvalidateNextMedia(playlist->player);

    return VLC_SUCCESS;
}

void
vlc_playlist_Remove(vlc_playlist_t *playlist, size_t index)
{
    vlc_playlist_RemoveSlice(playlist, index, 1);
}

void
vlc_playlist_RemoveSlice(vlc_playlist_t *playlist, size_t index, size_t count)
{
    PlaylistAssertLocked(playlist);
    assert(index < playlist->items.size);

    PlaylistNotify(playlist, on_items_removed, index, count);

    for (size_t i = 0; i < count; ++i)
        vlc_playlist_item_Release(playlist->items.data[index + i]);

    vlc_vector_remove_slice(&playlist->items, index, count);

    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->has_prev = PlaylistHasPrev(playlist);
    playlist->has_next = PlaylistHasNext(playlist);
    if (playlist->current != -1) {
        size_t current = (size_t) playlist->current;
        if (current >= index && current < index + count) {
            /* current item has been removed */
            /* TODO */
        } else if (current >= index + count) {
            playlist->current -= count;
        }
    }

    PlaylistNotify(playlist, on_items_removed, index, count);
    vlc_playlist_state_NotifyChanges(playlist, &state);

    PlaylistRefreshHasPrev(playlist);
    PlaylistRefreshHasNext(playlist);
    vlc_player_InvalidateNextMedia(playlist->player);
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

    PlaylistNotify(playlist, on_playback_repeat_changed, repeat);
}

void
vlc_playlist_SetPlaybackOrder(vlc_playlist_t *playlist,
                              enum vlc_playlist_playback_order order)
{
    PlaylistAssertLocked(playlist);

    if (playlist->order == order)
        return;

    playlist->order = order;

    PlaylistNotify(playlist, on_playback_order_changed, order);
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
    assert(PlaylistHasPrev(playlist));

    size_t index = PlaylistGetPrevIndex(playlist);
    int ret = PlaylistSetCurrent(playlist, index);
    if (ret != VLC_SUCCESS)
        return ret;

    PlaylistRefreshHasPrev(playlist);
    return VLC_SUCCESS;
}

int
vlc_playlist_Next(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    assert(PlaylistHasNext(playlist));

    size_t index = PlaylistGetNextIndex(playlist);
    int ret = PlaylistSetCurrent(playlist, index);
    if (ret != VLC_SUCCESS)
        return ret;

    PlaylistRefreshHasNext(playlist);
    return VLC_SUCCESS;
}

int
vlc_playlist_GoTo(vlc_playlist_t *playlist, size_t index)
{
    PlaylistAssertLocked(playlist);
    assert(index < playlist->items.size);

    int ret = PlaylistSetCurrent(playlist, index);
        return ret;

    PlaylistRefreshHasPrev(playlist);
    PlaylistRefreshHasNext(playlist);
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
    vlc_player_Lock(playlist->player);
    int ret = vlc_player_Start(playlist->player);
    vlc_player_Unlock(playlist->player);
    return ret;
}

void
vlc_playlist_Stop(vlc_playlist_t *playlist)
{
    vlc_player_Lock(playlist->player);
    vlc_player_Stop(playlist->player);
    vlc_player_Unlock(playlist->player);
}

void
vlc_playlist_Pause(vlc_playlist_t *playlist)
{
    vlc_player_Lock(playlist->player);
    vlc_player_Pause(playlist->player);
    vlc_player_Unlock(playlist->player);
}

void
vlc_playlist_Resume(vlc_playlist_t *playlist)
{
    vlc_player_Lock(playlist->player);
    vlc_player_Resume(playlist->player);
    vlc_player_Unlock(playlist->player);
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
    vlc_playlist_Remove(playlist, index);

    playlist_item_vector_t flatten = VLC_VECTOR_INITIALIZER;
    ChildrenToPlaylistItems(&flatten, node);

    if (vlc_vector_insert_all(&playlist->items, index, flatten.data,
                              flatten.size))
        PlaylistNotify(playlist, on_items_added, index,
                       &playlist->items.data[index], flatten.size);

    vlc_vector_clear(&flatten);
    return true;
}

static void
on_subtree_added(input_item_t *input, input_item_node_t *subtree,
                 void *userdata)
{
    vlc_playlist_t *playlist = userdata;
    vlc_playlist_Lock(playlist);

    ssize_t index = vlc_playlist_IndexOfMedia(playlist, input);
    if (index == -1)
    {
        /* the item has been removed, don't expand it */
        vlc_playlist_Unlock(playlist);
        return;
    }

    /* replace the item by its flatten subtree */
    ExpandItem(playlist, index, subtree);

    vlc_playlist_Unlock(playlist);
}

static const input_preparser_callbacks_t input_preparser_callbacks = {
    .on_subtree_added = on_subtree_added,
};

void
vlc_playlist_Preparse(vlc_playlist_t *playlist, libvlc_int_t *libvlc,
                      input_item_t *input)
{
#ifdef TEST
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

#ifdef TEST

static input_item_t *
CreateDummyInputItem(int num)
{
    char *url;
    char *name;

    int res = asprintf(&url, "vlc://item-%d", num);
    if (res == -1)
        return NULL;

    res = asprintf(&name, "item-%d", num);
    if (res == -1)
        return NULL;

    input_item_t *input = input_item_New(url, name);
    free(url);
    free(name);
    return input;
}

static input_item_node_t *
AddDummyInputItemNode(input_item_node_t *parent, int num)
{
    input_item_t *input = CreateDummyInputItem(num);
    if (!input)
        return NULL;
    return input_item_node_AppendItem(parent, input);
}

static void
test_expand_item(void)
{
    vlc_playlist_t *playlist = vlc_playlist_New(NULL);
    assert(playlist);

    /* initial playlist with 10 items */
    for (int i = 0; i < 10; ++i)
    {
        input_item_t *input = CreateDummyInputItem(i);
        assert(input);

        vlc_playlist_item_t *item = vlc_playlist_Append(playlist, input);
        assert(item);
    }

    /* create a subtree for item 8 with 4 children */
    input_item_t *item_to_expand = playlist->items.data[8]->media;
    input_item_node_t *root = input_item_node_Create(item_to_expand);
    for (int i = 0; i < 4; ++i)
    {
        input_item_node_t *node = AddDummyInputItemNode(root, i + 10);
        assert(node);
    }

    /* on the 3rd children, add 2 grand-children */
    input_item_node_t *parent = root->pp_children[2];
    for (int i = 0; i < 2; ++i)
    {
        input_item_node_t *node = AddDummyInputItemNode(parent, i+20);
        assert(node);
    }

    bool ok = ExpandItem(playlist, 8, root);
    assert(ok);
    assert(vlc_playlist_Count(playlist) == 15);
    assert(!strcmp("item-7", vlc_playlist_Get(playlist, 7)->media->psz_name));
    assert(!strcmp("item-10", vlc_playlist_Get(playlist, 8)->media->psz_name));
    assert(!strcmp("item-11", vlc_playlist_Get(playlist, 9)->media->psz_name));
    assert(!strcmp("item-12", vlc_playlist_Get(playlist, 10)->media->psz_name));
    assert(!strcmp("item-20", vlc_playlist_Get(playlist, 11)->media->psz_name));
    assert(!strcmp("item-21", vlc_playlist_Get(playlist, 12)->media->psz_name));
    assert(!strcmp("item-13", vlc_playlist_Get(playlist, 13)->media->psz_name));
    assert(!strcmp("item-9", vlc_playlist_Get(playlist, 14)->media->psz_name));
}

int main(void)
{
    test_expand_item();
    return 0;
}
#endif
