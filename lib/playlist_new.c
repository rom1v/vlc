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
#include <vlc_list.h>
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
    struct vlc_list listeners; /**< list of libvlc_playlist_listener_id.node */
    /** listener to the core playlist */
    struct vlc_playlist_listener_id *listener;
    bool owned;

    /**
     * On core playlist events, a memory allocation error may happen, which
     * desynchronize the libvlc playlist and the core playlist.
     *
     * Set a flag to retry a resync on the next playlist event.
     */
    bool must_resync;
};

struct libvlc_playlist_item
{
    vlc_playlist_item_t *item;
    libvlc_media_t *media;
    vlc_atomic_rc_t rc;
};

struct libvlc_playlist_listener_id
{
    const struct libvlc_playlist_callbacks *cbs;
    void *userdata;
    struct vlc_list node; /**< node of libvlc_playlist.listeners */
};

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

#define libvlc_playlist_listener_foreach(listener, playlist) \
    vlc_list_foreach(listener, &(playlist)->listeners, node)

#define libvlc_playlist_NotifyListener(playlist, listener, event, ...) \
do { \
    if (listener->cbs->event) \
        listener->cbs->event(playlist, ##__VA_ARGS__, listener->userdata); \
} while (0)

#define libvlc_playlist_Notify(playlist, event, ...) \
do { \
    libvlc_playlist_listener_id *listener; \
    libvlc_playlist_listener_foreach(listener, playlist) \
        libvlc_playlist_NotifyListener(playlist, listener, event, \
                                       ##__VA_ARGS__); \
} while(0)

libvlc_playlist_listener_id *
libvlc_playlist_AddListener(libvlc_playlist_t *playlist,
                            const struct libvlc_playlist_callbacks *cbs,
                            void *userdata, bool notify_current_state)
{
    struct libvlc_playlist_listener_id *listener = malloc(sizeof(*listener));
    if (unlikely(!listener))
        return NULL;

    listener->cbs = cbs;
    listener->userdata = userdata;
    vlc_list_append(&listener->node, &playlist->listeners);

    if (notify_current_state)
        libvlc_playlist_NotifyCurrentState(playlist, listener);

    return listener;
}

void
libvlc_playlist_RemoveListener(libvlc_playlist_t *playlist,
                               libvlc_playlist_listener_id *listener)
{
    (void) playlist;
    vlc_list_remove(&listener->node);
    free(listener);
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
libvlc_playlist_ClearAll(libvlc_playlist_t *playlist)
{
    for (size_t i = 0; i < playlist->items.size; ++i)
        libvlc_playlist_item_Delete(playlist->items.data[i]);
    vlc_vector_clear(&playlist->items);
}

static bool
libvlc_playlist_WrapAll(libvlc_playlist_t *libvlc_playlist,
                        vlc_playlist_item_t *const items[], size_t count,
                        libvlc_playlist_item_t *dest[])
{
    for (size_t i = 0; i < count; ++i)
    {
        libvlc_playlist_item_t *libvlc_item =
            libvlc_playlist_item_Wrap(libvlc_playlist->libvlc, items[i]);
        if (!libvlc_item)
        {
            /* allocation failure, delete inserted items */
            /* ok if i == 0, it wraps (size_t is unsigned) but does not enter
             * the loop */
            while (i--)
                libvlc_playlist_item_Delete(dest[i]);
            return false;
        }
    }
    return true;
}

static bool
libvlc_playlist_WrapInsertAll(libvlc_playlist_t *libvlc_playlist, size_t index,
                              vlc_playlist_item_t *const items[], size_t count)
{
    if (!vlc_vector_insert_hole(&libvlc_playlist->items, index, count))
        return false;

    if (!libvlc_playlist_WrapAll(libvlc_playlist, items, count,
                                 &libvlc_playlist->items.data[index]))
    {
        /* we were optimistic, but it failed */
        vlc_vector_remove_slice(&libvlc_playlist->items, index, count);
        return false;
    }

    return true;
}

static void
libvlc_playlist_Resync(libvlc_playlist_t *playlist)
{
    assert(playlist->must_resync);
    assert(playlist->items.size == 0);


    libvlc_playlist_Notify(playlist, on_items_reset, playlist->items.data,
                           playlist->items.size);
    libvlc_playlist_Notify(playlist, on_current_index_changed,
                           vlc_playlist_GetCurrentIndex(playlist->playlist));
    libvlc_playlist_Notify(playlist, on_has_prev_changed,
                           vlc_playlist_HasPrev(playlist->playlist));
    libvlc_playlist_Notify(playlist, on_has_next_changed,
                           vlc_playlist_HasNext(playlist->playlist));
}

static void
on_items_reset(vlc_playlist_t *playlist, vlc_playlist_item_t *const items[],
               size_t count, void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    /* a reset necessarily resyncs the content with the core playlist */
    libvlc_playlist->must_resync = false;

    libvlc_playlist_ClearAll(libvlc_playlist);
    /* if insertion fails, the libvlc playlist must resync later */
    libvlc_playlist->must_resync =
        !libvlc_playlist_WrapInsertAll(libvlc_playlist, 0, items, count);

    libvlc_playlist_Notify(libvlc_playlist, on_items_reset,
                           libvlc_playlist->items.data,
                           libvlc_playlist->items.size);
}

static void
on_items_added(vlc_playlist_t *playlist, size_t index,
               vlc_playlist_item_t *const items[], size_t count, void *userdata)
{
    struct libvlc_playlist *libvlc_playlist = userdata;

    if (libvlc_playlist->must_resync) {
        libvlc_playlist_Resync(playlist);
        return;
    }
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
    .on_playback_repeat_changed = on_playback_repeat_changed,
    .on_current_index_changed = on_current_index_changed,
    .on_has_prev_changed = on_has_prev_changed,
    .on_has_next_changed = on_has_next_changed,
};

static libvlc_playlist_t *
libvlc_playlist_Wrap(vlc_playlist_t *playlist, libvlc_instance_t *libvlc,
                     bool owned)
{
    libvlc_playlist_t *libvlc_playlist = malloc(sizeof(*libvlc_playlist));
    if (unlikely(!libvlc_playlist))
        return NULL;

    libvlc_playlist->playlist = playlist;
    libvlc_playlist->libvlc = libvlc;
    vlc_vector_init(&libvlc_playlist->items);
    libvlc_playlist->owned = owned;

    libvlc_playlist->listener =
        vlc_playlist_AddListener(playlist, &vlc_playlist_callbacks_wrapper,
                                 libvlc_playlist, true);
    if (unlikely(!libvlc_playlist->listener))
    {
        free(libvlc_playlist);
        return NULL;
    }

    libvlc_retain(libvlc);

    return libvlc_playlist;
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
        vlc_playlist_Delete(playlist);
        return NULL;
    }

    return wrapper;
}
