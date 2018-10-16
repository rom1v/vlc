/*****************************************************************************
 * playlist_new/navigate.c
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

#include "navigate.h"

#include "item.h"
#include "notify.h"
#include "playlist.h"

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

    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

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

    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    PlaylistNotify(playlist, on_playback_repeat_changed, playlist->repeat);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

enum vlc_playlist_playback_repeat
vlc_playlist_GetPlaybackRepeat(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->repeat;
}

enum vlc_playlist_playback_order
vlc_playlist_GetPlaybackOrder(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->order;
}

void
vlc_playlist_SetPlaybackRepeat(vlc_playlist_t *playlist,
                               enum vlc_playlist_playback_repeat repeat)
{
    vlc_playlist_AssertLocked(playlist);

    if (playlist->repeat == repeat)
        return;

    playlist->repeat = repeat;
    PlaylistPlaybackRepeatChanged(playlist);
}

void
vlc_playlist_SetPlaybackOrder(vlc_playlist_t *playlist,
                              enum vlc_playlist_playback_order order)
{
    vlc_playlist_AssertLocked(playlist);

    if (playlist->order == order)
        return;

    playlist->order = order;
    PlaylistPlaybackOrderChanged(playlist);
}

int
vlc_playlist_SetCurrentMedia(vlc_playlist_t *playlist, ssize_t index)
{
    vlc_playlist_AssertLocked(playlist);

    input_item_t *media = index != -1
                        ? playlist->items.data[index]->media
                        : NULL;
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
    vlc_playlist_AssertLocked(playlist);
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
    vlc_playlist_AssertLocked(playlist);
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

bool
vlc_playlist_ComputeHasPrev(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
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

bool
vlc_playlist_ComputeHasNext(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
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

ssize_t
vlc_playlist_GetCurrentIndex(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->current;
}

static void
PlaylistSetCurrentIndex(vlc_playlist_t *playlist, ssize_t index)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->current = index;
    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    vlc_playlist_state_NotifyChanges(playlist, &state);
}

bool
vlc_playlist_HasPrev(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->has_prev;
}

bool
vlc_playlist_HasNext(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->has_next;
}

int
vlc_playlist_Prev(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);

    if (!vlc_playlist_ComputeHasPrev(playlist))
        return VLC_EGENERIC;

    ssize_t index = PlaylistGetPrevIndex(playlist);
    assert(index != -1);

    int ret = vlc_playlist_SetCurrentMedia(playlist, index);
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
    vlc_playlist_AssertLocked(playlist);

    if (!vlc_playlist_ComputeHasNext(playlist))
        return VLC_EGENERIC;

    ssize_t index = PlaylistGetNextIndex(playlist);
    assert(index != -1);

    int ret = vlc_playlist_SetCurrentMedia(playlist, index);
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
    vlc_playlist_AssertLocked(playlist);
    assert(index == -1 || (size_t) index < playlist->items.size);

    int ret = vlc_playlist_SetCurrentMedia(playlist, index);
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

static ssize_t
PlaylistGetNextMediaIndex(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    if (playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT)
        return playlist->current;
    if (!vlc_playlist_ComputeHasNext(playlist))
        return -1;
    return PlaylistGetNextIndex(playlist);
}

input_item_t *
vlc_playlist_GetNextMedia(vlc_playlist_t *playlist)
{
    /* the playlist and the player share the lock */
    vlc_playlist_AssertLocked(playlist);

    ssize_t index = PlaylistGetNextMediaIndex(playlist);
    if (index == -1)
        return NULL;

    input_item_t *media = playlist->items.data[index]->media;
    input_item_Hold(media);
    return media;
}
