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
#include <vlc_aout.h>

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
struct vlc_player_track
{
    vlc_es_id_t *id;
    const char *title;
    es_format_t fmt;
    bool selected;
};

/** Menu (VCD/DVD/BD) Navigation */
enum vlc_player_nav
{
    /** Activate the navigation item selected. */
    VLC_PLAYER_NAV_ACTIVATE,
    /** Use the up arrow to select a navigation item above. */
    VLC_PLAYER_NAV_UP,
    /** Use the down arrow to select a navigation item under. */
    VLC_PLAYER_NAV_DOWN,
    /** Use the left arrow to select a navigation item on the left */
    VLC_PLAYER_NAV_LEFT,
    /** Use the right arrow to select a navigation item on the right. */
    VLC_PLAYER_NAV_RIGHT,
    /** Activate the popup Menu (for BD). */
    VLC_PLAYER_NAV_POPUP,
    /** Activate disc Root Menu. */
    VLC_PLAYER_NAV_MENU,
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
struct vlc_player_media_provider
{
    /**
     * Called when the player requires a new media.
     *
     * @param player locked player instance
     * @param data opaque pointer set from vlc_player_New()
     * @return the next media to play, held by the callee with input_item_Hold()
     */
    input_item_t *(*get_next)(vlc_player_t *player, void *data);
};

enum vlc_player_list_action
{
    VLC_PLAYER_LIST_ADDED,
    VLC_PLAYER_LIST_REMOVED,
    VLC_PLAYER_LIST_UPDATED,
};

enum vlc_player_state
{
    VLC_PLAYER_STATE_IDLE,
    VLC_PLAYER_STATE_STARTED,
    VLC_PLAYER_STATE_STOPPED,
    VLC_PLAYER_STATE_PLAYING,
    VLC_PLAYER_STATE_PAUSED,
    VLC_PLAYER_STATE_ERROR,
};

enum vlc_player_seek_speed
{
    VLC_PLAYER_SEEK_PRECISE,
    VLC_PLAYER_SEEK_FAST,
};

enum vlc_player_seek_whence
{
    VLC_PLAYER_SEEK_ABSOLUTE,
    VLC_PLAYER_SEEK_RELATIVE,
};

#define VLC_PLAYER_CAP_SEEK (1<<0)
#define VLC_PLAYER_CAP_PAUSE (1<<1)
#define VLC_PLAYER_CAP_CHANGE_RATE (1<<2)
#define VLC_PLAYER_CAP_REWIND (1<<3)
#define VLC_PLAYER_CAP_RECORD (1<<4)

/**
 * Callbacks to get the state of the input.
 *
 * Can be registered with vlc_player_AddListener().
 *
 * All callbacks are called with the player locked (cf. vlc_player_Lock()).
 */
struct vlc_player_cbs
{
    /**
     * Called when a new media is played.
     *
     * @param player locked player instance
     * @param new_media new media currently played or NULL
     * @param data opaque pointer set from vlc_player_New()
     */
    void (*on_current_media_changed)(vlc_player_t *player,
                                     input_item_t *new_media, void *data);

    void (*on_state_changed)(vlc_player_t *player, enum vlc_player_state state,
                             void *data);

    void (*on_buffering_changed)(vlc_player_t *player, float new_buffering,
                                 void *data);

    void (*on_rate_changed)(vlc_player_t *player, float new_rate, void *data);

    void (*on_capabilities_changed)(vlc_player_t *player, int new_caps,
                                    void *data);

    void (*on_position_changed)(vlc_player_t *player, vlc_tick_t time,
                                float pos, void *data);

    void (*on_length_changed)(vlc_player_t *player, vlc_tick_t new_length,
                              void *data);

    void (*on_track_list_changed)(vlc_player_t *player,
                                  enum vlc_player_list_action action,
                                  const struct vlc_player_track *track,
                                  void *data);

    void (*on_track_selection_changed)(vlc_player_t *player,
                                       vlc_es_id_t *unselected_id,
                                       vlc_es_id_t *selected_id, void *data);

    void (*on_program_list_changed)(vlc_player_t *player,
                                    enum vlc_player_list_action action,
                                    const struct vlc_player_program *prgm,
                                    void *data);

    void (*on_program_selection_changed)(vlc_player_t *player,
                                         int unselected_id, int selected_id,
                                         void *data);

    void (*on_title_array_changed)(vlc_player_t *player,
                                   const input_title_t * const *array,
                                   size_t count, void *data);

