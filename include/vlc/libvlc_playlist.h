/*****************************************************************************
 * libvlc_playlist.h
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

#ifndef LIBVLC_PLAYLIST_H
#define LIBVLC_PLAYLIST_H

#include <stdbool.h>
#include <unistd.h>
#include "libvlc.h"

# ifdef __cplusplus
extern "C" {
# endif

/**
 * \defgroup playlist playlist
 * \ingroup playlist
 * @{
 */

typedef struct libvlc_media_t libvlc_media_t;

typedef struct libvlc_playlist libvlc_playlist_t;
typedef struct libvlc_playlist_item libvlc_playlist_item_t;
typedef struct libvlc_playlist_listener_id libvlc_playlist_listener_id;

enum libvlc_playlist_playback_repeat
{
    LIBVLC_PLAYLIST_PLAYBACK_REPEAT_NONE,
    LIBVLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT,
    LIBVLC_PLAYLIST_PLAYBACK_REPEAT_ALL,
};

enum libvlc_playlist_playback_order
{
    LIBVLC_PLAYLIST_PLAYBACK_ORDER_NORMAL,
    LIBVLC_PLAYLIST_PLAYBACK_ORDER_RANDOM,
};

enum libvlc_playlist_sort_key
{
    LIBVLC_PLAYLIST_SORT_KEY_TITLE,
    LIBVLC_PLAYLIST_SORT_KEY_DURATION,
    LIBVLC_PLAYLIST_SORT_KEY_ARTIST,
    LIBVLC_PLAYLIST_SORT_KEY_ALBUM,
    LIBVLC_PLAYLIST_SORT_KEY_ALBUM_ARTIST,
    LIBVLC_PLAYLIST_SORT_KEY_GENRE,
    LIBVLC_PLAYLIST_SORT_KEY_DATE,
    LIBVLC_PLAYLIST_SORT_KEY_TRACK_NUMBER,
    LIBVLC_PLAYLIST_SORT_KEY_DISC_NUMBER,
    LIBVLC_PLAYLIST_SORT_KEY_URL,
    LIBVLC_PLAYLIST_SORT_KEY_RATING,
};

enum libvlc_playlist_sort_order
{
    LIBVLC_PLAYLIST_SORT_ORDER_ASCENDING,
    LIBVLC_PLAYLIST_SORT_ORDER_DESCENDING,
};

struct libvlc_playlist_sort_criterion
{
    enum libvlc_playlist_sort_key key;
    enum libvlc_playlist_sort_order order;
};

/**
 * Playlist callbacks.
 *
 * A client may register a listener using libvlc_playlist_AddListener() to listen
 * playlist events.
 *
 * All callbacks are called with the playlist locked (see libvlc_playlist_Lock()).
 */
struct libvlc_playlist_callbacks
{
    /**
     * Called when the whole content has changed (e.g. when the playlist has
     * been cleared, shuffled or sorted).
     *
     * \param playlist the playlist
     * \param items    the whole new content of the playlist
     * \param count    the number of items
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_items_reset)(libvlc_playlist_t *,
                      libvlc_playlist_item_t *const items[], size_t count,
                      void *userdata);

    /**
     * Called when items have been added to the playlist.
     *
     * \param playlist the playlist
     * \param index    the index of the insertion
     * \param items    the array of added items
     * \param count    the number of items added
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_items_added)(libvlc_playlist_t *playlist, size_t index,
                      libvlc_playlist_item_t *const items[], size_t count,
                      void *userdata);

    /**
     * Called when a slice of items have been moved.
     *
     * \param playlist the playlist
     * \param index    the index of the first moved item
     * \param count    the number of items moved
     * \param target   the new index of the moved slice
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_items_moved)(libvlc_playlist_t *playlist, size_t index, size_t count,
                      size_t target, void *userdata);
    /**
     * Called when a slice of items have been removed from the playlist.
     *
     * \param playlist the playlist
     * \param index    the index of the first removed item
     * \param items    the array of removed items
     * \param count    the number of items removed
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_items_removed)(libvlc_playlist_t *playlist, size_t index, size_t count,
                        void *userdata);

    /**
     * Called when an item has been updated via (pre-)parsing.
     *
     * \param playlist the playlist
     * \param index    the index of the first updated item
     * \param items    the array of updated items
     * \param count    the number of items updated
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_items_updated)(libvlc_playlist_t *, size_t index,
                        libvlc_playlist_item_t *const items[], size_t count,
                        void *userdata);

    /**
     * Called when the playback repeat mode has been changed.
     *
     * \param playlist the playlist
     * \param repeat   the new playback "repeat" mode
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_playback_repeat_changed)(libvlc_playlist_t *playlist,
                                  enum libvlc_playlist_playback_repeat repeat,
                                  void *userdata);

    /**
     * Called when the playback order mode has been changed.
     *
     * \param playlist the playlist
     * \param rorder   the new playback order
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_playback_order_changed)(libvlc_playlist_t *playlist,
                                 enum libvlc_playlist_playback_order order,
                                 void *userdata);

    /**
     * Called when the current item index has changed.
     *
     * Note that the current item index may have changed while the current item
     * is still the same: it may have been moved.
     *
     * \param playlist the playlist
     * \param index    the new current index (-1 if there is no current item)
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_current_index_changed)(libvlc_playlist_t *playlist, ssize_t index,
                                void *userdata);

    /**
     * Called when the "has previous item" property has changed.
     *
     * This is typically useful to update any "previous" button in the UI.
     *
     * \param playlist the playlist
     * \param has_prev true if there is a previous item, false otherwise
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_has_prev_changed)(libvlc_playlist_t *, bool has_prev, void *userdata);

    /**
     * Called when the "has next item" property has changed.
     *
     * This is typically useful to update any "next" button in the UI.
     *
     * \param playlist the playlist
     * \param has_next true if there is a next item, false otherwise
     * \param userdata userdata provided to AddListener()
     */
    void
    (*on_has_next_changed)(libvlc_playlist_t *, bool has_next, void *userdata);
};

