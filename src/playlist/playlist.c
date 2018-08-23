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
#include <vlc_arrays.h>
#include <vlc_input_item.h>
#include <vlc_list.h>
#include <vlc_player.h>
#include <vlc_threads.h>
#include "libvlc.h" // for vlc_assert_lock()

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
    /* TODO nb_played, etc. */
};

static vlc_playlist_item_t *vlc_playlist_item_New(input_item_t *input)
{
    vlc_playlist_item_t *item = malloc(sizeof(*item));
    if (unlikely(!item))
        return NULL;

    item->input = input;
    input_item_Hold(input);
    return item;
}

static void vlc_playlist_item_Delete(vlc_playlist_item_t *item)
{
    input_item_Release(item->input);
    free(item);
}

input_item_t *vlc_playlist_item_GetInputItem(vlc_playlist_item_t *item)
{
    return item->input;
}

TYPEDEF_ARRAY(struct vlc_playlist_item *, vlc_playlist_item_array_t);

struct vlc_playlist
{
    vlc_mutex_t lock;
    vlc_player_t *player;
    vlc_playlist_item_array_t items;
    int current;
    bool has_next;
    bool has_prev;
    struct vlc_list listeners; /**< list of vlc_playlist_listener_id.node */
    enum vlc_playlist_playback_mode mode;
    enum vlc_playlist_playback_order order;
};

static inline void PlaylistAssertLocked(vlc_playlist_t *playlist)
{
    vlc_assert_locked(&playlist->lock);
}

static inline vlc_playlist_item_t *
PlaylistGet(vlc_playlist_t *playlist, int index)
{
    PlaylistAssertLocked(playlist);
    return ARRAY_VAL(playlist->items, index);
}

static inline int PlaylistCount(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->items.i_size;
}

static inline void PlaylistClear(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);

    vlc_playlist_item_t *item;
    ARRAY_FOREACH(item, playlist->items)
        vlc_playlist_item_Delete(item);
    ARRAY_RESET(playlist->items);
}

static void PlaylistSetHasNext(vlc_playlist_t *playlist, bool has_next)
{
    PlaylistAssertLocked(playlist);

    if (playlist->has_next == has_next)
        return;

    playlist->has_next = has_next;

    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_has_next_changed)
            listener->cbs->on_has_next_changed(playlist, has_next, listener->userdata);
}

static void PlaylistSetHasPrev(vlc_playlist_t *playlist, bool has_prev)
{
    PlaylistAssertLocked(playlist);

    if (playlist->has_prev == has_prev)
        return;

    playlist->has_prev = has_prev;

    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_has_prev_changed)
            listener->cbs->on_has_prev_changed(playlist, has_prev, listener->userdata);
}

static void on_player_new_item_played(vlc_player_t *player, void *userdata,
                                      input_item_t *item)
{
    VLC_UNUSED(player);
    VLC_UNUSED(userdata);
    VLC_UNUSED(item);
    /* TODO */
}

static void on_player_input_event(vlc_player_t *player, void *userdata,
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

vlc_playlist_t *vlc_playlist_New(vlc_object_t *parent)
{
    vlc_playlist_t *playlist = malloc(sizeof(*playlist));
    if (unlikely(!playlist))
        return NULL;

    vlc_player_t *player = vlc_player_New(parent, &player_callbacks, playlist);
    if (unlikely(!player))
    {
        free(playlist);
        return NULL;
    }

    vlc_mutex_init(&playlist->lock);
    ARRAY_INIT(playlist->items);
    playlist->current = -1;
    playlist->has_prev = false;
    playlist->has_next = false;
    vlc_list_init(&playlist->listeners);
    playlist->mode = VLC_PLAYLIST_PLAYBACK_NORMAL;
    playlist->order = VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL;

    return playlist;
}

void vlc_playlist_Delete(vlc_playlist_t *playlist)
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

void vlc_playlist_Lock(vlc_playlist_t *playlist)
{
    vlc_mutex_lock(&playlist->lock);
}

void vlc_playlist_Unlock(vlc_playlist_t *playlist)
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

void vlc_playlist_RemoveListener(vlc_playlist_t *playlist,
                                 vlc_playlist_listener_id *listener)
{
    PlaylistAssertLocked(playlist);

    vlc_list_remove(&listener->node);
    free(listener);
}

void vlc_playlist_setPlaybackMode(vlc_playlist_t *playlist,
                                  enum vlc_playlist_playback_mode mode)
{
    PlaylistAssertLocked(playlist);

    if (playlist->mode == mode)
        return;

    playlist->mode = mode;

    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_playback_mode_changed)
            listener->cbs->on_playback_mode_changed(playlist, mode, listener->userdata);
}

