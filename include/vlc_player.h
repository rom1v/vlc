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

struct vlc_player_t
{
    struct vlc_common_members obj;
};

struct vlc_player_listener_id;

struct vlc_player_program
{
    int     id;
    bool    selected;
    bool    scrambled;
    char *  title;
};

struct vlc_player_es
{
    es_format_t fmt;
    bool        selected;
    char *      title;
};

struct vlc_player_teletext
{
    int     id;
    bool    selected;
    char *  title;
};

struct vlc_player_cbs
{
    void (*on_new_item_played)(vlc_player_t *player, void *data,
                               input_item_t *new_item);
    void (*on_input_event)(vlc_player_t *player, void *data,
                           const struct vlc_input_event *event);
};

VLC_API vlc_player_t *
vlc_player_New(vlc_object_t *parent, struct vlc_player_cbs *owner_cbs,
               void *owner_cbs_data);

static inline void
vlc_player_Release(vlc_player_t *player)
{
    vlc_object_release(player);
}

VLC_API void
vlc_player_Lock(vlc_player_t *player);

VLC_API void
vlc_player_Unlock(vlc_player_t *player);

VLC_API struct vlc_player_listener_id *
vlc_player_AddListener(vlc_player_t *player,
                       const struct vlc_player_cbs *cbs, void *cbs_data);

VLC_API void
vlc_player_RemoveListener(vlc_player_t *player,
                          struct vlc_player_listener_id *id);

VLC_API int
vlc_player_OpenItem(vlc_player_t *player, input_item_t *item);

VLC_API int
vlc_player_SetNextItem(vlc_player_t *player, input_item_t *item);

VLC_API int
vlc_player_Start(vlc_player_t *player);

VLC_API void
vlc_player_Stop(vlc_player_t *player);

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

VLC_API void
vlc_player_es_FreeArray(struct vlc_player_es *eses, size_t count);

static inline void
vlc_player_es_Free(struct vlc_player_es *es)
{
    vlc_player_es_FreeArray(es, 1);
}

VLC_API ssize_t
vlc_player_GetEsesArray(vlc_player_t *player, enum es_format_category_e cat,
                        int id, bool selected_only,
                        struct vlc_player_es **eses);

static inline ssize_t
vlc_player_GetVideoEsesArray(vlc_player_t *player, struct vlc_player_es **eses)
{
    return vlc_player_GetEsesArray(player, VIDEO_ES, -1, false, eses);
}
static inline ssize_t
vlc_player_GetSelectedVideoEsesArray(vlc_player_t *player,
                                     struct vlc_player_es **eses)
{
    return vlc_player_GetEsesArray(player, VIDEO_ES, -1, true, eses);
}
static inline struct vlc_player_es *
vlc_player_GetVideoEs(vlc_player_t *player, int id)
{
    assert(id != -1);
    struct vlc_player_es *es;
    ssize_t ret = vlc_player_GetEsesArray(player, VIDEO_ES, id, false, &es);
    if (ret < 0)
        return NULL;
    assert(ret == 1);
    return es;
}

static inline ssize_t
vlc_player_GetAudioEsesArray(vlc_player_t *player, struct vlc_player_es **eses)
{
    return vlc_player_GetEsesArray(player, AUDIO_ES, -1, false, eses);
}
static inline ssize_t
vlc_player_GetSelectedAudioEsesArray(vlc_player_t *player,
                                     struct vlc_player_es **eses)
{
    return vlc_player_GetEsesArray(player, AUDIO_ES, -1, true, eses);
}
static inline struct vlc_player_es *
vlc_player_GetAudioEs(vlc_player_t *player, int id)
{
    assert(id != -1);
    struct vlc_player_es *es;
    ssize_t ret = vlc_player_GetEsesArray(player, AUDIO_ES, id, false, &es);
    if (ret < 0)
        return NULL;
    assert(ret == 1);
    return es;
}

static inline ssize_t
vlc_player_GetSpuEsesArray(vlc_player_t *player, struct vlc_player_es **eses)
{
    return vlc_player_GetEsesArray(player, SPU_ES, -1, false, eses);
}
static inline ssize_t
vlc_player_GetSelectedSpuEsesArray(vlc_player_t *player,
                                   struct vlc_player_es **eses)
{
    return vlc_player_GetEsesArray(player, SPU_ES, -1, true, eses);
}
static inline struct vlc_player_es *
vlc_player_GetSpuEs(vlc_player_t *player, int id)
{
    assert(id >= 0);
    struct vlc_player_es *es;
    ssize_t ret = vlc_player_GetEsesArray(player, SPU_ES, id, false, &es);
    if (ret <= 0)
        return NULL;
    assert(ret == 1);
    return es;
}

VLC_API void
vlc_player_SetEs(vlc_player_t *player, enum es_format_category_e cat, int id);

void
vlc_player_program_FreeArray(struct vlc_player_program *prgms, size_t count);

VLC_API ssize_t
vlc_player_GetProgramsArray(vlc_player_t *player, int id, bool selected_only,
                            struct vlc_player_program **prgms_out);

static inline struct vlc_player_program *
vlc_player_GetSelectedProgram(vlc_player_t *player)
{
    struct vlc_player_program *prgm;
    ssize_t ret = vlc_player_GetProgramsArray(player, -1, true, &prgm);
    if (ret <= 0)
        return NULL;
    assert(ret == 1);
    return prgm;
}
static inline struct vlc_player_program *
vlc_player_GetProgram(vlc_player_t *player, int id)
{
    assert(id >= 0);
    struct vlc_player_program *prgm;
    ssize_t ret = vlc_player_GetProgramsArray(player, id, false, &prgm);
    if (ret <= 0)
        return NULL;
    assert(ret == 1);
    return prgm;
}

VLC_API void
vlc_player_SetProgram(vlc_player_t *player, int id);

VLC_API void
vlc_player_teletext_FreeArray(struct vlc_player_teletext *tts, size_t count);

VLC_API ssize_t
vlc_player_GetTeletextsArray(vlc_player_t *player, int id, bool selected_only,
                             struct vlc_player_teletext **tts_out);

VLC_API input_item_t *
vlc_player_GetCurrentItem(vlc_player_t *player);

VLC_API void
vlc_player_SetRenderer(vlc_player_t *player, vlc_renderer_item_t *renderer);

VLC_API void
vlc_player_ResetAout(vlc_player_t *player);

/** @} */
#endif
