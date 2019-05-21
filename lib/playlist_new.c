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

    /**
     * On user interaction, we need to keep a reference to the libvlc_media_t
     * instance they provided to map them with input_item_t on callbacks.
     */
    libvlc_media_t *const *user_media;
    size_t user_media_count;
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

// media may be NULL or an existing instance provided by the client
static libvlc_playlist_item_t *
libvlc_playlist_item_Wrap(libvlc_instance_t *libvlc, vlc_playlist_item_t *item,
                          libvlc_media_t *media)
{
    libvlc_playlist_item_t *libvlc_item = malloc(sizeof(*libvlc_item));
    if (unlikely(!libvlc_item))
        return NULL;

    input_item_t *media_item = vlc_playlist_item_GetMedia(item);
    if (!media) {
        media = libvlc_media_new_from_input_item(libvlc, media_item);
        if (unlikely(!media))
        {
            free(libvlc_item);
            return NULL;
        }
    } else {
        assert(media->p_input_item == vlc_playlist_item_GetMedia(item));
    }

    vlc_playlist_item_Hold(item);

    libvlc_item->item = item;
    libvlc_item->media = media;
    vlc_atomic_rc_init(&libvlc_item->rc);

    return libvlc_item;
}

static void
libvlc_playlist_item_Delete(libvlc_playlist_item_t *libvlc_item)
{
    vlc_playlist_item_Release(libvlc_item->item);
    libvlc_media_release(libvlc_item->media);
    free(libvlc_item);
}

void
libvlc_playlist_item_Hold(libvlc_playlist_item_t *libvlc_item)
{
    vlc_atomic_rc_inc(&libvlc_item->rc);
}

void
libvlc_playlist_item_Release(libvlc_playlist_item_t *libvlc_item)
{
    if (vlc_atomic_rc_dec(&libvlc_item->rc))
    {
        vlc_playlist_item_Release(libvlc_item->item);
        free(libvlc_item);
    }
}

libvlc_media_t *
libvlc_playlist_item_GetMedia(libvlc_playlist_item_t *libvlc_item)
{
    return libvlc_item->media;
}

