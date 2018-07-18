/*****************************************************************************
 * randomizer.h : Randomizer
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

#ifndef RANDOMIZER_H
#define RANDOMIZER_H

#include <vlc_common.h>
#include <vlc_vector.h>

typedef struct vlc_playlist_item vlc_playlist_item_t;

#define RANDOMIZER_MIN_HISTORY_SIZE 5

/*
 * 0              next  head          history       size
 * |---------------|-----|.............|-------------|
 */

struct randomizer {
    struct VLC_VECTOR(vlc_playlist_item_t *) items;
    unsigned short xsubi[3]; /* random state */
    bool loop;
    size_t head;
    size_t next;
    size_t history;
};

void
randomizer_Init(struct randomizer *randomizer);

void
randomizer_Destroy(struct randomizer *randomizer);

void
randomizer_SetLoop(struct randomizer *randomizer, bool loop);

bool
randomizer_Count(struct randomizer *randomizer);

void
randomizer_Reshuffle(struct randomizer *randomizer);

bool
randomizer_HasPrev(struct randomizer *randomizer);

bool
randomizer_HasNext(struct randomizer *randomizer);

vlc_playlist_item_t *
randomizer_PeekPrev(struct randomizer *randomizer);

vlc_playlist_item_t *
randomizer_PeekNext(struct randomizer *randomizer);

vlc_playlist_item_t *
randomizer_Prev(struct randomizer *randomizer);

vlc_playlist_item_t *
randomizer_Next(struct randomizer *randomizer);

void
randomizer_Select(struct randomizer *randomizer,
                  const vlc_playlist_item_t *item);

vlc_playlist_item_t *
randomizer_SelectRandom(struct randomizer *randomizer, size_t avoid_last_n);

bool
randomizer_Add(struct randomizer *randomizer, vlc_playlist_item_t *items[],
               size_t count);

void
randomizer_Remove(struct randomizer *randomizer,
                  vlc_playlist_item_t *const items[], size_t count);

void
randomizer_Clear(struct randomizer *randomizer);

#endif