    void (*on_title_selection_changed)(vlc_player_t *player,
                                       const input_title_t *new_title,
                                       size_t new_idx, void *data);

    void (*on_chapter_selection_changed)(vlc_player_t *player,
                                         const input_title_t *title,
                                         size_t title_idx,
                                         const seekpoint_t *chapter,
                                         size_t chapter_idx, void *data);

    void (*on_audio_delay_changed)(vlc_player_t *player, vlc_tick_t new_delay,
                                   void *data);

    void (*on_subtitle_delay_changed)(vlc_player_t *player, vlc_tick_t new_delay,
                                      void *data);

    void (*on_record_changed)(vlc_player_t *player, bool recording, void *data);

    void (*on_signal_changed)(vlc_player_t *player,
                              float quality, float strength, void *data);

    void (*on_stats_changed)(vlc_player_t *player,
                             const struct input_stats_t *stats, void *data);

    void (*on_vout_list_changed)(vlc_player_t *player,
                                 enum vlc_player_list_action action,
                                 vout_thread_t *vout, void *data);

    void (*on_media_meta_changed)(vlc_player_t *player, input_item_t *item,
                                 void *data);

    void (*on_media_epg_changed)(vlc_player_t *player, input_item_t *item,
                                void *data);

    void (*on_subitems_changed)(vlc_player_t *player,
                                input_item_node_t *subitems, void *data);