uint64_t
libvlc_playlist_item_GetId(libvlc_playlist_item_t *libvlc_item)
{
    return vlc_playlist_item_GetId(libvlc_item->item);
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

    (void) notify_current_state;
    // TODO
    // if (notify_current_state)
    //     libvlc_playlist_NotifyCurrentState(playlist, listener);

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

static libvlc_media_t *
libvlc_playlist_FindUserMedia(libvlc_playlist_t *libvlc_playlist,
                              input_item_t *input_item, ssize_t index_hint)
{
    if (index_hint != -1 &&
        (size_t) index_hint < libvlc_playlist->user_media_count)
    {
        libvlc_media_t *media = libvlc_playlist->user_media[index_hint];
        if (media->p_input_item == input_item)
            return media;
    }

    for (size_t i = 0; i < libvlc_playlist->user_media_count; ++i)
    {
        libvlc_media_t *media = libvlc_playlist->user_media[index_hint];
        if (media->p_input_item == input_item)
            return media;
    }

    return NULL;
}

static bool
libvlc_playlist_WrapAllItems(libvlc_playlist_t *libvlc_playlist,
                             vlc_playlist_item_t *const items[], size_t count,
                             libvlc_playlist_item_t *dest[])
{
    for (size_t i = 0; i < count; ++i)
    {
        vlc_playlist_item_t *item = items[i];
        input_item_t *media = vlc_playlist_item_GetMedia(item);

        libvlc_media_t *libvlc_media =
            libvlc_playlist_FindUserMedia(libvlc_playlist, media, i);

        libvlc_playlist_item_t *libvlc_item =
            libvlc_playlist_item_Wrap(libvlc_playlist->libvlc, items[i],
                                      libvlc_media);

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

    if (!libvlc_playlist_WrapAllItems(libvlc_playlist, items, count,
                                      &libvlc_playlist->items.data[index]))
    {
        /* we were optimistic, but it failed */
        vlc_vector_remove_slice(&libvlc_playlist->items, index, count);
        return false;
    }

    return true;
}

static input_item_t **
UnwrapAllMedia(libvlc_media_t *const libvlc_media[], size_t count)
{
    input_item_t **input_items = vlc_alloc(count, sizeof(*input_items));
    if (!input_items)
        return NULL;

    for (size_t i = 0; i < count; ++i)
        input_items[i] = libvlc_media[i]->p_input_item;
    return input_items;
}

static vlc_playlist_item_t **
UnwrapAllItems(libvlc_playlist_item_t *const libvlc_items[], size_t count)
{
    vlc_playlist_item_t **items = vlc_alloc(count, sizeof(*items));
    if (!items)
        return NULL;

    for (size_t i = 0; i < count; ++i)
        items[i] = libvlc_items[i]->item;
    return items;
}

/**
 * Expose an empty libvlc playlist, because it cannot be keep in sync with the
 * core playlist, due to allocation error.
 */
static void
libvlc_playlist_Desync(libvlc_playlist_t *libvlc_playlist)
{
    assert(!libvlc_playlist->must_resync);
    libvlc_playlist->must_resync = true;

    libvlc_playlist_ClearAll(libvlc_playlist);

    libvlc_playlist_Notify(libvlc_playlist, on_items_reset, NULL, 0);
    libvlc_playlist_Notify(libvlc_playlist, on_current_index_changed, -1);
    libvlc_playlist_Notify(libvlc_playlist, on_has_prev_changed, false);
    libvlc_playlist_Notify(libvlc_playlist, on_has_next_changed, false);
}

static void
libvlc_playlist_Resync(libvlc_playlist_t *libvlc_playlist)
{
    assert(libvlc_playlist->must_resync);
    assert(libvlc_playlist->items.size == 0);

    vlc_playlist_t *playlist = libvlc_playlist->playlist;

    vlc_playlist_item_t *const *items = vlc_playlist_GetItems(playlist);
    size_t count = vlc_playlist_Count(playlist);
    if (!libvlc_playlist_WrapInsertAll(libvlc_playlist, 0, items, count))
        /* resync failed */
        return;

    /* resync succeeded */
    libvlc_playlist->must_resync = false;

    /* notify current state to all listeners */
    libvlc_playlist_Notify(libvlc_playlist, on_items_reset,
                           libvlc_playlist->items.data,
                           libvlc_playlist->items.size);
    libvlc_playlist_Notify(libvlc_playlist, on_current_index_changed,
                           vlc_playlist_GetCurrentIndex(playlist));
    libvlc_playlist_Notify(libvlc_playlist, on_has_prev_changed,
                           vlc_playlist_HasPrev(playlist));
    libvlc_playlist_Notify(libvlc_playlist, on_has_next_changed,
                           vlc_playlist_HasNext(playlist));

    /* playback repeat and order modes are still valid while desynchronized, so
     * there is no need to notifying their current state */
}

static void
on_items_reset(vlc_playlist_t *playlist, vlc_playlist_item_t *const items[],
               size_t count, void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    if (libvlc_playlist->must_resync)
    {
        libvlc_playlist_Resync(libvlc_playlist);
        return;
    }

    libvlc_playlist_ClearAll(libvlc_playlist);

    if (!libvlc_playlist_WrapInsertAll(libvlc_playlist, 0, items, count))
    {
        libvlc_playlist_Desync(libvlc_playlist);
        return;
    }

    libvlc_playlist_Notify(libvlc_playlist, on_items_reset,
                           libvlc_playlist->items.data,
                           libvlc_playlist->items.size);
}

static void
on_items_added(vlc_playlist_t *playlist, size_t index,
               vlc_playlist_item_t *const items[], size_t count, void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    if (libvlc_playlist->must_resync)
    {
        libvlc_playlist_Resync(libvlc_playlist);
        return;
    }

    if (!libvlc_playlist_WrapInsertAll(libvlc_playlist, index, items, count))
    {
        libvlc_playlist_Desync(libvlc_playlist);
        return;
    }

    libvlc_playlist_Notify(libvlc_playlist, on_items_added, index,
                           &libvlc_playlist->items.data[index], count);
}

static void
on_items_moved(vlc_playlist_t *playlist, size_t index, size_t count,
               size_t target, void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    assert(index + count <= libvlc_playlist->items.size);
    assert(target + count <= libvlc_playlist->items.size);

    if (libvlc_playlist->must_resync)
    {
        libvlc_playlist_Resync(libvlc_playlist);
        return;
    }

    vlc_vector_move_slice(&libvlc_playlist->items, index, count, target);

    libvlc_playlist_Notify(libvlc_playlist, on_items_moved, index, count,
                           target);
}

static void
on_items_removed(vlc_playlist_t *playlist, size_t index, size_t count,
                 void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    if (libvlc_playlist->must_resync)
    {
        libvlc_playlist_Resync(libvlc_playlist);
        return;
    }

    vlc_vector_remove_slice(&libvlc_playlist->items, index, count);

    libvlc_playlist_Notify(libvlc_playlist, on_items_removed, index, count);
}

static void
on_items_updated(vlc_playlist_t *playlist, size_t index,
                 vlc_playlist_item_t *const items[], size_t count,
                 void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    if (libvlc_playlist->must_resync)
    {
        libvlc_playlist_Resync(libvlc_playlist);
        return;
    }

    for (size_t i = 0; i < count; ++i)
    {
        libvlc_playlist_item_t *libvlc_item =
            libvlc_playlist_item_Wrap(libvlc_playlist->libvlc, items[i], NULL);
        if (!libvlc_item)
        {
            libvlc_playlist_Desync(libvlc_playlist);
            return;
        }

        libvlc_playlist_item_Delete(libvlc_playlist->items.data[i]);
        libvlc_playlist->items.data[i] = libvlc_item;
    }

    libvlc_playlist_Notify(libvlc_playlist, on_items_updated, index,
                           &libvlc_playlist->items.data[index], count);
}

static enum libvlc_playlist_playback_repeat
CoreToLibvlcRepeat(enum vlc_playlist_playback_repeat repeat)
{
    switch (repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            return LIBVLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
            return LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL;
        default:
            return LIBVLC_PLAYLIST_PLAYBACK_REPEAT_NONE;
    }
}

static enum libvlc_playlist_playback_order
CoreToLibvlcOrder(enum vlc_playlist_playback_order order)
{
    switch (order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return LIBVLC_PLAYLIST_PLAYBACK_ORDER_RANDOM;
        default:
            return LIBVLC_PLAYLIST_PLAYBACK_ORDER_NORMAL;
    }
}

static void
on_playback_repeat_changed(vlc_playlist_t *playlist,
                           enum vlc_playlist_playback_repeat repeat,
                           void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    libvlc_playlist_Notify(libvlc_playlist, on_playback_repeat_changed,
                           CoreToLibvlcRepeat(repeat));
}

static void
on_playback_order_changed(vlc_playlist_t *playlist,
                          enum vlc_playlist_playback_order order,
                          void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    libvlc_playlist_Notify(libvlc_playlist, on_playback_order_changed,
                           CoreToLibvlcOrder(order));
}

static void
on_current_index_changed(vlc_playlist_t *playlist, ssize_t index,
                         void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    if (libvlc_playlist->must_resync)
    {
        libvlc_playlist_Resync(libvlc_playlist);
        return;
    }

    libvlc_playlist_Notify(libvlc_playlist, on_current_index_changed, index);
}

static void
on_has_prev_changed(vlc_playlist_t *playlist, bool has_prev, void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    if (libvlc_playlist->must_resync)
    {
        libvlc_playlist_Resync(libvlc_playlist);
        return;
    }

    libvlc_playlist_Notify(libvlc_playlist, on_has_prev_changed, has_prev);
}

static void
on_has_next_changed(vlc_playlist_t *playlist, bool has_next, void *userdata)
{
    (void) playlist;

    struct libvlc_playlist *libvlc_playlist = userdata;

    if (libvlc_playlist->must_resync)
    {
        libvlc_playlist_Resync(libvlc_playlist);
        return;
    }

    libvlc_playlist_Notify(libvlc_playlist, on_has_next_changed, has_next);
}

static const struct vlc_playlist_callbacks vlc_playlist_callbacks_wrapper = {
    .on_items_reset = on_items_reset,
    .on_items_added = on_items_added,
    .on_items_moved = on_items_moved,
    .on_items_removed = on_items_removed,
    .on_items_updated = on_items_updated,
    .on_playback_repeat_changed = on_playback_repeat_changed,
    .on_playback_order_changed = on_playback_order_changed,
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
    libvlc_playlist->user_media = NULL;
    libvlc_playlist->user_media_count = 0;

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

size_t
libvlc_playlist_Count(libvlc_playlist_t *libvlc_playlist)
{
    return libvlc_playlist->items.size;
}

libvlc_playlist_item_t *
libvlc_playlist_Get(libvlc_playlist_t *libvlc_playlist, size_t index)
{
    return libvlc_playlist->items.data[index];
}

libvlc_playlist_item_t *const *
libvlc_playlist_GetItems(libvlc_playlist_t *libvlc_playlist)
{
    return libvlc_playlist->items.data;
}

void
libvlc_playlist_Clear(libvlc_playlist_t *libvlc_playlist)
{
    vlc_playlist_Clear(libvlc_playlist->playlist);
}

// save the instance of libvlc_media to reuse them on core playlist events
// instead of creating new instances
static inline void
libvlc_playlist_SaveUserMedia(libvlc_playlist_t *libvlc_playlist,
                              libvlc_media_t *const libvlc_media[],
                              size_t count)
{
    libvlc_playlist->user_media = libvlc_media;
    libvlc_playlist->user_media_count = count;
}

static inline void
libvlc_playlist_ResetUserMedia(libvlc_playlist_t *libvlc_playlist)
{
    libvlc_playlist->user_media = NULL;
    libvlc_playlist->user_media_count = 0;
}

int
libvlc_playlist_Insert(libvlc_playlist_t *libvlc_playlist, size_t index,
                       libvlc_media_t *const libvlc_media[], size_t count)
{
    vlc_playlist_t *playlist = libvlc_playlist->playlist;

    input_item_t **input_items = UnwrapAllMedia(libvlc_media, count);
    if (!input_items)
        return VLC_ENOMEM;

    libvlc_playlist_SaveUserMedia(libvlc_playlist, libvlc_media, count);
    int res = vlc_playlist_Insert(playlist, index, input_items, count);
    libvlc_playlist_ResetUserMedia(libvlc_playlist);

    free(input_items);

    return res;
}

void
libvlc_playlist_Move(libvlc_playlist_t *libvlc_playlist, size_t index,
                     size_t count, size_t target)
{
    vlc_playlist_Move(libvlc_playlist->playlist, index, count, target);
}

void
libvlc_playlist_Remove(libvlc_playlist_t *libvlc_playlist, size_t index,
                       size_t count)
{
    vlc_playlist_Remove(libvlc_playlist->playlist, index, count);
}

int
libvlc_playlist_RequestInsert(libvlc_playlist_t *libvlc_playlist, size_t index,
                              libvlc_media_t *const libvlc_media[],
                              size_t count)
{
    vlc_playlist_t *playlist = libvlc_playlist->playlist;

    input_item_t **input_items = UnwrapAllMedia(libvlc_media, count);
    if (!input_items)
        return VLC_ENOMEM;

    libvlc_playlist_SaveUserMedia(libvlc_playlist, libvlc_media, count);
    int res = vlc_playlist_RequestInsert(playlist, index, input_items, count);
    libvlc_playlist_ResetUserMedia(libvlc_playlist);

    free(input_items);

    return res;
}

int
libvlc_playlist_RequestMove(libvlc_playlist_t *libvlc_playlist,
                            libvlc_playlist_item_t *const libvlc_items[],
                            size_t count, size_t target, ssize_t index_hint)
{
    vlc_playlist_t *playlist = libvlc_playlist->playlist;

    vlc_playlist_item_t **items = UnwrapAllItems(libvlc_items, count);
    if (!items)
        return VLC_ENOMEM;

    int res =
        vlc_playlist_RequestMove(playlist, items, count, target, index_hint);

    free(items);
    return res;
}

int
libvlc_playlist_RequestRemove(libvlc_playlist_t *libvlc_playlist,
                              libvlc_playlist_item_t *const libvlc_items[],
                              size_t count, ssize_t index_hint)
{
    vlc_playlist_t *playlist = libvlc_playlist->playlist;

    vlc_playlist_item_t **items = UnwrapAllItems(libvlc_items, count);
    if (!items)
        return VLC_ENOMEM;

    int res =
        vlc_playlist_RequestRemove(playlist, items, count, index_hint);

    free(items);
    return res;
}

void
libvlc_playlist_Shuffle(libvlc_playlist_t *libvlc_playlist)
{
    vlc_playlist_Shuffle(libvlc_playlist->playlist);
}

static enum libvlc_playlist_sort_key
LibvlcToCoreSortKey(enum libvlc_playlist_sort_key key)
{
    switch (key) {
        case LIBVLC_PLAYLIST_SORT_KEY_TITLE:
            return VLC_PLAYLIST_SORT_KEY_TITLE;
        case LIBVLC_PLAYLIST_SORT_KEY_DURATION:
            return VLC_PLAYLIST_SORT_KEY_DURATION;
        case LIBVLC_PLAYLIST_SORT_KEY_ARTIST:
            return VLC_PLAYLIST_SORT_KEY_ARTIST;
        case LIBVLC_PLAYLIST_SORT_KEY_ALBUM:
            return VLC_PLAYLIST_SORT_KEY_ALBUM;
        case LIBVLC_PLAYLIST_SORT_KEY_ALBUM_ARTIST:
            return VLC_PLAYLIST_SORT_KEY_ALBUM_ARTIST;
        case LIBVLC_PLAYLIST_SORT_KEY_GENRE:
            return VLC_PLAYLIST_SORT_KEY_GENRE;
        case LIBVLC_PLAYLIST_SORT_KEY_DATE:
            return VLC_PLAYLIST_SORT_KEY_DATE;
        case LIBVLC_PLAYLIST_SORT_KEY_TRACK_NUMBER:
            return VLC_PLAYLIST_SORT_KEY_TRACK_NUMBER;
        case LIBVLC_PLAYLIST_SORT_KEY_DISC_NUMBER:
            return VLC_PLAYLIST_SORT_KEY_DISC_NUMBER;
        case LIBVLC_PLAYLIST_SORT_KEY_URL:
            return VLC_PLAYLIST_SORT_KEY_URL;
        case LIBVLC_PLAYLIST_SORT_KEY_RATING:
            return VLC_PLAYLIST_SORT_KEY_RATING;
        default:
            vlc_assert_unreachable();
    }
}

static enum libvlc_playlist_sort_order
LibvlcToCoreSortOrder(enum libvlc_playlist_sort_order order)
{
    switch (order) {
        case LIBVLC_PLAYLIST_SORT_ORDER_ASCENDING:
            return VLC_PLAYLIST_SORT_ORDER_ASCENDING;
        case LIBVLC_PLAYLIST_SORT_ORDER_DESCENDING:
            return VLC_PLAYLIST_SORT_ORDER_DESCENDING;
        default:
            vlc_assert_unreachable();
    }
}

static struct vlc_playlist_sort_criterion
LibvlcToCoreSortCriterion(
    const struct libvlc_playlist_sort_criterion *libvlc_criterion)
{
    return (struct vlc_playlist_sort_criterion) {
        .key = LibvlcToCoreSortKey(libvlc_criterion->key),
        .order = LibvlcToCoreSortOrder(libvlc_criterion->order),
    };
}

static struct vlc_playlist_sort_criterion *
UnwrapCriteria(const struct libvlc_playlist_sort_criterion libvlc_criteria[],
               size_t count)
{
    struct vlc_playlist_sort_criterion *criteria =
        vlc_alloc(count, sizeof(*criteria));
    if (!criteria)
        return NULL;

    for (size_t i = 0; i < count; ++i)
        criteria[i] = LibvlcToCoreSortCriterion(&libvlc_criteria[i]);
    return criteria;
}

int
libvlc_playlist_Sort(libvlc_playlist_t *libvlc_playlist,
                 const struct libvlc_playlist_sort_criterion libvlc_criteria[],
                 size_t count)
{
    vlc_playlist_t *playlist = libvlc_playlist->playlist;

    struct vlc_playlist_sort_criterion *criteria =
        UnwrapCriteria(libvlc_criteria, count);
    if (!criteria)
        return VLC_ENOMEM;

    int res = vlc_playlist_Sort(playlist, criteria, count);

    free(criteria);
    return res;
}

ssize_t
libvlc_playlist_IndexOf(libvlc_playlist_t *libvlc_playlist,
                        const libvlc_playlist_item_t *libvlc_item)
{
    ssize_t index =
        vlc_playlist_IndexOf(libvlc_playlist->playlist, libvlc_item->item);
    assert(index == -1 || libvlc_playlist->items.data[index] == libvlc_item);
    return index;
}

ssize_t
libvlc_playlist_IndexOfMedia(libvlc_playlist_t *libvlc_playlist,
                             const libvlc_media_t *libvlc_media)
{
    ssize_t index = vlc_playlist_IndexOfMedia(libvlc_playlist->playlist,
                                              libvlc_media->p_input_item);
    assert(index == -1 ||
           libvlc_playlist->items.data[index]->media == libvlc_media);
    return index;
}

ssize_t
libvlc_playlist_IndexOfId(libvlc_playlist_t *libvlc_playlist, uint64_t id)
{
    return vlc_playlist_IndexOfId(libvlc_playlist->playlist, id);
}

static enum vlc_playlist_playback_repeat
LibvlcToCoreRepeat(enum libvlc_playlist_playback_repeat libvlc_repeat)
{
    switch (libvlc_repeat) {
        case LIBVLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
            return VLC_PLAYLIST_PLAYBACK_REPEAT_NONE;
        case LIBVLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            return VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT;
        case LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
            return VLC_PLAYLIST_PLAYBACK_REPEAT_ALL;
        default:
            vlc_assert_unreachable();
    }
}

static enum vlc_playlist_playback_order
LibvlcToCoreOrder(enum libvlc_playlist_playback_order libvlc_order)
{
    switch (libvlc_order) {
        case LIBVLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL;
        case LIBVLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM;
        default:
            vlc_assert_unreachable();
    }
}

void
libvlc_playlist_SetPlaybackRepeat(libvlc_playlist_t *libvlc_playlist,
                          enum libvlc_playlist_playback_repeat libvlc_repeat)
{
    vlc_playlist_t *playlist = libvlc_playlist->playlist;
    enum vlc_playlist_playback_repeat repeat =
        LibvlcToCoreRepeat(libvlc_repeat);
    vlc_playlist_SetPlaybackRepeat(playlist, repeat);
}

void
libvlc_playlist_SetPlaybackOrder(libvlc_playlist_t *libvlc_playlist,
                             enum libvlc_playlist_playback_order libvlc_order)
{
    vlc_playlist_t *playlist = libvlc_playlist->playlist;
    enum vlc_playlist_playback_order order = LibvlcToCoreOrder(libvlc_order);
    vlc_playlist_SetPlaybackOrder(playlist, order);
}

ssize_t
libvlc_playlist_GetCurrentIndex(libvlc_playlist_t *libvlc_playlist)
{
    if (libvlc_playlist->must_resync)
        return -1;

    return vlc_playlist_GetCurrentIndex(libvlc_playlist->playlist);
}

bool
libvlc_playlist_HasPrev(libvlc_playlist_t *libvlc_playlist)
{
    if (libvlc_playlist->must_resync)
        return false;

    return vlc_playlist_HasPrev(libvlc_playlist->playlist);
}

bool
libvlc_playlist_HasNext(libvlc_playlist_t *libvlc_playlist)
{
    if (libvlc_playlist->must_resync)
        return false;

    return vlc_playlist_HasNext(libvlc_playlist->playlist);
}

int
libvlc_playlist_Prev(libvlc_playlist_t *libvlc_playlist)
{
    return vlc_playlist_Prev(libvlc_playlist->playlist);
}

int
libvlc_playlist_Next(libvlc_playlist_t *libvlc_playlist)
{
    return vlc_playlist_Next(libvlc_playlist->playlist);
}

int
libvlc_playlist_GoTo(libvlc_playlist_t *libvlc_playlist, ssize_t index)
{
    return vlc_playlist_GoTo(libvlc_playlist->playlist, index);
}

int
libvlc_playlist_RequestGoTo(libvlc_playlist_t *libvlc_playlist,
                            libvlc_playlist_item_t *libvlc_item,
                            ssize_t index_hint)
{
    vlc_playlist_t *playlist = libvlc_playlist->playlist;
    return vlc_playlist_RequestGoTo(playlist, libvlc_item->item, index_hint);
}
