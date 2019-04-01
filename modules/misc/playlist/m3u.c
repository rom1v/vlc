/*****************************************************************************
 * m3u.c : M3U playlist export module
 *****************************************************************************
 * Copyright (C) 2004-2009 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_input.h>
#include <vlc_meta.h>
#include <vlc_charset.h>
#include <vlc_playlist_export.h>
#include <vlc_url.h>

#include <assert.h>

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
int Export_M3U ( vlc_object_t * );
int Export_M3U8( vlc_object_t * );

/*****************************************************************************
 * Export_M3U: main export function
 *****************************************************************************/
static void DoExport(struct playlist_export *export,
                     int (*pf_fprintf) (FILE *, const char *, ...))
{
    size_t prefix_len = -1;
    if (export->base_url)
    {
        const char *p = strrchr(export->base_url, '/');
        assert(p != NULL);
        prefix_len = (p + 1) - export->base_url;
    }

    /* Write header */
    fputs("#EXTM3U\n", export->file);

    /* Go through the playlist and add items */
    size_t count = vlc_playlist_view_Count(export->playlist_view);
    for (size_t i = 0; i < count; ++i)
    {
        vlc_playlist_item_t *item =
            vlc_playlist_view_Get(export->playlist_view, i);

        /* General info */
        input_item_t *media = vlc_playlist_item_GetMedia(item);

        char *uri = input_item_GetURI(media);
        assert(uri);

        char *name = input_item_GetName(media);
        if (name && strcmp(uri, name))
        {
            char *artist = input_item_GetArtist(media);
            vlc_tick_t duration = input_item_GetDuration(media);
            if (artist && *artist)
            {
                /* write EXTINF with artist */
                pf_fprintf(export->file, "#EXTINF:%"PRIu64",%s - %s\n",
                           SEC_FROM_VLC_TICK(duration), artist, name);
            }
            else
            {
                /* write EXTINF without artist */
                pf_fprintf(export->file, "#EXTINF:%"PRIu64",%s\n",
                           SEC_FROM_VLC_TICK(duration), name);
            }
            free(artist);
        }
        free(name);

        /* VLC specific options */
        vlc_mutex_lock(&media->lock);
        for (int j = 0; j < media->i_options; j++)
        {
            pf_fprintf(export->file, "#EXTVLCOPT:%s\n",
                       media->ppsz_options[j][0] == ':'
                           ? media->ppsz_options[j] + 1
                           : media->ppsz_options[j] );
        }
        vlc_mutex_unlock(&media->lock);

        /* We cannot really know if relative or absolute URL is better. As a
         * heuristic, we write a relative URL if the item is in the same
         * directory as the playlist, or a sub-directory thereof. */
        size_t skip = 0;
        if (prefix_len != (size_t)-1
                && !strncmp(export->base_url, uri, prefix_len))
            skip = prefix_len;

        fprintf(export->file, "%s\n", uri + skip);
        free(uri);
    }
}

int Export_M3U( vlc_object_t *p_this )
{
    struct playlist_export *export = (struct playlist_export *) p_this;

    msg_Dbg(export, "saving using M3U format");

    DoExport(export, utf8_fprintf);
    return VLC_SUCCESS;
}

int Export_M3U8( vlc_object_t *p_this )
{
    struct playlist_export *export = (struct playlist_export *) p_this;

    msg_Dbg(export, "saving using M3U8 format");

    DoExport(export, fprintf);
    return VLC_SUCCESS;
}