    void (*on_aout_volume_changed)(vlc_player_t *player, audio_output_t *aout,
                                   float volume, void *data);
    void (*on_aout_mute_changed)(vlc_player_t *player, audio_output_t *aout,
                                 bool muted, void *data);
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
               const struct vlc_player_media_provider *owner_cbs,
               void *owner_cbs_data);

/**
 * Delete the player.
 *
 * This function stop any playback previously started and wait for their
 * termination.
 *
 * @warning Blocking function, don't call it from an UI thread
 *
 * @param player unlocked player instance created by vlc_player_New()
 */
VLC_API void
vlc_player_Delete(vlc_player_t *player);

/**
 * Lock the player.
 *
 * All player functions (except vlc_player_Delete()) need to be called while
 * the player lock is held.
 *
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

VLC_API void
vlc_player_CondWait(vlc_player_t *player, vlc_cond_t *cond);

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

/**
 * Set the current media for playback.
 *
 * This function replaces the current and next medias (and stop the playback of
 * these medias if needed). The playback need to be started with
 * vlc_player_Start().
 *
 * @param player locked player instance
 * @param media new media to player
 * @return VLC_SUCCESS or a VLC error code
 */
VLC_API int
vlc_player_SetCurrentMedia(vlc_player_t *player, input_item_t *media);

/**
 * Get the current played media.
 *
 * @param player locked player instance
 * @return a valid media or NULL (if not media are set)
 */
VLC_API input_item_t *
vlc_player_GetCurrentMedia(vlc_player_t *player);

static inline input_item_t *
vlc_player_HoldCurrentMedia(vlc_player_t *player)
{
    input_item_t *item = vlc_player_GetCurrentMedia(player);
    return item ? input_item_Hold(item) : NULL;
}

/**
 * Invalidate the next media.
 *
 * This function can be used to invalidate the media returned by the
 * vlc_player_media_provider.get_next callback. This can be used when the next
 * item from a playlist was changed by the user.
 *
 * Calling this function will trigger the
 * vlc_player_media_provider.get_next callback from the playback thread.
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_InvalidateNextMedia(vlc_player_t *player);

/**
 * Start the playback of the current media.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @return VLC_SUCCESS or a VLC error code
 */
VLC_API int
vlc_player_Start(vlc_player_t *player);

/**
 * Stop the playback of the current media .
 *
 * This function will wait for the termination of the playback.
 *
 * @warning The behaviour is undefined if there is no current media.
 * @warning Blocking function, don't call it from an UI thread
 *
 * @param player locked player instance
 * @return VLC_SUCCESS or a VLC error code
 */
VLC_API void
vlc_player_Stop(vlc_player_t *player);

/**
 * Stop the playback of the current media without waiting.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @return VLC_SUCCESS or a VLC error code
 */
VLC_API void
vlc_player_RequestStop(vlc_player_t *player);

/**
 * Pause the playback.
 *
 * @warning The behaviour is undefined if the player is not started.
 * @warning The behaviour is undefined if vlc_player_CanPause() is false.
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_Pause(vlc_player_t *player);

/**
 * Resume the playback from a pause.
 *
 * @warning The behaviour is undefined if the player is not started.
 * @warning The behaviour is undefined if vlc_player_CanPause() is false.
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_Resume(vlc_player_t *player);

VLC_API enum vlc_player_state
vlc_player_GetState(vlc_player_t *player);

/**
 * Get the started state.
 *
 * @param player locked player instance
 * @return true if the player is started (vlc_player_Start() succeeded and
 * vlc_player_cbs.on_playback_event didn't send a stopped/dead event).
 */
static inline bool
vlc_player_IsStarted(vlc_player_t *player)
{
    switch (vlc_player_GetState(player))
    {
        case VLC_PLAYER_STATE_STARTED:
        case VLC_PLAYER_STATE_PLAYING:
        case VLC_PLAYER_STATE_PAUSED:
            return true;
        default:
            return false;
    }
}

/**
 * Get the paused state.
 *
 * Since the vlc_player_Pause() / vlc_player_Resume() are asynchronous, this
 * function won't reflect the paused state immediately. Wait for the
 * INPUT_EVENT_STATE event to be notified.
 *
 * @param player locked player instance
 * @return true if the player is paused
 */
static inline bool
vlc_player_IsPaused(vlc_player_t *player)
{
    return vlc_player_GetState(player) == VLC_PLAYER_STATE_PAUSED;
}

static inline void
vlc_player_TogglePause(vlc_player_t *player)
{
    if (vlc_player_IsStarted(player))
    {
        if (vlc_player_IsPaused(player))
            vlc_player_Resume(player);
        else
            vlc_player_Pause(player);
    }
}

/**
 * Get the player capabilities
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @return the player capabilities, a bitwise mask of
 * VLC_PLAYER_CAP_SEEK, VLC_PLAYER_CAP_PAUSE, VLC_PLAYER_CAP_CHANGE_RATE,
 * VLC_PLAYER_CAP_REWIND, VLC_PLAYER_CAP_RECORD.
 */
VLC_API int
vlc_player_GetCapabilities(vlc_player_t *player);

/**
 * Get the seek capability (Helper).
 */
static inline bool
vlc_player_CanSeek(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_SEEK;
}

/**
 * Get the pause capability (Helper).
 */
static inline bool
vlc_player_CanPause(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_PAUSE;
}

/**
 * Get the change-rate capability (Helper).
 */
static inline bool
vlc_player_CanChangeRate(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_CHANGE_RATE;
}

/**
 * Get the rewindable capability (Helper).
 */
static inline bool
vlc_player_canRewind(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_REWIND;
}

/**
 * Get the recordable capability (Helper).
 */
static inline bool
vlc_player_canRecord(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_RECORD;
}

VLC_API float
vlc_player_GetRate(vlc_player_t *player);

VLC_API void
vlc_player_ChangeRate(vlc_player_t *player, float rate);

VLC_API void
vlc_player_IncrementRate(vlc_player_t *player);

VLC_API void
vlc_player_DecrementRate(vlc_player_t *player);

VLC_API vlc_tick_t
vlc_player_GetLength(vlc_player_t *player);

VLC_API vlc_tick_t
vlc_player_GetTime(vlc_player_t *player);

VLC_API float
vlc_player_GetPosition(vlc_player_t *player);

VLC_API void
vlc_player_SeekByPos(vlc_player_t *player, float position,
                     enum vlc_player_seek_speed speed,
                     enum vlc_player_seek_whence whence);

VLC_API void
vlc_player_SeekByTime(vlc_player_t *player, vlc_tick_t time,
                      enum vlc_player_seek_speed speed,
                      enum vlc_player_seek_whence whence);

static inline void
vlc_player_SetPosition(vlc_player_t *player, float position)
{
    vlc_player_SeekByPos(player, position, VLC_PLAYER_SEEK_PRECISE,
                         VLC_PLAYER_SEEK_ABSOLUTE);
}

static inline void
vlc_player_SetPositionFast(vlc_player_t *player, float position)
{
    vlc_player_SeekByPos(player, position, VLC_PLAYER_SEEK_FAST,
                         VLC_PLAYER_SEEK_ABSOLUTE);
}

static inline void
vlc_player_JumpPos(vlc_player_t *player, float jumppos)
{
    /* No fask seek for jumps. Indeed, jumps can seek to the current position
     * if not precise enough or if the jump value is too small. */
    vlc_player_SeekByPos(player, jumppos, VLC_PLAYER_SEEK_PRECISE,
                         VLC_PLAYER_SEEK_RELATIVE);
}

static inline void
vlc_player_SetTime(vlc_player_t *player, vlc_tick_t time)
{
    vlc_player_SeekByTime(player, time, VLC_PLAYER_SEEK_PRECISE,
                          VLC_PLAYER_SEEK_ABSOLUTE);
}

static inline void
vlc_player_SetTimeFast(vlc_player_t *player, vlc_tick_t time)
{
    vlc_player_SeekByTime(player, time, VLC_PLAYER_SEEK_FAST,
                          VLC_PLAYER_SEEK_ABSOLUTE);
}

static inline void
vlc_player_JumpTime(vlc_player_t *player, vlc_tick_t jumptime)
{
    /* No fask seek for jumps. Indeed, jumps can seek to the current position
     * if not precise enough or if the jump value is too small. */
    vlc_player_SeekByTime(player, jumptime, VLC_PLAYER_SEEK_PRECISE,
                          VLC_PLAYER_SEEK_RELATIVE);
}

/**
 * Get the number of tracks for an ES category.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @param cat VIDEO_ES, AUDIO_ES or SPU_ES
 * @return number of tracks (or 0)
 */
VLC_API size_t
vlc_player_GetTrackCount(vlc_player_t *player, enum es_format_category_e cat);

/**
 * Get the track for an ES caterogy at a specific index.
 *
 * @warning The behaviour is undefined if there is no current media.
 * @warning The behaviour is undefined if the index is not valid.
 *
 * @param player locked player instance
 * @param cat VIDEO_ES, AUDIO_ES or SPU_ES
 * @param index valid index in the range [0; count[
 * @return a valid player track (can't be NULL)
 */
VLC_API const struct vlc_player_track *
vlc_player_GetTrackAt(vlc_player_t *player, enum es_format_category_e cat,
                      size_t index);

/**
 * Get the video track count (Helper).
 */
static inline size_t
vlc_player_GetVideoTrackCount(vlc_player_t *player)
{
    return vlc_player_GetTrackCount(player, VIDEO_ES);
}

/**
 * Get the video track at a specific index (Helper).
 */
static inline const struct vlc_player_track *
vlc_player_GetVideoTrackAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetTrackAt(player, VIDEO_ES, index);
}

