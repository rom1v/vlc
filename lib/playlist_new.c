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
#include "media_internal.h"
#include "../src/playlist_new/item.h"
#include "../src/playlist_new/playlist.h"

struct libvlc_playlist
{
    vlc_playlist_t *playlist;
    libvlc_instance_t *libvlc;
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
libvlc_playlist_Wrap(vlc_playlist_t *playlist, libvlc_instance_t *libvlc,
                     bool owned)
{
    libvlc_playlist_t *wrapper = malloc(sizeof(*wrapper));
    if (unlikely(!wrapper))
        return NULL;

    libvlc_retain(libvlc);

    wrapper->playlist = playlist;
    wrapper->libvlc = libvlc;
    wrapper->owned = owned;
    return wrapper;
}

static void
libvlc_playlist_DeleteWrapper(libvlc_playlist_t *wrapper)
{
    if (wrapper->owned)
        vlc_playlist_Delete(wrapper->playlist);
    libvlc_release(wrapper->libvlc);
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

libvlc_playlist_t *
libvlc_playlist_New(libvlc_instance_t *libvlc)
{
    vlc_object_t *obj = VLC_OBJECT(libvlc->p_libvlc_int);
    vlc_playlist_t *playlist = vlc_playlist_New(obj);
    if (unlikely(!playlist))
        return NULL;

    libvlc_playlist_t *wrapper = libvlc_playlist_Wrap(playlist, libvlc, true);
    if (unlikely(!wrapper))
    {
        free(playlist);
        return NULL;
    }

    return wrapper;
}

void
libvlc_playlist_Delete(libvlc_playlist_t *playlist)
{
    libvlc_playlist_DeleteWrapper(playlist);
}

void
libvlc_playlist_Lock(libvlc_playlist_t *playlist)
{
    vlc_playlist_Lock(playlist->playlist);
}

void
libvlc_playlist_Unlock(libvlc_playlist_t *playlist)
{
    vlc_playlist_Unlock(playlist->playlist);
}

static void
libvlc_playlist_DeleteItemsArray(libvlc_playlist_item_t *array[],
                                 size_t count)
{
    while (count--)
        libvlc_playlist_item_DeleteWrapper(array[count]);
    free(array);
}

static libvlc_playlist_item_t *const *
libvlc_playlist_WrapItemsArray(libvlc_playlist_t *playlist,
                               vlc_playlist_item_t *const items[], size_t count)
{
    libvlc_playlist_item_t **array = vlc_alloc(count, sizeof(*array));
    if (unlikely(!array))
        return NULL;

    size_t i;
    for (i = 0; i < count; ++i)
    {
        array[i] = libvlc_playlist_item_Wrap(playlist->libvlc, items[i]);
        if (unlikely(!array[i]))
            break;
    }

    if (i < count)
    {
        libvlc_playlist_DeleteItemsArray(array, i);
        return NULL;
    }

    return array;
}

struct libvlc_callback_context {
    libvlc_playlist_t *playlist;
    const struct libvlc_playlist_callbacks *callbacks;
    void *userdata;
};

static void
on_items_reset(vlc_playlist_t *playlist, vlc_playlist_item_t *const items[],
               size_t count, void *userdata)
{
    VLC_UNUSED(playlist);

    struct libvlc_callback_context *ctx = userdata;

    libvlc_playlist_item_t *const *array =
            libvlc_playlist_WrapItemsArray(ctx->playlist, items, count);
    /* XXX if array is NULL, we must still notify, otherwise the sequence of
     * events will provide invalid indices.
     * An alternative would be to stop reporting events at the first allocation
     * failure, but how to notify the client?*/
    ctx->callbacks->on_items_reset(ctx->playlist, array, count, ctx->userdata);
}

static const struct vlc_playlist_callbacks vlc_playlist_callbacks_wrapper = {
    .on_items_reset = on_items_reset,
};


libvlc_playlist_listener_id *
libvlc_playlist_AddListener(libvlc_playlist_t *playlist,
                            const struct libvlc_playlist_callbacks *cbs,
                            void *userdata, bool notify_current_state)
{
    struct libvlc_callback_context *ctx = malloc(sizeof(*ctx));
    if (unlikely(!ctx))
        return NULL;
    ctx->playlist = playlist;
    ctx->callbacks = cbs;
    ctx->userdata = userdata;

    vlc_playlist_listener_id *listener =
            vlc_playlist_AddListener(playlist->playlist,
                                     &vlc_playlist_callbacks_wrapper, ctx,
                                     notify_current_state);
    if (unlikely(!listener))
    {
        free(ctx);
        return NULL;
    }

    libvlc_playlist_listener_id *wrapper =
            libvlc_playlist_listener_Wrap(listener);
    if (unlikely(!wrapper))
    {
        vlc_playlist_RemoveListener(playlist->playlist, listener);
        free(ctx);
        return NULL;
    }

    return wrapper;
}

void
libvlc_playlist_RemoveListener(libvlc_playlist_t *playlist,
                               libvlc_playlist_listener_id *listener)
{
    vlc_playlist_RemoveListener(playlist->playlist, listener->listener);
    libvlc_playlist_listener_DeleteWrapper(listener);
}
