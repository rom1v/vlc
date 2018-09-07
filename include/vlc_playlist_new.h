/*****************************************************************************
 * vlc_playlist_new.h
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

#ifndef VLC_PLAYLIST_NEW_H_
#define VLC_PLAYLIST_NEW_H_

#include <vlc_common.h>

# ifdef __cplusplus
extern "C" {
# endif

/**
 * \defgroup playlist VLC playlist
 * \ingroup playlist
 * VLC playlist controls
 * @{
 */

typedef struct input_item_t input_item_t;
typedef struct vlc_player_t vlc_player_t;

/* opaque types */
typedef struct vlc_playlist vlc_playlist_t;
typedef struct vlc_playlist_item vlc_playlist_item_t;
typedef struct vlc_playlist_listener_id vlc_playlist_listener_id;

enum vlc_playlist_playback_repeat
{
    VLC_PLAYLIST_PLAYBACK_REPEAT_NONE,
    VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT,
    VLC_PLAYLIST_PLAYBACK_REPEAT_ALL,
};

enum vlc_playlist_playback_order
{
    VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL,
    VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM,
};

struct vlc_playlist_callbacks
{
    /* The whole list have been reset (typically after a clear, shuffle or
     * sort) */
    void (*on_items_reset)(vlc_playlist_t *, vlc_playlist_item_t *const [],
                           size_t len, void *userdata);
    void (*on_items_added)(vlc_playlist_t *, size_t index,
                           vlc_playlist_item_t *const [], size_t len,
                           void *userdata);
    void (*on_items_removed)(vlc_playlist_t *, size_t index,
                             vlc_playlist_item_t *const [], size_t len,
                             void *userdata);
    void (*on_item_updated)(vlc_playlist_t *, size_t index,
                            vlc_playlist_item_t *, void *userdata);
    void (*on_playback_repeat_changed)(vlc_playlist_t *,
                                       enum vlc_playlist_playback_repeat,
                                       void *userdata);
    void (*on_playback_order_changed)(vlc_playlist_t *,
                                      enum vlc_playlist_playback_order,
                                      void *userdata);
    void (*on_current_item_changed)(vlc_playlist_t *, ssize_t index,
                                    vlc_playlist_item_t *, void *userdata);
    void (*on_has_prev_changed)(vlc_playlist_t *, bool has_prev,
                                void *userdata);
    void (*on_has_next_changed)(vlc_playlist_t *, bool has_next,
                                void *userdata);
};

/* playlist item */

VLC_API void
vlc_playlist_item_Hold(vlc_playlist_item_t *);

VLC_API void
vlc_playlist_item_Release(vlc_playlist_item_t *);

VLC_API input_item_t *
vlc_playlist_item_GetMedia(vlc_playlist_item_t *);

/* playlist */

VLC_API vlc_playlist_t *
vlc_playlist_New(vlc_object_t *parent);

VLC_API void
vlc_playlist_Delete(vlc_playlist_t *);

VLC_API void
vlc_playlist_Lock(vlc_playlist_t *);

VLC_API void
vlc_playlist_Unlock(vlc_playlist_t *);

VLC_API vlc_playlist_listener_id *
vlc_playlist_AddListener(vlc_playlist_t *,
                         const struct vlc_playlist_callbacks *,
                         void *userdata);

VLC_API void
vlc_playlist_RemoveListener(vlc_playlist_t *, vlc_playlist_listener_id *);

VLC_API size_t
vlc_playlist_Count(vlc_playlist_t *);

VLC_API vlc_playlist_item_t *
vlc_playlist_Get(vlc_playlist_t *, size_t index);

VLC_API void
vlc_playlist_Clear(vlc_playlist_t *);

VLC_API vlc_playlist_item_t *
vlc_playlist_Append(vlc_playlist_t *, input_item_t *);

VLC_API int
vlc_playlist_AppendAll(vlc_playlist_t *, input_item_t *const [], size_t count,
                       vlc_playlist_item_t *out[]);

VLC_API vlc_playlist_item_t *
vlc_playlist_Insert(vlc_playlist_t *, size_t index, input_item_t *);

VLC_API int
vlc_playlist_InsertAll(vlc_playlist_t *, size_t index, input_item_t *const [],
                       size_t count, vlc_playlist_item_t *out[]);

VLC_API void
vlc_playlist_Remove(vlc_playlist_t *, size_t index);

VLC_API void
vlc_playlist_RemoveSlice(vlc_playlist_t *, size_t index, size_t count);

VLC_API ssize_t
vlc_playlist_IndexOf(vlc_playlist_t *, const vlc_playlist_item_t *);

VLC_API ssize_t
vlc_playlist_IndexOfMedia(vlc_playlist_t *, const input_item_t *);

VLC_API enum vlc_playlist_playback_repeat
vlc_playlist_GetPlaybackRepeat(vlc_playlist_t *);

VLC_API enum vlc_playlist_playback_order
vlc_playlist_GetPlaybackOrder(vlc_playlist_t *);

VLC_API void
vlc_playlist_SetPlaybackRepeat(vlc_playlist_t *,
                               enum vlc_playlist_playback_repeat);

VLC_API void
vlc_playlist_SetPlaybackOrder(vlc_playlist_t *,
                              enum vlc_playlist_playback_order);

VLC_API ssize_t
vlc_playlist_GetCurrentIndex(vlc_playlist_t *);

VLC_API bool
vlc_playlist_HasPrev(vlc_playlist_t *);

VLC_API bool
vlc_playlist_HasNext(vlc_playlist_t *);

VLC_API int
vlc_playlist_Prev(vlc_playlist_t *);

VLC_API int
vlc_playlist_Next(vlc_playlist_t *);

VLC_API int
vlc_playlist_GoTo(vlc_playlist_t *, size_t index);

VLC_API vlc_player_t *
vlc_playlist_GetPlayer(vlc_playlist_t *);

VLC_API int
vlc_playlist_Start(vlc_playlist_t *);

VLC_API void
vlc_playlist_Stop(vlc_playlist_t *);

VLC_API void
vlc_playlist_Pause(vlc_playlist_t *);

VLC_API void
vlc_playlist_Resume(vlc_playlist_t *);

VLC_API void
vlc_playlist_Preparse(vlc_playlist_t *, libvlc_int_t *libvlc, input_item_t *);

/** @} */
# ifdef __cplusplus
}
# endif

#endif