/**
 * Get the audio track count (Helper).
 */
static inline size_t
vlc_player_GetAudioTrackCount(vlc_player_t *player)
{
    return vlc_player_GetTrackCount(player, AUDIO_ES);
}

/**
 * Get the audio track at a specific index (Helper).
 */
static inline const struct vlc_player_track *
vlc_player_GetAudioTrackAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetTrackAt(player, AUDIO_ES, index);
}

/**
 * Get the spu track count (Helper).
 */
static inline size_t
vlc_player_GetSpuTrackCount(vlc_player_t *player)
{
    return vlc_player_GetTrackCount(player, SPU_ES);
}

/**
 * Get the spu track at a specific index (Helper).
 */
static inline const struct vlc_player_track *
vlc_player_GetSpuTrackAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetTrackAt(player, SPU_ES, index);
}

/**
 * Get a track from an ES identifier.
 *
 * The only way to save a player track when the player is not locked anymore
 * (from the event thread to the UI main thread for example) is to hold the ES
 * ID with vlc_es_id_Hold()). Then, the user can call this function in order to
 * retrieve the up to date track information from the previously held ES ID.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @param id an ES ID (retrieved from the INPUT_EVENT_ES event or from
 * vlc_player_GetTrackAt())
 * @return a valid player track or NULL (if the track was terminated by the
 * playback thread)
 */
VLC_API const struct vlc_player_track *
vlc_player_GetTrack(vlc_player_t *player, vlc_es_id_t *id);

/**
 * Select a track from an ES identifier.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @param id an ES ID (retrieved from the INPUT_EVENT_ES event or from
 * vlc_player_GetTrackAt())
 */
VLC_API void
vlc_player_SelectTrack(vlc_player_t *player, vlc_es_id_t *id);

/**
 * Unselect a track from an ES identifier.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @param id an ES ID (retrieved from the INPUT_EVENT_ES event or from
 * vlc_player_GetTrackAt())
 */
VLC_API void
vlc_player_UnselectTrack(vlc_player_t *player, vlc_es_id_t *id);

