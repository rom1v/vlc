/*****************************************************************************
 * playlist.c : Playlist
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
    input_item_t *input;
    vlc_atomic_rc_t rc;
};

static vlc_playlist_item_t *
vlc_playlist_item_New(input_item_t *input)
{
    vlc_playlist_item_t *item = malloc(sizeof(*item));
    if (unlikely(!item))
        return NULL;

    vlc_atomic_rc_init(&item->rc);
    item->input = input;
    input_item_Hold(input);
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
        input_item_Release(item->input);
        free(item);
    }
}

input_item_t *
vlc_playlist_item_GetInputItem(vlc_playlist_item_t *item)
{
    return item->input;
}

typedef struct VLC_VECTOR(vlc_playlist_item_t *) playlist_item_vector_t;

struct vlc_playlist
{
    vlc_mutex_t lock;
    vlc_player_t *player;
    playlist_item_vector_t items;
    int current;
    bool has_next;
    bool has_prev;
    struct vlc_list listeners; /**< list of vlc_playlist_listener_id.node */
    enum vlc_playlist_playback_mode mode;
    enum vlc_playlist_playback_order order;
};

static inline void
PlaylistAssertLocked(vlc_playlist_t *playlist)
{
    vlc_assert_locked(&playlist->lock);
}

static inline void
NotifyCleared(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_cleared)
            listener->cbs->on_cleared(playlist, listener->userdata);
}

static inline void
NotifyItemsAdded(vlc_playlist_t *playlist, int index,
                 vlc_playlist_item_t *items[], int len)
{
    PlaylistAssertLocked(playlist);
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_items_added)
            listener->cbs->on_items_added(playlist, index, items, len,
                                          listener->userdata);
}

static inline void
NotifyItemsRemoved(vlc_playlist_t *playlist, int index,
                   vlc_playlist_item_t *items[], int len)
{
    PlaylistAssertLocked(playlist);
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_items_removed)
            listener->cbs->on_items_removed(playlist, index, items, len,
                                            listener->userdata);
}

static inline void
NotifyPlaybackModeChanged(vlc_playlist_t *playlist,
                          enum vlc_playlist_playback_mode mode)
{
    PlaylistAssertLocked(playlist);
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_playback_mode_changed)
            listener->cbs->on_playback_mode_changed(playlist, mode,
                                                    listener->userdata);
}

static inline void
NotifyPlaybackOrderChanged(vlc_playlist_t *playlist,
                           enum vlc_playlist_playback_order order)
{
    PlaylistAssertLocked(playlist);
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_playback_order_changed)
            listener->cbs->on_playback_order_changed(playlist, order,
                                                     listener->userdata);
}

static inline void
NotifyCurrentItemChanged(vlc_playlist_t *playlist, int index,
                         vlc_playlist_item_t *item)
{
    PlaylistAssertLocked(playlist);
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_current_item_changed)
            listener->cbs->on_current_item_changed(playlist, index, item,
                                                   listener->userdata);
}

static inline void
NotifyHasNextChanged(vlc_playlist_t *playlist, bool has_next)
{
    PlaylistAssertLocked(playlist);
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_has_next_changed)
            listener->cbs->on_has_next_changed(playlist, has_next,
                                               listener->userdata);
}

static inline void
NotifyHasPrevChanged(vlc_playlist_t *playlist, bool has_prev)
{
    PlaylistAssertLocked(playlist);
    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_has_prev_changed)
            listener->cbs->on_has_prev_changed(playlist, has_prev,
                                               listener->userdata);
}

static inline vlc_playlist_item_t *
PlaylistGet(vlc_playlist_t *playlist, int index)
{
    PlaylistAssertLocked(playlist);
    return playlist->items.data[index];
}

