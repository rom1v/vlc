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
#include <vlc_arrays.h>

typedef struct randomizer {
    vlc_array_t items;
    int head;
    int current;
} randomizer;

void randomizer_init(randomizer *randomizer);
void randomizer_destroy(randomizer *randomizer);

void randomizer_reshuffle(randomizer *randomizer);

void *randomizer_prev(randomizer *randomizer);
void *randomizer_next(randomizer *randomizer);
void randomizer_select(randomizer *randomizer, const void *item);

bool randomizer_add(randomizer *randomizer, void *item);
bool randomizer_remove(randomizer *randomizer, const void *item);
void randomizer_clear(randomizer *randomizer);

#endif
