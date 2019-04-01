/*****************************************************************************
 * playlist/view.c
 *****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
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

#include <vlc_playlist.h>
#include "view.h"

struct vlc_playlist_view *
vlc_playlist_ConstView(vlc_playlist_t *playlist)
{
    struct vlc_playlist_view *view = malloc(sizeof(*view));
    if (!view)
        return NULL;

    view->playlist = playlist;
    return view;
}

void
vlc_playlist_view_Delete(struct vlc_playlist_view *view)
{
    free(view);
}

size_t
vlc_playlist_view_Count(struct vlc_playlist_view *view)
{
    return vlc_playlist_Count(view->playlist);
}

vlc_playlist_item_t *
vlc_playlist_view_Get(struct vlc_playlist_view *view, size_t index)
{
    return vlc_playlist_Get(view->playlist, index);
}
