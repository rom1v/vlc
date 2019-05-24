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

#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_playlist.h>
#include <vlc_vector.h>
#include <vlc/libvlc.h>
#include <vlc/libvlc_picture.h>
#include <vlc/libvlc_playlist.h>
#include <vlc/libvlc_media.h>

#include "media_internal.h"

typedef struct VLC_VECTOR(libvlc_playlist_item_t *) \
        libvlc_playlist_item_vector_t;

struct libvlc_playlist
{
    vlc_playlist_t *playlist;
    libvlc_instance_t *libvlc;
    libvlc_playlist_item_vector_t items;
    bool owned;
    /**
     * On core playlist events, a memory allocation error cannot be recovered
     * for the whole lifetime of the playlist.
     *
     * Mark the libvlc playlist as dead in that case.
     */
    bool dead;
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
    vlc_vector_init(&wrapper->items);
    wrapper->owned = owned;
    wrapper->dead = false;
    return wrapper;
}

void
libvlc_playlist_Delete(libvlc_playlist_t *wrapper)
{
    if (wrapper->owned)
        vlc_playlist_Delete(wrapper->playlist);
    vlc_vector_destroy(&wrapper->items);
    libvlc_release(wrapper->libvlc);
    free(wrapper);
}

static libvlc_playlist_item_t *
libvlc_playlist_item_Wrap(libvlc_instance_t *libvlc, vlc_playlist_item_t *item)
{
    libvlc_playlist_item_t *wrapper = malloc(sizeof(*wrapper));
    if (unlikely(!wrapper))
        return NULL;

    input_item_t *media_item = vlc_playlist_item_GetMedia(item);
    libvlc_media_t *media =
        libvlc_media_new_from_input_item(libvlc, media_item);
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
libvlc_playlist_item_Delete(libvlc_playlist_item_t *wrapper)
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
libvlc_playlist_listener_Delete(libvlc_playlist_listener_id *wrapper)
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
libvlc_playlist_Lock(libvlc_playlist_t *playlist)
{
    vlc_playlist_Lock(playlist->playlist);
}

void
libvlc_playlist_Unlock(libvlc_playlist_t *playlist)
{
    vlc_playlist_Unlock(playlist->playlist);
}

//static void
//libvlc_playlist_DeleteItemsArray(libvlc_playlist_item_t *array[],
//                                 size_t count)
//{
//    while (count--)
//        libvlc_playlist_item_DeleteWrapper(array[count]);
//    free(array);
//}
//
//static libvlc_playlist_item_t *const *
//libvlc_playlist_WrapItemsArray(libvlc_playlist_t *playlist,
//                               vlc_playlist_item_t *const items[], size_t count)
//{
//    libvlc_playlist_item_t **array = vlc_alloc(count, sizeof(*array));
//    if (unlikely(!array))
//        return NULL;
//
//    size_t i;
//    for (i = 0; i < count; ++i)
//    {
//        array[i] = libvlc_playlist_item_Wrap(playlist->libvlc, items[i]);
//        if (unlikely(!array[i]))
//            break;
//    }
//
//    if (i < count)
//    {
//        libvlc_playlist_DeleteItemsArray(array, i);
//        return NULL;
//    }
//
//    return array;
//}

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

    for (size_t i = 0; i < ctx->playlist->items.size; ++i)
        libvlc_playlist_item_Delete(ctx->playlist->items.data[i]);
    vlc_vector_clear(&ctx->playlist->items);

    
    libvlc_playlist_item_t *const *array =
            libvlc_playlist_WrapItemsArray(ctx->playlist, items, count);
    /* XXX if array is NULL, we must still notify, otherwise the sequence of
     * events will provide invalid indices.
     * An alternative would be to stop reporting events at the first allocation
     * failure, but how to notify the client?*/
    ctx->callbacks->on_items_reset(ctx->playlist, array, count, ctx->userdata);
}

static void
on_items_added(vlc_playlist_t *playlist, size_t index,
               vlc_playlist_item_t *const items[], size_t count, void *userdata)
{

}

static void
on_items_moved(vlc_playlist_t *playlist, size_t index, size_t count,
               size_t target, void *userdata)
{

}

static void
on_items_removed(vlc_playlist_t *playlist, size_t index, size_t count,
                 void *userdata)
{

}

static void
on_items_updated(vlc_playlist_t *playlist, size_t index,
                 vlc_playlist_item_t *const items[], size_t count,
                 void *userdata)
{

}

static void
on_playback_repeat_changed(vlc_playlist_t *playlist,
                           enum vlc_playlist_playback_repeat repeat,
                           void *userdata)
{

}

static void
on_current_index_changed(vlc_playlist_t *playlist, ssize_t index,
                         void *userdata)
{

}

static void
on_has_prev_changed(vlc_playlist_t *playlist, bool has_prev, void *userdata)
{

}

static void
on_has_next_changed(vlc_playlist_t *playlist, bool has_next, void *userdata)
{

}

static const struct vlc_playlist_callbacks vlc_playlist_callbacks_wrapper = {
    .on_items_reset = on_items_reset,
    .on_items_added = on_items_added,
    .on_items_moved = on_items_moved,
    .on_items_removed = on_items_removed,
    .on_items_updated = on_items_updated,
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
