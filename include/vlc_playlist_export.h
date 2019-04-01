/*****************************************************************************
 * vlc_playlist_export.h
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

#ifndef VLC_PLAYLIST_EXPORT_H
#define VLC_PLAYLIST_EXPORT_H

#include <vlc_playlist.h>

struct playlist_export
{
    struct vlc_common_members obj;
    char *base_url;
    FILE *file;
    struct vlc_playlist_view *playlist_view;
};

/**
 * Export the playlist to a file.
 *
 * \param filename the location where the exported file will be saved
 * \param type the type of the playlist file to create (m3u, m3u8, xspf, ...)
 * \return VLC_SUCCESS on success, another value on error
 */
// XXX use vlc_memstream instead of filename?
int
vlc_playlist_Export(struct vlc_playlist_view *playlist_view,
                    const char *filename,
                    const char *type);

#endif