static inline void
vlc_player_UnselectTrackCategory(vlc_player_t *player,
                                 enum es_format_category_e cat)
{
    size_t count = vlc_player_GetTrackCount(player, cat);
    for (size_t i = 0; i < count; ++i)
    {
        const struct vlc_player_track *track =
            vlc_player_GetTrackAt(player, cat, i);
        assert(track);
        if (track->selected)
            vlc_player_UnselectTrack(player, track->id);
    }
}

/**
 * Restart a track from an ES identifier.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @param id an ES ID (retrieved from the INPUT_EVENT_ES event or from
 * vlc_player_GetTrackAt())
 */
VLC_API void
vlc_player_RestartTrack(vlc_player_t *player, vlc_es_id_t *id);

/**
 * Select the default track for an ES category.
 *
 * Tracks for this category will be automatically chosen according to the
 * language for all future played media.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @param cat VIDEO_ES, AUDIO_ES or SPU_ES
 * @param lang language (TODO: define it) or NULL to reset the default state
 */
VLC_API void
vlc_player_SelectDefaultTrack(vlc_player_t *player,
                              enum es_format_category_e cat, const char *lang);

/**
 * Select the default video track (Helper).
 */
static inline void
vlc_player_SelectDefaultVideoTrack(vlc_player_t *player, const char *lang)
{
    vlc_player_SelectDefaultTrack(player, VIDEO_ES, lang);
}

/**
 * Select the default audio track (Helper).
 */
static inline void
vlc_player_SelectDefaultAudioTrack(vlc_player_t *player, const char *lang)
{
    vlc_player_SelectDefaultTrack(player, AUDIO_ES, lang);
}

/**
 * Select the default spu track (Helper).
 */
static inline void
vlc_player_SelectDefaultSpuTrack(vlc_player_t *player, const char *lang)
{
    vlc_player_SelectDefaultTrack(player, SPU_ES, lang);
}

/**
 * Get the number of programs
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @return number of programs (or 0)
 */
VLC_API size_t
vlc_player_GetProgramCount(vlc_player_t *player);

/**
 * Get the program at a specific index.
 *
 * @warning The behaviour is undefined if there is no current media.
 * @warning The behaviour is undefined if the index is not valid.
 *
 * @param player locked player instance
 * @param index valid index in the range [0; count[
 * @return a valid player program (can't be NULL)
 */
VLC_API const struct vlc_player_program *
vlc_player_GetProgramAt(vlc_player_t *player, size_t index);

/**
 * Get a program from an ES identifier.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @param id an ES program ID (retrieved from the INPUT_EVENT_PROGRAM event or
 * from vlc_player_GetProgramAt())
 * @return a valid program or NULL (if the program was terminated by the
 * playback thread)
 */
VLC_API const struct vlc_player_program *
vlc_player_GetProgram(vlc_player_t *player, int id);

/**
 * Select a program from an ES program identifier.
 *
 * @warning The behaviour is undefined if there is no current media.
 *
 * @param player locked player instance
 * @param id an ES program ID (retrieved from the INPUT_EVENT_PROGRAM event or
 * from vlc_player_GetProgramAt())
 */
VLC_API void
vlc_player_SelectProgram(vlc_player_t *player, int id);

VLC_API const input_title_t * const *
vlc_player_GetTitleArray(vlc_player_t *player, size_t *count);

VLC_API ssize_t
vlc_player_GetSelectedTitleIdx(vlc_player_t *player);

static inline const input_title_t *
vlc_player_GetSelectedTitle(vlc_player_t *player)
{
    size_t u;
    const input_title_t * const *array = vlc_player_GetTitleArray(player, &u);
    if (!array)
        return NULL;
    return array[vlc_player_GetSelectedTitleIdx(player)];
}

VLC_API void
vlc_player_SelectTitleIdx(vlc_player_t *player, size_t index);

VLC_API void
vlc_player_SelectNextTitle(vlc_player_t *player);

VLC_API void
vlc_player_SelectPrevTitle(vlc_player_t *player);

VLC_API ssize_t
vlc_player_GetSelectedChapterIdx(vlc_player_t *player);

static inline const seekpoint_t *
vlc_player_GetSelectedChapter(vlc_player_t *player)
{
    const input_title_t *title = vlc_player_GetSelectedTitle(player);
    if (!title || !title->seekpoint)
        return NULL;
    ssize_t chapter_idx = vlc_player_GetSelectedChapterIdx(player);
    return chapter_idx >= 0 ? title->seekpoint[chapter_idx] : NULL;
}