/* Playlist items */

/**
 * Hold a playlist item.
 *
 * Increment the refcount of the playlist item.
 */
LIBVLC_API void
libvlc_playlist_item_Hold(libvlc_playlist_item_t *);

/**
 * Release a playlist item.
 *
 * Decrement the refcount of the playlist item, and destroy it if necessary.
 */
LIBVLC_API void
libvlc_playlist_item_Release(libvlc_playlist_item_t *);

/**
 * Return the media associated to the playlist item.
 */
LIBVLC_API libvlc_media_t *
libvlc_playlist_item_GetMedia(libvlc_playlist_item_t *);

/* Playlist */

/**
 * Create a new playlist.
 *
 * \param libvlc   the libvlc instance
 * \return a pointer to a valid playlist instance, or NULL if an error occurred
 */
LIBVLC_API LIBVLC_MUST_USE libvlc_playlist_t *
libvlc_playlist_New(libvlc_instance_t *libvlc);

/**
 * Delete a playlist.
 *
 * All playlist items are released, and listeners are removed and destroyed.
 */
LIBVLC_API void
libvlc_playlist_Delete(libvlc_playlist_t *);

/**
 * Lock the playlist/player.
 *
 * The playlist and its player share the same lock, to avoid lock-order
 * inversion issues.
 *
 * \warning Do not forget that the playlist and player lock are the same (or
 * you could lock twice the same and deadlock).
 *
 * Almost all playlist functions must be called with lock held (check their
 * description).
 *
 * The lock is not recursive.
 */
LIBVLC_API void
libvlc_playlist_Lock(libvlc_playlist_t *);

/**
 * Unlock the playlist/player.
 */
LIBVLC_API void
libvlc_playlist_Unlock(libvlc_playlist_t *);

/**
 * Add a playlist listener.
 *
 * Return an opaque listener identifier, to be passed to
 * libvlc_player_RemoveListener().
 *
 * If notify_current_state is true, the callbacks are called once with the
 * current state of the playlist. This is useful because when a client
 * registers to the playlist, it may already contain items. Calling callbacks
 * is a convenient way to initialize the client automatically.
 *
 * \param playlist             the playlist
 * \param cbs                  the callbacks (must be valid until the listener
 *                             is removed)
 * \param userdata             userdata provided as a parameter in callbacks
 * \param notify_current_state true to notify the current state immediately via
 *                             callbacks
 * \return a listener identifier, or NULL if an error occurred
 */
LIBVLC_API LIBVLC_MUST_USE libvlc_playlist_listener_id *
libvlc_playlist_AddListener(libvlc_playlist_t *playlist,
                            const struct libvlc_playlist_callbacks *cbs,
                            void *userdata, bool notify_current_state);

/**
 * Remove a player listener.
 *
 * \param playlist the playlist
 * \param id       the listener identifier returned by
 *                 libvlc_playlist_AddListener()
 */
