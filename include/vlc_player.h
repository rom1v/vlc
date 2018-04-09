/*****************************************************************************
 * vlc_player.h: player interface
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

#ifndef VLC_PLAYER_H
#define VLC_PLAYER_H 1

#include <vlc_input.h>

/**
 * \defgroup Player
 * \ingroup input
 * @{
 */

/**
 * Player opaque vlc_object structure.
 */
typedef struct vlc_player_t vlc_player_t;

/**
 * Player listener opaque structure.
 *
 * This opaque structure is returned by vlc_player_AddListener() and can be
 * used to remove the listener via vlc_player_RemoveListener().
 */
struct vlc_player_listener_id;

/**
 * Player program structure.
 */
struct vlc_player_program
{
    int id;
    const char *title;
    bool selected;
    bool scrambled;
};

/**
 * Player ES track structure.
 */
struct vlc_player_es
{
    vlc_es_id_t *id;
    const char *title;
    const es_format_t *fmt;
    bool selected;
};

/**
 * Callbacks for the owner of the player.
 *
 * These callbacks are needed to control the player flow (via the
 * vlc_playlist_t as a owner for example). It can only be set when creating the
 * player via vlc_player_New().
 *
 * All callbacks are called with the player locked (cf. vlc_player_Lock()).
 */
struct vlc_player_owner_cbs
{
    void (*on_current_playback_changed)(vlc_player_t *player,
                                        input_item_t *new_item, void *userdata);
    input_item_t *(*get_next_item)(vlc_player_t *player, void *userdata);
};

/**
 * Callbacks to get the state of the input.
 *
 * Can be registered with vlc_player_AddListener().
 *
 * All callbacks are called with the player locked (cf. vlc_player_Lock()).
 */
struct vlc_player_cbs
{
    void (*on_input_event)(vlc_player_t *player, input_item_t *item,
                           const struct vlc_input_event *event, void *userdata);
};

/**
 * Create a new player instance.
 *
 * @param parent parent VLC object
 * @parent owner_cbs callbacks for the owner
 * @parent owner_cbs_data opaque data for owner callbacks
 * @return a pointer to a valid player instance or NULL in case of error
 */
VLC_API vlc_player_t *
vlc_player_New(vlc_object_t *parent,
               const struct vlc_player_owner_cbs *owner_cbs,
               void *owner_cbs_data);

/**
 * Delete the player.
 *
 * @param player unlocked player instance created by vlc_player_New()
 */
VLC_API void
vlc_player_Delete(vlc_player_t *player);

/**
 * Lock the player
 *
 * All player functions (except vlc_player_Delete()) need to be called while
 * the player lock is held.
 * @param player unlocked player instance
 */
VLC_API void
vlc_player_Lock(vlc_player_t *player);

/**
 * Unlock the player
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_Unlock(vlc_player_t *player);

/**
 * Add a listener callback
 *
 * Every registered callbacks need to be removed by the caller with
 * vlc_player_RemoveListener().
 *
 * @param player locked player instance
 * @param cbs pointer to a static vlc_player_cbs structure
 * @param cbs_data opaque pointer used by the callbacks
 * @return a valid listener id, or NULL in case of error
 */
VLC_API struct vlc_player_listener_id *
vlc_player_AddListener(vlc_player_t *player,
                       const struct vlc_player_cbs *cbs, void *cbs_data);

/**
 * Remove a listener callback
 *
 * @param player locked player instance
 * @param id listener id returned by vlc_player_AddListener()
 */
VLC_API void
vlc_player_RemoveListener(vlc_player_t *player,
                          struct vlc_player_listener_id *id);

VLC_API int
vlc_player_OpenItem(vlc_player_t *player, input_item_t *item);

VLC_API void
vlc_player_InvalidateNext(vlc_player_t *player);

VLC_API int
vlc_player_Start(vlc_player_t *player);

VLC_API void
vlc_player_Stop(vlc_player_t *player);

VLC_API void
vlc_player_Resume(vlc_player_t *player);

VLC_API void
vlc_player_Pause(vlc_player_t *player);

VLC_API bool
vlc_player_IsStarted(vlc_player_t *player);

VLC_API bool
vlc_player_IsPlaying(vlc_player_t *player);

VLC_API int
vlc_player_GetCapabilities(vlc_player_t *player);

static inline bool
vlc_player_CanSeek(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_INPUT_CAPABILITIES_SEEKABLE;
}

static inline bool
vlc_player_CanPause(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_INPUT_CAPABILITIES_PAUSEABLE;
}

static inline bool
vlc_player_CanChangeRate(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_INPUT_CAPABILITIES_CHANGE_RATE;
}

static inline bool
vlc_player_IsRewindable(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_INPUT_CAPABILITIES_REWINDABLE;
}

static inline bool
vlc_player_IsRecordable(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_INPUT_CAPABILITIES_RECORDABLE;
}

VLC_API size_t
vlc_player_GetEsCount(vlc_player_t *player, enum es_format_category_e i_cat);

VLC_API const struct vlc_player_es *
vlc_player_GetEsAt(vlc_player_t *player, enum es_format_category_e i_cat,
                   size_t index);

static inline size_t
vlc_player_GetVideoEsCount(vlc_player_t *player)
{
    return vlc_player_GetEsCount(player, VIDEO_ES);
}

static inline const struct vlc_player_es *
vlc_player_GetVideoEsAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetEsAt(player, VIDEO_ES, index);
}

static inline size_t
vlc_player_GetAudioEsCount(vlc_player_t *player)
{
    return vlc_player_GetEsCount(player, AUDIO_ES);
}

static inline const struct vlc_player_es *
vlc_player_GetAudioEsAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetEsAt(player, AUDIO_ES, index);
}

static inline size_t
vlc_player_GetSpuEsCount(vlc_player_t *player)
{
    return vlc_player_GetEsCount(player, SPU_ES);
}

static inline const struct vlc_player_es *
vlc_player_GetSpuEsAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetEsAt(player, SPU_ES, index);
}

VLC_API const struct vlc_player_es *
vlc_player_GetEs(vlc_player_t *player, vlc_es_id_t *id);

VLC_API void
vlc_player_SelectEs(vlc_player_t *player, vlc_es_id_t *id);

VLC_API void
vlc_player_UnselectEs(vlc_player_t *player, vlc_es_id_t *id);

VLC_API void
vlc_player_RestartEs(vlc_player_t *player, vlc_es_id_t *id);

VLC_API size_t
vlc_player_GetProgramCount(vlc_player_t *player);

VLC_API const struct vlc_player_program *
vlc_player_GetProgramAt(vlc_player_t *player, size_t index);

VLC_API const struct vlc_player_program *
vlc_player_GetProgram(vlc_player_t *player, int id);

VLC_API void
vlc_player_SelectProgram(vlc_player_t *player, int id);

VLC_API input_item_t *
vlc_player_GetCurrentItem(vlc_player_t *player);

VLC_API void
vlc_player_SetRenderer(vlc_player_t *player, vlc_renderer_item_t *renderer);

/** @} */
#endif