void vlc_playlist_setPlaybackOrder(vlc_playlist_t *playlist,
                                   enum vlc_playlist_playback_order order)
{
    PlaylistAssertLocked(playlist);

    if (playlist->order == order)
        return;

    playlist->order = order;

    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_playback_order_changed)
            listener->cbs->on_playback_order_changed(playlist, order, listener->userdata);
}

void vlc_playlist_Clear(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);

    PlaylistClear(playlist);

    vlc_playlist_listener_id *listener;
    vlc_playlist_listener_foreach(listener, playlist)
        if (listener->cbs->on_items_cleared)
            listener->cbs->on_items_cleared(playlist, listener->userdata);
}

bool vlc_playlist_Append(vlc_playlist_t *playlist, input_item_t *input)
{
    PlaylistAssertLocked(playlist);

    vlc_playlist_item_t *item = vlc_playlist_item_New(input);
    if (unlikely(!item))
        return false;

    /* currently, ARRAY_APPEND() aborts on allocation failure */
    ARRAY_APPEND(playlist->items, item);

    return true;
}

bool vlc_playlist_Insert(vlc_playlist_t *playlist, int index, input_item_t *input)
{
    PlaylistAssertLocked(playlist);
    assert(index >= 0 && index <= PlaylistCount(playlist));

    vlc_playlist_item_t *item = vlc_playlist_item_New(input);
    if (unlikely(!item))
        return false;

    /* currently, ARRAY_INSERT() aborts on allocation failure */
    ARRAY_INSERT(playlist->items, item, index);

    return true;
}

void vlc_playlist_RemoveAt(vlc_playlist_t *playlist, int index)
{
    PlaylistAssertLocked(playlist);
    assert(index >= 0 && index < PlaylistCount(playlist));

    vlc_playlist_item_t *item = PlaylistGet(playlist, index);
    vlc_playlist_item_Delete(item);
    ARRAY_REMOVE(playlist->items, index);
}

bool vlc_playlist_Remove(vlc_playlist_t *playlist, input_item_t *input)
{
    PlaylistAssertLocked(playlist);

    int index = vlc_playlist_Find(playlist, input);
    if (index == -1)
        return false;

    vlc_playlist_RemoveAt(playlist, index);
    return true;
}

int vlc_playlist_Find(vlc_playlist_t *playlist, input_item_t *input)
{
    PlaylistAssertLocked(playlist);

    for (int i = 0; i < PlaylistCount(playlist); ++i)
        if (PlaylistGet(playlist, i)->input == input)
            return i;
    return -1;
}

int vlc_playlist_Count(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return PlaylistCount(playlist);
}

vlc_playlist_item_t *vlc_playlist_Get(vlc_playlist_t *playlist, int index)
{
    PlaylistAssertLocked(playlist);
    return PlaylistGet(playlist, index);
}

int vlc_playlist_GetCurrentIndex(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->current;
}

bool vlc_playlist_HasNext(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->has_next;
}

bool vlc_playlist_HasPrev(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    return playlist->has_prev;
}

void vlc_playlist_Next(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    assert(playlist->has_next);
    /* TODO */
}

void vlc_playlist_Prev(vlc_playlist_t *playlist)
{
    PlaylistAssertLocked(playlist);
    assert(playlist->has_prev);
    /* TODO */
}

void vlc_playlist_GoTo(vlc_playlist_t *playlist, int index)
{
    PlaylistAssertLocked(playlist);
    assert(index >= 0 && index < PlaylistCount(playlist));
    /* TODO */
}

vlc_player_t *vlc_playlist_GetPlayer(vlc_playlist_t *playlist)
{
    return playlist->player;
}