LIBVLC_API void
libvlc_playlist_RemoveListener(libvlc_playlist_t *,
                               libvlc_playlist_listener_id *);

/**
 * Return the number of items.
 *
 * \param playlist the playlist, locked
 */
LIBVLC_API size_t
libvlc_playlist_Count(libvlc_playlist_t *playlist);

/**
 * Return the item at a given index.
 *
 * The index must be in range (less than libvlc_playlist_Count()).
 *
 * \param playlist the playlist, locked
 * \param index    the index
 * \return the playlist item
 */
LIBVLC_API libvlc_playlist_item_t *
libvlc_playlist_Get(libvlc_playlist_t *playlist, size_t index);

/**
 * Clear the playlist.
 *
 * \param playlist the playlist, locked
 */
LIBVLC_API void
libvlc_playlist_Clear(libvlc_playlist_t *playlist);

/**
 * Insert a list of media at a given index.
 *
 * The index must be in range (less than or equal to libvlc_playlist_Count()).
 *
 * \param playlist the playlist, locked
 * \index index    the index where the media are to be inserted
 * \param media    the array of media to insert
 * \param count    the number of media to insert
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_Insert(libvlc_playlist_t *playlist, size_t index,
                       libvlc_media_t *const media[], size_t count);

/**
 * Insert a media at a given index.
 *
 * The index must be in range (less than or equal to libvlc_playlist_Count()).
 *
 * \param playlist the playlist, locked
 * \index index    the index where the media is to be inserted
 * \param media    the media to insert
 * \return LIBVLC_SUCCESS on success, another value on error
 */
static inline int
libvlc_playlist_InsertOne(libvlc_playlist_t *playlist, size_t index,
                          libvlc_media_t *media)
{
    return libvlc_playlist_Insert(playlist, index, &media, 1);
}

/**
 * Add a list of media at the end of the playlist.
 *
 * \param playlist the playlist, locked
 * \param media    the array of media to append
 * \param count    the number of media to append
 * \return LIBVLC_SUCCESS on success, another value on error
 */
static inline int
libvlc_playlist_Append(libvlc_playlist_t *playlist, libvlc_media_t *const media[],
                       size_t count)
{
    size_t size = libvlc_playlist_Count(playlist);
    return libvlc_playlist_Insert(playlist, size, media, count);
}

/**
 * Add a media at the end of the playlist.
 *
 * \param playlist the playlist, locked
 * \param media    the media to append
 * \return LIBVLC_SUCCESS on success, another value on error
 */
static inline int
libvlc_playlist_AppendOne(libvlc_playlist_t *playlist, libvlc_media_t *media)
{
    return libvlc_playlist_Append(playlist, &media, 1);
}

/**
 * Move a slice of items to a given target index.
 *
 * The slice and the target must be in range (both index+count and target+count
 * less than or equal to libvlc_playlist_Count()).
 *
 * \param playlist the playlist, locked
 * \param index    the index of the first item to move
 * \param count    the number of items to move
 * \param target   the new index of the moved slice
 */
LIBVLC_API void
libvlc_playlist_Move(libvlc_playlist_t *playlist, size_t index, size_t count,
                     size_t target);

/**
 * Move an item to a given target index.
 *
 * The index and the target must be in range (index less than, and target less
 * than or equal to, libvlc_playlist_Count()).
 *
 * \param playlist the playlist, locked
 * \param index    the index of the item to move
 * \param target   the new index of the moved item
 */
static inline void
libvlc_playlist_MoveOne(libvlc_playlist_t *playlist, size_t index,
                        size_t target)
{
    libvlc_playlist_Move(playlist, index, 1, target);
}

/**
 * Remove a slice of items at a given index.
 *
 * The slice must be in range (index+count less than or equal to
 * libvlc_playlist_Count()).
 *
 * \param playlist the playlist, locked
 * \param index    the index of the first item to remove
 * \param count    the number of items to remove
 */
LIBVLC_API void
libvlc_playlist_Remove(libvlc_playlist_t *playlist, size_t index, size_t count);

/**
 * Remove an item at a given index.
 *
 * The index must be in range (less than libvlc_playlist_Count()).
 *
 * \param playlist the playlist, locked
 * \param index    the index of the item to remove
 */
static inline void
libvlc_playlist_RemoveOne(libvlc_playlist_t *playlist, size_t index)
{
    libvlc_playlist_Remove(playlist, index, 1);
}

