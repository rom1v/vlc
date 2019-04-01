/******************************************************************************
 * xspf.c : XSPF playlist export functions
 ******************************************************************************
 * Copyright (C) 2006-2009 the VideoLAN team
 *
 * Authors: Daniel Stränger <vlc at schmaller dot de>
 *          Yoann Peronneau <yoann@videolan.org>
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
 *******************************************************************************/

/**
 * \file modules/misc/playlist/xspf.c
 * \brief XSPF playlist export functions
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_input.h>
#include <vlc_playlist_export.h>
#include <vlc_strings.h>
#include <vlc_url.h>

#include <assert.h>

int xspf_export_playlist( vlc_object_t *p_this );

static char *input_xml( input_item_t *p_item, char *(*func)(input_item_t *) )
{
    char *tmp = func( p_item );
    if( tmp == NULL )
        return NULL;
    char *ret = vlc_xml_encode( tmp );
    free( tmp );
    return ret;
}

/**
 * \brief exports one item to file or traverse if item is a node
 * \param p_item playlist item to export
 * \param p_file file to write xml-converted item to
 * \param p_i_count counter for track identifiers
 */
static void xspf_export_item( input_item_t *media, FILE *file, uint64_t id)
{
    fputs("\t\t<track>\n", file);

    /* -> the location */

    char *uri = input_xml(media, input_item_GetURI);
    if (uri && *uri)
        fprintf(file, "\t\t\t<location>%s</location>\n", uri);

    /* -> the name/title (only if different from uri)*/
    char *title = input_xml(media, input_item_GetTitle);
    if( title && strcmp(uri, title))
        fprintf(file, "\t\t\t<title>%s</title>\n", title);
    free(title);

    if (media->p_meta) {
        /* -> the artist/creator */
        char *artist = input_xml(media, input_item_GetArtist);
        if (artist && *artist)
            fprintf(file, "\t\t\t<creator>%s</creator>\n", artist);
        free(artist);

        /* -> the album */
        char *album = input_xml(media, input_item_GetAlbum);
        if (album && *album)
            fprintf(file, "\t\t\t<album>%s</album>\n", album);
        free(album);

        /* -> the track number */
        char *track = input_xml(media, input_item_GetTrackNum);
        if (track)
        {
            int tracknum = atoi(track);

            free(track);
            if (tracknum > 0)
                fprintf(file, "\t\t\t<trackNum>%i</trackNum>\n", tracknum);
        }

        /* -> the description */
        char *desc = input_xml(media, input_item_GetDescription);
        if (desc && *desc)
            fprintf(file, "\t\t\t<annotation>%s</annotation>\n", desc);
        free(desc);

        char *url = input_xml(media, input_item_GetURL);
        if (url && *url)
            fprintf(file, "\t\t\t<info>%s</info>\n", url);
        free(url);

        char *art_url = input_xml(media, input_item_GetArtURL);
        if (art_url && *art_url)
            fprintf(file, "\t\t\t<image>%s</image>\n", art_url);
        free(art_url);
    }

    /* -> the duration */
    vlc_tick_t duration = input_item_GetDuration(media);
    if (duration > 0)
        fprintf(file, "\t\t\t<duration>%"PRIu64"</duration>\n",
                MS_FROM_VLC_TICK(duration));

    /* export the intenal id and the input's options (bookmarks, ...)
     * in <extension> */
    fputs("\t\t\t<extension application=\""
          "http://www.videolan.org/vlc/playlist/0\">\n", file);

    /* print the id and increase the counter */
    fprintf(file, "\t\t\t\t<vlc:id>%"PRIu64"</vlc:id>\n", id);

    for (int i = 0; i < media->i_options; i++)
    {
        char* src = media->ppsz_options[i];
        char* ret = NULL;

        if (src[0] == ':')
            src++;

        ret = vlc_xml_encode(src);
        if (!ret)
            continue;

        fprintf(file, "\t\t\t\t<vlc:option>%s</vlc:option>\n", ret);
        free(ret);
    }
    fputs("\t\t\t</extension>\n", file);
    fputs("\t\t</track>\n", file);
}

/**
 * \brief Prints the XSPF header to file, writes each item by xspf_export_item()
 * and closes the open xml elements
 * \param p_this the VLC playlist object
 * \return VLC_SUCCESS if some memory is available, otherwise VLC_ENONMEM
 */
int xspf_export_playlist( vlc_object_t *p_this )
{
    struct playlist_export *export = (struct playlist_export *) p_this;

    /* write XSPF XML header */
    fprintf(export->file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" );
    fprintf(export->file,
             "<playlist xmlns=\"http://xspf.org/ns/0/\" " \
              "xmlns:vlc=\"http://www.videolan.org/vlc/playlist/ns/0/\" " \
              "version=\"1\">\n" );

    fprintf(export->file, "\t<trackList>\n" );
    size_t count = vlc_playlist_view_Count(export->playlist_view);
    for (size_t i = 0; i < count; ++i)
    {
        vlc_playlist_item_t *item =
            vlc_playlist_view_Get(export->playlist_view, i);
        input_item_t *media = vlc_playlist_item_GetMedia(item);

        xspf_export_item(media, export->file, i);
    }

    fprintf(export->file, "\t</trackList>\n");

    /* close the header elements */
    fprintf(export->file, "</playlist>\n");

    return VLC_SUCCESS;
}
