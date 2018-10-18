/*****************************************************************************
 * playlist_new/content.c
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

#include "content.h"

#include "control.h"
#include "item.h"
#include "notify.h"
#include "playlist.h"

void
vlc_playlist_ClearItems(vlc_playlist_t *playlist)
{
    vlc_playlist_item_t *item;
    vlc_vector_foreach(item, &playlist->items)
        vlc_playlist_item_Release(item);
    vlc_vector_clear(&playlist->items);
}

static void
vlc_playlist_ItemsReset(vlc_playlist_t *playlist)
{
    if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
        randomizer_Clear(&playlist->randomizer);

    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->current = -1;
    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    vlc_playlist_Notify(playlist, on_items_reset, playlist->items.data,
                   playlist->items.size);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

/* not static, it's called from preparse.c */
void
vlc_playlist_ItemsInserted(vlc_playlist_t *playlist, size_t index, size_t count)
{
    if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
        randomizer_Add(&playlist->randomizer,
                       &playlist->items.data[index], count);

    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    if (playlist->current >= (ssize_t) index)
        playlist->current += count;
    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    vlc_player_InvalidateNextMedia(playlist->player);

    vlc_playlist_item_t **items = &playlist->items.data[index];
    vlc_playlist_Notify(playlist, on_items_added, index, items, count);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
vlc_playlist_ItemsMoved(vlc_playlist_t *playlist, size_t index, size_t count,
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

    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    vlc_player_InvalidateNextMedia(playlist->player);

    vlc_playlist_Notify(playlist, on_items_moved, index, count, target);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
vlc_playlist_ItemsRemoving(vlc_playlist_t *playlist, size_t index, size_t count)
{
    if (playlist->order == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM)
        randomizer_Remove(&playlist->randomizer,
                          &playlist->items.data[index], count);
}

static void
vlc_playlist_ItemsRemoved(vlc_playlist_t *playlist, size_t index, size_t count)
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
            vlc_playlist_SetCurrentMedia(playlist, playlist->current);
            /* we changed the current media, this already resets the next */
            invalidate_next_media = false;
        } else if (current >= index + count) {
            playlist->current -= count;
        }
    }
    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    vlc_playlist_Notify(playlist, on_items_removed, index, count);
    vlc_playlist_state_NotifyChanges(playlist, &state);

    if (invalidate_next_media)
        vlc_player_InvalidateNextMedia(playlist->player);
}

size_t
vlc_playlist_Count(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->items.size;
}

vlc_playlist_item_t *
vlc_playlist_Get(vlc_playlist_t *playlist, size_t index)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->items.data[index];
}

ssize_t
vlc_playlist_IndexOf(vlc_playlist_t *playlist, const vlc_playlist_item_t *item)
{
    vlc_playlist_AssertLocked(playlist);

    ssize_t index;
    vlc_vector_index_of(&playlist->items, item, &index);
    return index;
}

ssize_t
vlc_playlist_IndexOfMedia(vlc_playlist_t *playlist, const input_item_t *media)
{
    vlc_playlist_AssertLocked(playlist);

    playlist_item_vector_t *items = &playlist->items;
    for (size_t i = 0; i < items->size; ++i)
        if (items->data[i]->media == media)
            return i;
    return -1;
}

void
vlc_playlist_Clear(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);

    vlc_playlist_ClearItems(playlist);
    int ret = vlc_player_SetCurrentMedia(playlist->player, NULL);
    VLC_UNUSED(ret); /* what could we do? */

    vlc_playlist_ItemsReset(playlist);
}

static int
vlc_playlist_MediaToItems(input_item_t *const media[], size_t count,
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
    vlc_playlist_AssertLocked(playlist);
    assert(index <= playlist->items.size);

    /* make space in the vector */
    if (!vlc_vector_insert_hole(&playlist->items, index, count))
        return VLC_ENOMEM;

    /* create playlist items in place */
    int ret = vlc_playlist_MediaToItems(media, count,
                                        &playlist->items.data[index]);
    if (ret != VLC_SUCCESS)
    {
        /* we were optimistic, it failed, restore the vector state */
        vlc_vector_remove_slice(&playlist->items, index, count);
        return ret;
    }

    vlc_playlist_ItemsInserted(playlist, index, count);

    return VLC_SUCCESS;
}

void
vlc_playlist_Move(vlc_playlist_t *playlist, size_t index, size_t count,
                  size_t target)
{
    vlc_playlist_AssertLocked(playlist);
    assert(index + count <= playlist->items.size);
    assert(target + count <= playlist->items.size);

    vlc_vector_move_slice(&playlist->items, index, count, target);

    vlc_playlist_ItemsMoved(playlist, index, count, target);
}

void
vlc_playlist_Remove(vlc_playlist_t *playlist, size_t index, size_t count)
{
    vlc_playlist_AssertLocked(playlist);
    assert(index < playlist->items.size);

    vlc_playlist_ItemsRemoving(playlist, index, count);

    for (size_t i = 0; i < count; ++i)
        vlc_playlist_item_Release(playlist->items.data[index + i]);

    vlc_vector_remove_slice(&playlist->items, index, count);

    vlc_playlist_ItemsRemoved(playlist, index, count);
}