/**
 * Insert a list of media at a given index (if in range), or append.
 *
 * Contrary to libvlc_playlist_Insert(), the index need not be in range: if it is
 * out of bounds, items will be appended.
 *
 * This is an helper to apply a desynchronized insert request, i.e. the
 * playlist content may have changed since the request had been submitted.
 * This is typically the case for user requests (e.g. from UI), because the
 * playlist lock has to be acquired *after* the user requested the
 * change.
 *
 * \param playlist the playlist, locked
 * \index index    the index where the media are to be inserted
 * \param media    the array of media to insert
 * \param count    the number of media to insert
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_RequestInsert(libvlc_playlist_t *playlist, size_t index,
                              libvlc_media_t *const media[], size_t count);

/**
 * Move a slice of items by value.
 *
 * If the indices are known, use libvlc_playlist_Move() instead.
 *
 * This is an helper to apply a desynchronized move request, i.e. the playlist
 * content may have changed since the request had been submitted. This is
 * typically the case for user requests (e.g. from UI), because the playlist
 * lock has to be acquired *after* the user requested the change.
 *
 * For optimization purpose, it is possible to pass an `index_hint`, which is
 * the expected index of the first item of the slice (as known by the client).
 * Hopefully, the index should often match, since conflicts are expected to be
 * rare. Pass -1 not to pass any hint.
 *
 * \param playlist   the playlist, locked
 * \param items      the array of items to move
 * \param count      the number of items to move
 * \param target     the new index of the moved slice
 * \param index_hint the expected index of the first item (-1 for none)
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_RequestMove(libvlc_playlist_t *playlist,
                            libvlc_playlist_item_t *const items[], size_t count,
                            size_t target, ssize_t index_hint);

/**
 * Remove a slice of items by value.
 *
 * If the indices are known, use libvlc_playlist_Remove() instead.
 *
 * This is an helper to apply a desynchronized remove request, i.e. the
 * playlist content may have changed since the request had been submitted.
 * This is typically the case for user requests (e.g. from UI), because the
 * playlist lock has to be acquired *after* the user requested the change.
 *
 * For optimization purpose, it is possible to pass an `index_hint`, which is
 * the expected index of the first item of the slice (as known by the client).
 * Hopefully, the index should often match, since conflicts are expected to be
 * rare. Pass -1 not to pass any hint.
 *
 * \param playlist   the playlist, locked
 * \param items      the array of items to remove
 * \param count      the number of items to remove
 * \param index_hint the expected index of the first item (-1 for none)
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_RequestRemove(libvlc_playlist_t *playlist,
                              libvlc_playlist_item_t *const items[], size_t count,
                              ssize_t index_hint);

/**
 * Shuffle the playlist.
 *
 * \param playlist the playlist, locked
 */
LIBVLC_API void
libvlc_playlist_Shuffle(libvlc_playlist_t *playlist);

/**
 * Sort the playlist by a list of criteria.
 *
 * \param playlist the playlist, locked
 * \param criteria the sort criteria (in order)
 * \param count    the number of criteria
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_Sort(libvlc_playlist_t *playlist,
                     const struct libvlc_playlist_sort_criterion criteria[],
                     size_t count);

/**
 * Return the index of a given item.
 *
 * \param playlist the playlist, locked
 * \param item     the item to locate
 * \return the index of the item (-1 if not found)
 */
LIBVLC_API ssize_t
libvlc_playlist_IndexOf(libvlc_playlist_t *playlist,
                        const libvlc_playlist_item_t *item);

/**
 * Return the index of a given media.
 *
 * \param playlist the playlist, locked
 * \param media    the media to locate
 * \return the index of the playlist item containing the media (-1 if not found)
 */
LIBVLC_API ssize_t
libvlc_playlist_IndexOfMedia(libvlc_playlist_t *playlist,
                             const libvlc_media_t *media);

/**
 * Return the playback "repeat" mode.
 *
 * \param playlist the playlist, locked
 * \return the playback "repeat" mode
 */
LIBVLC_API enum libvlc_playlist_playback_repeat
libvlc_playlist_GetPlaybackRepeat(libvlc_playlist_t *playlist);

/**
 * Return the playback order.
 *
 * \param playlist the playlist, locked
 * \return the playback order
 */
LIBVLC_API enum libvlc_playlist_playback_order
libvlc_playlist_GetPlaybackOrder(libvlc_playlist_t *);

