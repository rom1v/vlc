/*****************************************************************************
 * playlist_new.c
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

#include <vlc/libvlc_playlist.h>
#include <vlc/libvlc_media.h>

#include <vlc_atomic.h>
#include "../src/playlist_new/playlist.h"

struct libvlc_playlist
{
    vlc_playlist_t *playlist;
    bool owned;
};

struct libvlc_playlist_item
{
    vlc_playlist_item_t *item;
    libvlc_media_t *media;
    vlc_atomic_rc_t rc;
};

struct libvlc_playlist_listener_id
{
    vlc_playlist_listener_id *listener; /* owned by the playlist */
};

static libvlc_playlist_t *
libvlc_playlist_Wrap(vlc_playlist_t *playlist, bool owned)
{
    libvlc_playlist_t *wrapper = malloc(sizeof(*wrapper));
    if (unlikely(!wrapper))
        return NULL;

    wrapper->playlist = playlist;
    wrapper->owned = owned;
    return wrapper;
}

static void
libvlc_playlist_DeleteWrapper(libvlc_playlist_t *wrapper)
{
    if (wrapper->owned)
        vlc_playlist_Delete(wrapper->playlist);
    free(wrapper);
}

static libvlc_playlist_item_t *
libvlc_playlist_item_Wrap(libvlc_instance_t *libvlc, vlc_playlist_item_t *item)
{
    libvlc_playlist_item_t *wrapper = malloc(sizeof(*wrapper));
    if (unlikely(!wrapper))
        return NULL;

    libvlc_media_t *media = libvlc_media_new_from_input_item(libvlc,
                                                             item->media);
    if (unlikely(!media))
    {
        free(wrapper);
        return NULL;
    }

    vlc_playlist_item_Hold(item);

    wrapper->item = item;
    wrapper->media = media;

    vlc_atomic_rc_init(&wrapper->rc);

    return wrapper;
}

static void
libvlc_playlist_item_DeleteWrapper(libvlc_playlist_item_t *wrapper)
{
    vlc_playlist_item_Release(wrapper->item);
    libvlc_media_release(wrapper->media);
    free(wrapper);
}

static libvlc_playlist_listener_id *
libvlc_playlist_listener_Wrap(vlc_playlist_listener_id *listener)
{
    libvlc_playlist_listener_id *wrapper = malloc(sizeof(*wrapper));
    if (unlikely(!wrapper))
        return NULL;

    wrapper->listener = listener;
    return wrapper;
}

static void
libvlc_playlist_listener_DeleteWrapper(libvlc_playlist_listener_id *wrapper)
{
    free(wrapper);
}

void
libvlc_playlist_item_Hold(libvlc_playlist_item_t *item)
{
    vlc_atomic_rc_inc(&item->rc);
}

void
libvlc_playlist_item_Release(libvlc_playlist_item_t *item)
{
    if (vlc_atomic_rc_dec(&item->rc))
    {
        vlc_playlist_item_Release(item->item);
        free(item);
    }
}

libvlc_media_t *
libvlc_playlist_item_GetMedia(libvlc_playlist_item_t *item)
{
    return item->media;
}