static inline int
PlaylistCount(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->items.size;
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

static inline void
PlaylistSetCurrentItem(vlc_playlist_t *playlist, int current)
{
    PlaylistAssertLocked(playlist);

    if (playlist->current == current)
        return;

    playlist->current = current;
    NotifyCurrentItemChanged(playlist, current, playlist->items.data[current]);
}

static inline void
PlaylistSetHasNext(vlc_playlist_t *playlist, bool has_next)
{
    PlaylistAssertLocked(playlist);

    if (playlist->has_next == has_next)
        return;

    playlist->has_next = has_next;
    NotifyHasNextChanged(playlist, has_next);
}

static inline void
PlaylistSetHasPrev(vlc_playlist_t *playlist, bool has_prev)
{
    PlaylistAssertLocked(playlist);

    if (playlist->has_prev == has_prev)
        return;

    playlist->has_prev = has_prev;
    NotifyHasPrevChanged(playlist, has_prev);
}

static void
on_player_new_item_played(vlc_player_t *player, void *userdata,
                          input_item_t *item)
{
    VLC_UNUSED(player);
    VLC_UNUSED(userdata);
    VLC_UNUSED(item);
    /* TODO */
}

static void
on_player_input_event(vlc_player_t *player, void *userdata,
                      const struct vlc_input_event *event)
{
    VLC_UNUSED(player);
    VLC_UNUSED(userdata);
    VLC_UNUSED(event);
    /* TODO */
}

static const struct vlc_player_cbs player_callbacks = {
    .on_new_item_played = on_player_new_item_played,
    .on_input_event = on_player_input_event,
};

vlc_playlist_t *
vlc_playlist_New(vlc_object_t *parent)
{
    vlc_playlist_t *playlist = malloc(sizeof(*playlist));
    if (unlikely(!playlist))
        return NULL;

    playlist->player = vlc_player_New(parent, &player_callbacks, playlist);
    if (unlikely(!playlist->player))
    {
        free(playlist);
        return NULL;
    }

    vlc_mutex_init(&playlist->lock);
    vlc_vector_init(&playlist->items);
    playlist->current = -1;
    playlist->has_prev = false;
    playlist->has_next = false;
    vlc_list_init(&playlist->listeners);
    playlist->mode = VLC_PLAYLIST_PLAYBACK_NORMAL;
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

    vlc_player_Release(playlist->player);

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

int
vlc_playlist_Count(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return PlaylistCount(playlist);
}

vlc_playlist_item_t *
vlc_playlist_Get(vlc_playlist_t *playlist, int index)
{
    PlaylistAssertLocked(playlist);
    return PlaylistGet(playlist, index);
}

void
vlc_playlist_Clear(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);

    PlaylistClear(playlist);
    NotifyCleared(playlist);
}

vlc_playlist_item_t *
vlc_playlist_Append(vlc_playlist_t *playlist, input_item_t *input)
{
    PlaylistAssertLocked(playlist);

    vlc_playlist_item_t *item = vlc_playlist_item_New(input);
    if (unlikely(!item))
        return NULL;

    if (!vlc_vector_push(&playlist->items, item))
    {
        vlc_playlist_item_Release(item);
        return NULL;
    }

    NotifyItemsAdded(playlist, playlist->items.size, &item, 1);

    return item;
}

vlc_playlist_item_t *
vlc_playlist_Insert(vlc_playlist_t *playlist, int index, input_item_t *input)
{
    PlaylistAssertLocked(playlist);
    assert(index >= 0 && index <= PlaylistCount(playlist));

    vlc_playlist_item_t *item = vlc_playlist_item_New(input);
    if (unlikely(!item))
        return NULL;

    if (!vlc_vector_insert(&playlist->items, index, item))
    {
        vlc_playlist_item_Release(item);
        return NULL;
    }

    NotifyItemsAdded(playlist, index, &item, 1);

    return item;
}

void
vlc_playlist_RemoveAt(vlc_playlist_t *playlist, int index)
{
    PlaylistAssertLocked(playlist);
    assert(index >= 0 && index < PlaylistCount(playlist));

    vlc_playlist_item_t *item = PlaylistGet(playlist, index);
    vlc_vector_remove(&playlist->items, index);

    NotifyItemsRemoved(playlist, index, &item, 1);

    vlc_playlist_item_Release(item);
}

bool
vlc_playlist_Remove(vlc_playlist_t *playlist, vlc_playlist_item_t *item)
{
    PlaylistAssertLocked(playlist);

    int index = vlc_playlist_IndexOf(playlist, item);
    if (index == -1)
        return false;

    vlc_playlist_RemoveAt(playlist, index);
    return true;
}

int
vlc_playlist_IndexOf(vlc_playlist_t *playlist, vlc_playlist_item_t *item)
{
    PlaylistAssertLocked(playlist);

    ssize_t index;
    vlc_vector_index_of(&playlist->items, item, &index);
    return index;
}

int
vlc_playlist_IndexOfInputItem(vlc_playlist_t *playlist, input_item_t *input)
{
    PlaylistAssertLocked(playlist);

    for (int i = 0; i < PlaylistCount(playlist); ++i)
        if (PlaylistGet(playlist, i)->input == input)
            return i;
    return -1;
}

enum vlc_playlist_playback_mode
vlc_playlist_GetPlaybackMode(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->mode;
}

enum vlc_playlist_playback_order
vlc_playlist_GetPlaybackOrder(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->order;
}

void
vlc_playlist_SetPlaybackMode(vlc_playlist_t *playlist,
                             enum vlc_playlist_playback_mode mode)
{
    PlaylistAssertLocked(playlist);

    if (playlist->mode == mode)
        return;

    playlist->mode = mode;

    NotifyPlaybackModeChanged(playlist, mode);
}

void
vlc_playlist_SetPlaybackOrder(vlc_playlist_t *playlist,
                              enum vlc_playlist_playback_order order)
{
    PlaylistAssertLocked(playlist);

    if (playlist->order == order)
        return;

    playlist->order = order;

    NotifyPlaybackOrderChanged(playlist, order);
}

int
vlc_playlist_GetCurrentIndex(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->current;
}

bool
vlc_playlist_HasNext(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->has_next;
}

bool
vlc_playlist_HasPrev(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->has_prev;
}

void
vlc_playlist_Next(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    assert(playlist->has_next);
    /* TODO */
}

void
vlc_playlist_Prev(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    assert(playlist->has_prev);
    /* TODO */
}

void
vlc_playlist_GoTo(vlc_playlist_t *playlist, int index)
{
    PlaylistAssertLocked(playlist);
    assert(index >= 0 && index < PlaylistCount(playlist));
    /* TODO */
}

vlc_player_t *
vlc_playlist_GetPlayer(vlc_playlist_t *playlist)
{
    return playlist->player;
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
ExpandItem(vlc_playlist_t *playlist, int index, input_item_node_t *node)
{
    vlc_playlist_RemoveAt(playlist, index);

    playlist_item_vector_t flatten = VLC_VECTOR_INITIALIZER;
    ChildrenToPlaylistItems(&flatten, node);
    if (vlc_vector_insert_all(&playlist->items, index, flatten.data, flatten.size))
        NotifyItemsAdded(playlist, index, &playlist->items.data[index], flatten.size);
    vlc_vector_clear(&flatten);
    return true;
}

static void
on_subtree_added(input_item_t *input, input_item_node_t *subtree,
                 void *userdata)
{
    vlc_playlist_t *playlist = userdata;
    vlc_playlist_Lock(playlist);

    int index = vlc_playlist_IndexOfInputItem(playlist, input);
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
    input_item_t *item_to_expand = PlaylistGet(playlist, 8)->input;
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
    assert(!strcmp("item-7", vlc_playlist_Get(playlist, 7)->input->psz_name));
    assert(!strcmp("item-10", vlc_playlist_Get(playlist, 8)->input->psz_name));
    assert(!strcmp("item-11", vlc_playlist_Get(playlist, 9)->input->psz_name));
    assert(!strcmp("item-12", vlc_playlist_Get(playlist, 10)->input->psz_name));
    assert(!strcmp("item-20", vlc_playlist_Get(playlist, 11)->input->psz_name));
    assert(!strcmp("item-21", vlc_playlist_Get(playlist, 12)->input->psz_name));
    assert(!strcmp("item-13", vlc_playlist_Get(playlist, 13)->input->psz_name));
    assert(!strcmp("item-9", vlc_playlist_Get(playlist, 14)->input->psz_name));
}

int main(void)
{
    test_expand_item();
    return 0;
}
#endif