VLC_API void
vlc_player_SelectChapterIdx(vlc_player_t *player, size_t index);

VLC_API void
vlc_player_SelectNextChapter(vlc_player_t *player);

VLC_API void
vlc_player_SelectPrevChapter(vlc_player_t *player);

VLC_API int
vlc_player_AddAssociatedMedia(vlc_player_t *player,
                              enum es_format_category_e cat, const char *uri,
                              bool select, bool notify, bool check_ext);

/**
 * Set the renderer
 *
 * Valid for the current media and all future ones.
 *
 * @param player locked player instance
 * @param renderer a valid renderer item or NULL (to disable it)
 */
VLC_API void
vlc_player_SetRenderer(vlc_player_t *player, vlc_renderer_item_t *renderer);

VLC_API void
vlc_player_Navigate(vlc_player_t *player, enum vlc_player_nav nav);

VLC_API bool
vlc_player_IsRecording(vlc_player_t *player);

VLC_API void
vlc_player_SetAudioDelay(vlc_player_t *player, vlc_tick_t delay,
                         bool absolute);

VLC_API vlc_tick_t
vlc_player_GetAudioDelay(vlc_player_t *player);

VLC_API void
vlc_player_SetSubtitleDelay(vlc_player_t *player, vlc_tick_t delay,
                            bool absolute);

VLC_API vlc_tick_t
vlc_player_GetSubtitleDelay(vlc_player_t *player);

VLC_API int
vlc_player_GetSignal(vlc_player_t *player, float *quality, float *strength);

VLC_API int
vlc_player_GetStats(vlc_player_t *player, struct input_stats_t *stats);

VLC_API size_t
vlc_player_GetVouts(vlc_player_t *player, vout_thread_t ***vouts);

VLC_API audio_output_t *
vlc_player_GetAout(vlc_player_t *player);

static inline float
vlc_player_aout_GetVolume(vlc_player_t *player)
{
    audio_output_t *aout = vlc_player_GetAout(player);
    if (!aout)
        return -1.f;
    float ret = aout_VolumeGet(aout);
    vlc_object_release(aout);
    return ret;
}

static inline int
vlc_player_aout_SetVolume(vlc_player_t *player, float volume)
{
    audio_output_t *aout = vlc_player_GetAout(player);
    if (!aout)
        return -1;
    int ret = aout_VolumeSet(aout, volume);
    vlc_object_release(aout);
    return ret;

}

static inline int
vlc_player_aout_IncrementVolume(vlc_player_t *player, float volume,
                                float *result)
{
    audio_output_t *aout = vlc_player_GetAout(player);
    if (!aout)
        return -1;
    int ret = aout_VolumeUpdate(aout, volume, result);
    vlc_object_release(aout);
    return ret;
}

static inline int
vlc_player_aout_DecrementVolume(vlc_player_t *player, float volume,
                                float *result)
{
    return vlc_player_aout_IncrementVolume(player, -volume, result);
}

static inline int
vlc_player_aout_IsMuted(vlc_player_t *player)
{
    audio_output_t *aout = vlc_player_GetAout(player);
    if (!aout)
        return -1;
    int ret = aout_MuteGet(aout);
    vlc_object_release(aout);
    return ret;
}

static inline int
vlc_player_aout_Mute(vlc_player_t *player, bool mute)
{
    audio_output_t *aout = vlc_player_GetAout(player);
    if (!aout)
        return -1;
    int ret = aout_MuteSet (aout, mute);
    vlc_object_release(aout);
    return ret;
}

VLC_API int
vlc_player_aout_EnableFilter(vlc_player_t *player, const char *name, bool add);

VLC_API bool
vlc_player_vout_IsFullscreen(vlc_player_t *player);

/**
 * This will have an effect on all currrent and futures vouts.
 */
VLC_API void
vlc_player_vout_SetFullscreen(vlc_player_t *player, bool enabled);

static inline void
vlc_player_vout_ToggleFullscreen(vlc_player_t *player)
{
    vlc_player_vout_SetFullscreen(player,
                                  !vlc_player_vout_IsFullscreen(player));
}

VLC_API struct vlc_player_track *
vlc_player_track_Dup(const struct vlc_player_track *track);

VLC_API void
vlc_player_track_Delete(struct vlc_player_track *track);

VLC_API struct vlc_player_program *
vlc_player_program_Dup(const struct vlc_player_program *prgm);

VLC_API void
vlc_player_program_Delete(struct vlc_player_program *prgm);

/** @} */
#endif
