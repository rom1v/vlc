/*****************************************************************************
 * vlc_playlist_new.h
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

#ifndef VLC_GUI_PLAYLIST_PROXY_H_
#define VLC_GUI_PLAYLIST_PROXY_H_

#include <vlc_common.h>

/* forward declarations */
typedef struct vlc_playlist vlc_playlist_t;
typedef struct vlc_playlist_item vlc_playlist_item_t;
typedef struct input_item_t input_item_t;

int
vlc_playlist_RequestInsert(vlc_playlist_t *playlist, size_t index,
                           input_item_t *const media[], size_t count);

int
vlc_playlist_RequestRemove(vlc_playlist_t *playlist, ssize_t index_hint,
                           vlc_playlist_item_t *const items[], size_t count);

#endif
