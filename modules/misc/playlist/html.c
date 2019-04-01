/*****************************************************************************
 * html.c : HTML playlist export module
 *****************************************************************************
 * Copyright (C) 2008-2009 the VideoLAN team
 *
 * Authors: Rémi Duraffort <ivoire@videolan.org>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_input.h>
#include <vlc_playlist_export.h>
#include <vlc_strings.h>

#include <assert.h>


// Export the playlist in HTML
int Export_HTML( vlc_object_t *p_this );


/**
 * Recursively follow the playlist
 * @param p_export: the export structure
 * @param p_root: the current node
 */
static void DoExport(struct playlist_export *export)
{
    /* Go through the playlist and add items */
    size_t count = vlc_playlist_view_Count(export->playlist_view);
    for (size_t i = 0; i < count; ++i)
    {
        vlc_playlist_item_t *item =
            vlc_playlist_view_Get(export->playlist_view, i);

        input_item_t *media = vlc_playlist_item_GetMedia(item);

        char *name = NULL;
        char *tmp = input_item_GetName(media);
        if (tmp)
            name = vlc_xml_encode(tmp);
        free(tmp);

        if (name)
        {
            char *artist = NULL;
            tmp = input_item_GetArtist(media);
            if (tmp)
                artist = vlc_xml_encode(tmp);
            free(tmp);

            vlc_tick_t duration = input_item_GetDuration(media);
            int min = SEC_FROM_VLC_TICK(duration) / 60;
            int sec = SEC_FROM_VLC_TICK(duration) - min * 60;

            // Print the artist if we have one
            if (artist && *artist)
                fprintf(export->file, "    <li>%s - %s (%02d:%02d)</li>\n", artist, name, min, sec);
            else
                fprintf(export->file, "    <li>%s (%2d:%2d)</li>\n", name, min, sec);

            free(artist);
        }
        free(name);
    }
}


/**
 * Export the playlist as an HTML page
 * @param p_this: the playlist
 * @return VLC_SUCCESS if everything goes fine
 */
int Export_HTML( vlc_object_t *p_this )
{
    struct playlist_export *export = (struct playlist_export *) p_this;

    msg_Dbg(export, "saving using HTML format");

    /* Write header */
    fprintf(export->file, "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n"
"<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\">\n"
"<head>\n"
"  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n"
"  <meta name=\"Generator\" content=\"VLC media player\" />\n"
"  <meta name=\"Author\" content=\"VLC, http://www.videolan.org/vlc/\" />\n"
"  <title>VLC generated playlist</title>\n"
"  <style type=\"text/css\">\n"
"    body {\n"
"      background-color: #E4F3FF;\n"
"      font-family: sans-serif, Helvetica, Arial;\n"
"      font-size: 13px;\n"
"    }\n"
"    h1 {\n"
"      color: #2D58AE;\n"
"      font-size: 25px;\n"
"    }\n"
"    hr {\n"
"      color: #555555;\n"
"    }\n"
"  </style>\n"
"</head>\n\n"
"<body>\n"
"  <h1>Playlist</h1>\n"
"  <hr />\n"
"  <ol>\n" );

    // Call the playlist constructor
    DoExport(export);

    // Print the footer
    fprintf(export->file, "  </ol>\n"
"  <hr />\n"
"</body>\n"
"</html>" );
    return VLC_SUCCESS;
}