/**
 * Change the playback "repeat" mode.
 *
 * \param playlist the playlist, locked
 * \param repeat the new playback "repeat" mode
 */
LIBVLC_API void
libvlc_playlist_SetPlaybackRepeat(libvlc_playlist_t *playlist,
                                  enum libvlc_playlist_playback_repeat repeat);

/**
 * Change the playback order
 *
 * \param playlist the playlist, locked
 * \param repeat the new playback order
 */
LIBVLC_API void
libvlc_playlist_SetPlaybackOrder(libvlc_playlist_t *playlist,
                                 enum libvlc_playlist_playback_order order);

/**
 * Return the index of the current item.
 *
 * \param playlist the playlist, locked
 * \return the index of the current item, -1 if none.
 */
LIBVLC_API ssize_t
libvlc_playlist_GetCurrentIndex(libvlc_playlist_t *playlist);

/**
 * Indicate whether a previous item is available.
 *
 * \param playlist the playlist, locked
 * \retval true if a previous item is available
 * \retval false if no previous item is available
 */
LIBVLC_API bool
libvlc_playlist_HasPrev(libvlc_playlist_t *playlist);

/**
 * Indicate whether a next item is available.
 *
 * \param playlist the playlist, locked
 * \retval true if a next item is available
 * \retval false if no next item is available
 */
LIBVLC_API bool
libvlc_playlist_HasNext(libvlc_playlist_t *playlist);

/**
 * Go to the previous item.
 *
 * Return LIBVLC_EGENERIC if libvlc_playlist_HasPrev() returns false.
 *
 * \param playlist the playlist, locked
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_Prev(libvlc_playlist_t *playlist);

/**
 * Go to the next item.
 *
 * Return LIBVLC_EGENERIC if libvlc_playlist_HasNext() returns false.
 *
 * \param playlist the playlist, locked
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_Next(libvlc_playlist_t *playlist);

/**
 * Go to a given index.
 *
 * the index must be -1 or in range (less than libvlc_playlist_Count()).
 *
 * \param playlist the playlist, locked
 * \param index    the index to go to (-1 to none)
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_GoTo(libvlc_playlist_t *playlist, ssize_t index);

/**
 * Go to a given item.
 *
 * If the index is known, use libvlc_playlist_GoTo() instead.
 *
 * This is an helper to apply a desynchronized "go to" request, i.e. the
 * playlist content may have changed since the request had been submitted.
 * This is typically the case for user requests (e.g. from UI), because the
 * playlist lock has to be acquired *after* the user requested the change.
 *
 * For optimization purpose, it is possible to pass an `index_hint`, which is
 * the expected index of the first item of the slice (as known by the client).
 * Hopefully, the index should often match, since conflicts are expected to be
 * rare. Pass -1 not to pass any hint.
 *
 * \param playlist   the playlist, locked
 * \param item       the item to go to (NULL for none)
 * \param index_hint the expected index of the item (-1 for none)
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_RequestGoTo(libvlc_playlist_t *playlist,
                            libvlc_playlist_item_t *item, ssize_t index_hint);

/**
 * Return the player owned by the playlist.
 *
 * \param playlist the playlist (not necessarily locked)
 * \return the player
 */
//LIBVLC_API libvlc_player_t *
//libvlc_playlist_GetPlayer(libvlc_playlist_t *playlist);

/**
 * Start the player.
 *
 * \param playlist the playlist, locked
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API int
libvlc_playlist_Start(libvlc_playlist_t *playlist);

/**
 * Stop the player.
 *
 * \param playlist the playlist, locked
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API void
libvlc_playlist_Stop(libvlc_playlist_t *playlist);

/**
 * Pause the player.
 *
 * \param playlist the playlist, locked
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API void
libvlc_playlist_Pause(libvlc_playlist_t *playlist);

/**
 * Resume the player.
 *
 * \param playlist the playlist, locked
 * \return LIBVLC_SUCCESS on success, another value on error
 */
LIBVLC_API void
libvlc_playlist_Resume(libvlc_playlist_t *playlist);

/**
 * Preparse a media, and expand it in the playlist on subitems added.
 *
 * \param playlist the playlist (not necessarily locked)
 * \param libvlc the libvlc instance
 * \param media the media to preparse
 */
LIBVLC_API void
libvlc_playlist_Preparse(libvlc_playlist_t *playlist, libvlc_instance_t *libvlc,
                         libvlc_media_t *media);

/** @} */

# ifdef __cplusplus
}
# endif

 #endif
