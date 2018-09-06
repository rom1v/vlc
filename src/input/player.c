/*****************************************************************************
 * player.c: Player interface
 *****************************************************************************
 * Copyright © 2018 VLC authors and VideoLAN
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
#include <vlc_player.h>
#include <vlc_player.h>
#include <vlc_aout.h>
#include <vlc_renderer_discovery.h>
#include <vlc_list.h>
#include <vlc_vector.h>

#include "libvlc.h"
#include "input_internal.h"
#include "resource.h"
#include "../audio_output/aout_internal.h"

#define GAPLESS 0 /* TODO */

struct vlc_player_listener_id
{
    const struct vlc_player_cbs * cbs;
    void * cbs_data;
    struct vlc_list node;
};

struct vlc_player_program_priv
{
    struct vlc_player_program p;
    char *title;
};
typedef struct VLC_VECTOR(struct vlc_player_program_priv *)
    vlc_player_program_vector;

typedef struct VLC_VECTOR(struct vlc_player_track *)
    vlc_player_track_vector;

struct vlc_player_input
{
    input_thread_t *thread;
    vlc_player_t *player;
    bool started;
    bool stop_async;

    input_state_e state;
    float rate;
    int capabilities;
    vlc_tick_t length;

    vlc_tick_t position_ms;
    float position_percent;
    vlc_tick_t position_date;

    bool recording;

    float signal_quality;
    float signal_strength;
    float cache;

    struct input_stats_t stats;

    bool discontinuity;

    vlc_tick_t audio_delay;
    vlc_tick_t subtitle_delay;

    vlc_player_program_vector program_vector;
    vlc_player_track_vector video_track_vector;
    vlc_player_track_vector audio_track_vector;
    vlc_player_track_vector spu_track_vector;

    struct vlc_list node;
};

struct vlc_player_t
{
    struct vlc_common_members obj;
    vlc_mutex_t lock;

    const struct vlc_player_owner_cbs *owner_cbs;
    void *owner_cbs_data;

    struct vlc_list listeners;

    input_resource_t *resource;
    vlc_renderer_item_t *renderer;

    input_item_t *media;
    struct vlc_player_input *input;

#if GAPLESS
    input_item_t *next_media;
    struct vlc_player_input *next_input;
#endif

    struct
    {
        bool running;
        vlc_thread_t thread;
        vlc_cond_t cond;
        struct vlc_list inputs;
        size_t wait_count;
    } destructor;
};

#define vlc_player_SendEvent(player, event, ...) do { \
    struct vlc_player_listener_id *listener; \
    vlc_list_foreach(listener, &player->listeners, node) \
    { \
        if (listener->cbs->event) \
            listener->cbs->event(player, ##__VA_ARGS__, listener->cbs_data); \
    } \
} while(0)

#if GAPLESS
#define vlc_player_foreach_inputs(it) \
    for (struct vlc_player_input *it = player->input; \
         it != NULL; \
         it = (it == player->input ? player->next_input : NULL))
#else
#define vlc_player_foreach_inputs(it) \
    for (struct vlc_player_input *it = player->input; it != NULL; it = NULL)
#endif

static void
input_thread_events(input_thread_t *, const struct vlc_input_event *, void *);

static inline void
vlc_player_assert_locked(vlc_player_t *player)
{
    assert(player);
    vlc_assert_locked(&player->lock);
}

static inline struct vlc_player_input *
vlc_player_get_input_locked(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    assert(player->input);
    return player->input;
}

static struct vlc_player_input *
vlc_player_input_New(vlc_player_t *player, input_item_t *item)
{
    struct vlc_player_input *input = malloc(sizeof(*input));
    if (!input)
        return NULL;

    input->player = player;
    input->started = false;
    input->stop_async = false;

    input->state = INIT_S;
    input->rate = 1.f;
    input->capabilities = 0;
    input->length = input->position_ms =
    input->position_date = VLC_TICK_INVALID;
    input->position_percent = 0.f;

    input->recording = false;

    input->cache = 0.f;
    input->signal_quality = input->signal_strength = -1.f;

    memset(&input->stats, 0, sizeof(input->stats));

    input->discontinuity = false;

    input->audio_delay = input->subtitle_delay = 0;

    vlc_vector_init(&input->program_vector);
    vlc_vector_init(&input->video_track_vector);
    vlc_vector_init(&input->audio_track_vector);
    vlc_vector_init(&input->spu_track_vector);

    input->thread = input_Create(player, input_thread_events, input, item,
                                 NULL, player->resource, player->renderer);
    if (!input->thread)
    {
        free(input);
        return NULL;
    }
    return input;
}

static void
vlc_player_input_Delete(struct vlc_player_input *input)
{
    input_Close(input->thread);

    assert(input->program_vector.size == 0);
    assert(input->video_track_vector.size == 0);
    assert(input->audio_track_vector.size == 0);
    assert(input->spu_track_vector.size == 0);

    vlc_vector_clear(&input->program_vector);
    vlc_vector_clear(&input->video_track_vector);
    vlc_vector_clear(&input->audio_track_vector);
    vlc_vector_clear(&input->spu_track_vector);

    free(input);
}

static int
vlc_player_input_Start(struct vlc_player_input *input)
{
    int ret = input_Start(input->thread);
    if (ret != VLC_SUCCESS)
        return ret;
    input->started = true;
    return ret;
}

static void
vlc_player_input_StopAndClose(struct vlc_player_input *input, bool wait)
{
    if (input->started)
    {
        input->started = false;
        input_Stop(input->thread);
        if (!wait)
        {
            input->stop_async = true;
            input->player->destructor.wait_count++;
            /* This input will be cleaned when we receive the INPUT_EVENT_DEAD
             * event */
            return;
        }
    }
    vlc_player_input_Delete(input);
}

static void *
destructor_thread(void *data)
{
    vlc_player_t *player = data;
    struct vlc_player_input *input;

    vlc_mutex_lock(&player->lock);
    while (player->destructor.running || player->destructor.wait_count > 0)
    {
        while (player->destructor.running
            && vlc_list_is_empty(&player->destructor.inputs))
            vlc_cond_wait(&player->destructor.cond, &player->lock);

        vlc_list_foreach(input, &player->destructor.inputs, node)
        {
            vlc_player_input_Delete(input);
            vlc_list_remove(&input->node);
            player->destructor.wait_count--;
        }
    }
    vlc_mutex_unlock(&player->lock);
    return NULL;
}

static void
vlc_player_SendNewPlaybackEvent(vlc_player_t *player, input_item_t *new_item)
{
    vlc_player_assert_locked(player);

    if (player->owner_cbs->on_current_media_changed)
        player->owner_cbs->on_current_media_changed(player, new_item,
                                                    player->owner_cbs_data);
}

static input_item_t *
vlc_player_GetNextItem(vlc_player_t *player)
{
    vlc_player_assert_locked(player);

    return player->owner_cbs->get_next_media ?
           player->owner_cbs->get_next_media(player, player->owner_cbs_data) :
           NULL;
}

static int
vlc_player_OpenNextItem(vlc_player_t *player)
{
    assert(player->input == NULL);

    input_item_t *next_media = vlc_player_GetNextItem(player);
    if (!next_media)
        return VLC_EGENERIC;

    if (player->media)
        input_item_Release(player->media);
    player->media = next_media;

    player->input = vlc_player_input_New(player, player->media);

    return player->input ? VLC_SUCCESS : VLC_EGENERIC;
}

static struct vlc_player_program_priv *
vlc_player_program_vector_FindById(vlc_player_program_vector *vec, int id,
                                   size_t *idx)
{
    for (size_t i = 0; i < vec->size; ++i)
    {
        struct vlc_player_program_priv *prgm = vec->data[i];
        if (prgm->p.id == id)
        {
            if (idx)
                *idx = i;
            return prgm;
        }
    }
    return NULL;
}

size_t
vlc_player_GetProgramCount(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    return input->program_vector.size;
}

const struct vlc_player_program *
vlc_player_GetProgramAt(vlc_player_t *player, size_t index)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    assert(index < input->program_vector.size);
    return &input->program_vector.data[index]->p;
}

const struct vlc_player_program *
vlc_player_GetProgram(vlc_player_t *player, int id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    struct vlc_player_program_priv *prgm =
        vlc_player_program_vector_FindById(&input->program_vector, id, NULL);
    return prgm ? &prgm->p : NULL;
}

void
vlc_player_SelectProgram(vlc_player_t *player, int id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_PROGRAM,
                            &(vlc_value_t) { .i_int = id });
}

static struct vlc_player_program_priv *
vlc_player_program_Create(int id, const char *title)
{
    struct vlc_player_program_priv *prgm = malloc(sizeof(*prgm));
    if (!prgm)
        return NULL;
    prgm->title = strdup(title);
    if (!prgm->title)
    {
        free(prgm);
        return NULL;
    }
    prgm->p.id = id;
    prgm->p.title = prgm->title;
    prgm->p.selected = prgm->p.scrambled = false;

    return prgm;
}

static void
vlc_player_program_Destroy(struct vlc_player_program_priv *prgm)
{
    free(prgm->title);
    free(prgm);
}

static int
vlc_player_input_HandleProgramEvent(struct vlc_player_input *input,
                                    const struct vlc_input_event_program *ev)
{
    struct vlc_player_program_priv *prgm;
    vlc_player_program_vector *vec = &input->program_vector;

    switch (ev->action)
    {
        case VLC_INPUT_PROGRAM_ADDED:
            assert(ev->title);
            prgm = vlc_player_program_Create(ev->id, ev->title);
            if (!prgm)
                return VLC_ENOMEM;

            if (vlc_vector_push(vec, prgm))
                return VLC_SUCCESS;
            vlc_player_program_Destroy(prgm);
            return VLC_ENOMEM;
        case VLC_INPUT_PROGRAM_DELETED:
        {
            size_t idx;
            prgm = vlc_player_program_vector_FindById(vec, ev->id, &idx);
            if (prgm)
            {
                vlc_vector_remove(vec, idx);
                vlc_player_program_Destroy(prgm);
                return VLC_SUCCESS;
            }
            return VLC_EGENERIC;
        }
        case VLC_INPUT_PROGRAM_SELECTED:
        {
            int ret = VLC_EGENERIC;
            vlc_vector_foreach(prgm, vec)
            {
                if (prgm->p.id == ev->id)
                {
                    prgm->p.selected = true;
                    ret = VLC_SUCCESS;
                }
                else
                    prgm->p.selected = false;
            }
            return ret;
        }
        case VLC_INPUT_PROGRAM_SCRAMBLED:
            prgm = vlc_player_program_vector_FindById(vec, ev->id, NULL);
            if (prgm)
            {
                prgm->p.scrambled = ev->scrambled;
                return VLC_SUCCESS;
            }
            return VLC_EGENERIC;
        default:
            vlc_assert_unreachable();
    }
}

static inline vlc_player_track_vector *
vlc_player_input_GetTrackVector(struct vlc_player_input *input,
                                enum es_format_category_e cat)
{
    switch (cat)
    {
        case VIDEO_ES:
            return &input->video_track_vector;
        case AUDIO_ES:
            return &input->audio_track_vector;
        case SPU_ES:
            return &input->spu_track_vector;
        default:
            return NULL;
    }
}

static struct vlc_player_track *
vlc_player_track_vector_FindById(vlc_player_track_vector *vec, vlc_es_id_t *id,
                                 size_t *idx)
{
    for (size_t i = 0; i < vec->size; ++i)
    {
        struct vlc_player_track *track = vec->data[i];
        if (track->id == id)
        {
            if (idx)
                *idx = i;
            return track;
        }
    }
    return NULL;
}

size_t
vlc_player_GetTrackCount(vlc_player_t *player, enum es_format_category_e cat)
{
    vlc_player_assert_locked(player);
    assert(player->input);

    vlc_player_track_vector *vec =
        vlc_player_input_GetTrackVector(player->input, cat);
    if (vec)
        return 0;
    return vec->size;
}

const struct vlc_player_track *
vlc_player_GetTrackAt(vlc_player_t *player, enum es_format_category_e cat,
                      size_t index)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    vlc_player_track_vector *vec = vlc_player_input_GetTrackVector(input, cat);
    if (!vec)
        return NULL;
    assert(index < vec->size);
    return vec->data[index];
}

const struct vlc_player_track *
vlc_player_GetTrack(vlc_player_t *player, vlc_es_id_t *id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    vlc_player_track_vector *vec =
        vlc_player_input_GetTrackVector(input, vlc_es_id_GetCat(id));
    if (!vec)
        return NULL;
    return vlc_player_track_vector_FindById(vec, id, NULL);
}

void
vlc_player_SelectTrack(vlc_player_t *player, vlc_es_id_t *id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    input_ControlPushEsHelper(input->thread, INPUT_CONTROL_SET_ES, id);
}

void
vlc_player_UnselectTrack(vlc_player_t *player, vlc_es_id_t *id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    input_ControlPushEsHelper(input->thread, INPUT_CONTROL_UNSET_ES, id);
}

void
vlc_player_RestartTrack(vlc_player_t *player, vlc_es_id_t *id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    input_ControlPushEsHelper(input->thread, INPUT_CONTROL_RESTART_ES, id);
}

void
vlc_player_SelectDefaultTrack(vlc_player_t *player,
                              enum es_format_category_e cat, const char *lang)
{
    vlc_player_assert_locked(player);
    /* TODO */ (void) cat; (void) lang;
}

static struct vlc_player_track *
vlc_player_track_New(vlc_es_id_t *id, const char *title, const es_format_t *fmt)
{
    struct vlc_player_track *track = malloc(sizeof(*track));
    if (!track)
        return NULL;
    track->title = strdup(title);
    if (!track->title)
    {
        free(track);
        return NULL;
    }

    int ret = es_format_Copy(&track->fmt, fmt);
    if (ret != VLC_SUCCESS)
    {
        free((char *)track->title);
        free(track);
        return NULL;
    }
    track->id = vlc_es_id_Hold(id);
    track->selected = false;

    return track;
}

struct vlc_player_track *
vlc_player_track_Dup(const struct vlc_player_track *src)
{
    struct vlc_player_track *dup =
        vlc_player_track_New(src->id, src->title, &src->fmt);

    if (!dup)
        return NULL;
    dup->selected = src->selected;
    return dup;
}

void
vlc_player_track_Release(struct vlc_player_track *track)
{
    es_format_Clean(&track->fmt);
    free((char *)track->title);
    vlc_es_id_Release(track->id);
    free(track);
}

static int
vlc_player_track_Update(struct vlc_player_track *track,
                             const char *title, const es_format_t *fmt)
{
    if (strcmp(title, track->title) != 0)
    {
        char *dup = strdup(title);
        if (!dup)
            return VLC_ENOMEM;
        free((char *)track->title);
        track->title = dup;
    }

    es_format_t fmtdup;
    int ret = es_format_Copy(&fmtdup, fmt);
    if (ret != VLC_SUCCESS)
        return ret;

    es_format_Clean(&track->fmt);
    track->fmt = fmtdup;
    return VLC_SUCCESS;
}

static void
vlc_player_StartDiscontinuity(vlc_player_t *player)
{
    struct vlc_player_input *input = player->input;
    if (input->discontinuity)
        return;

    input->discontinuity = true;
    vlc_player_SendEvent(player, on_discontinuity_changed,
                         input->discontinuity, VLC_TICK_INVALID, 0.0f, 0.0f);
}

static void
vlc_player_StopDiscontinuity(vlc_player_t *player, vlc_tick_t time, float pos,
                             float rate)
{
    struct vlc_player_input *input = player->input;
    if (!input->discontinuity)
        return;

    if (input->cache == 1.f && input->state == PLAYING_S)
    {
        input->discontinuity = false;
        vlc_player_SendEvent(player, on_discontinuity_changed,
                             input->discontinuity, time, pos, rate);
    }
}

static void
vlc_player_input_HandleEsEvent(struct vlc_player_input *input,
                               const struct vlc_input_event_es *ev)
{
    assert(ev->id && ev->title && ev->fmt);

    vlc_player_t *player = input->player;
    struct vlc_player_track *track;
    vlc_player_track_vector *vec =
        vlc_player_input_GetTrackVector(input, ev->fmt->i_cat);

    if (!vec)
        return; /* UNKNOWN_ES or DATA_ES not handled */

    switch (ev->action)
    {
        case VLC_INPUT_ES_ADDED:
            track = vlc_player_track_New(ev->id, ev->title, ev->fmt);
            if (!track)
                return;

            if (!vlc_vector_push(vec, track))
            {
                vlc_player_track_Release(track);
                break;
            }
            vlc_player_SendEvent(player, on_track_list_changed,
                                 VLC_PLAYER_LIST_ADDED, track);
            break;
        case VLC_INPUT_ES_DELETED:
        {
            size_t idx;
            track = vlc_player_track_vector_FindById(vec, ev->id, &idx);
            if (track)
            {
                vlc_player_SendEvent(player, on_track_list_changed,
                                     VLC_PLAYER_LIST_REMOVED, track);
                vlc_vector_remove(vec, idx);
                vlc_player_track_Release(track);
            }
            break;
        }
        case VLC_INPUT_ES_UPDATED:
            track = vlc_player_track_vector_FindById(vec, ev->id, NULL);
            if (track
             && vlc_player_track_Update(track, ev->title, ev->fmt) == 0)
                vlc_player_SendEvent(player, on_track_list_changed,
                                     VLC_PLAYER_LIST_UPDATED, track);
            break;
        case VLC_INPUT_ES_SELECTED:
            track = vlc_player_track_vector_FindById(vec, ev->id, NULL);
            if (track)
            {
                track->selected = true;
                vlc_player_SendEvent(player, on_track_selection_changed,
                                     NULL, track->id);
            }
            break;
        case VLC_INPUT_ES_UNSELECTED:
            track = vlc_player_track_vector_FindById(vec, ev->id, NULL);
            if (track)
            {
                track->selected = false;
                vlc_player_SendEvent(player, on_track_selection_changed,
                                     track->id, NULL);
            }
            break;
        default:
            vlc_assert_unreachable();
    }
}

static void
vlc_player_input_HandleVoutEvent(struct vlc_player_input *input,
                                 const struct vlc_input_event_vout *ev)
{
    assert(ev->vout);

    vlc_player_t *player = input->player;
    enum vlc_player_list_action action;
    switch (ev->action)
    {
        case VLC_INPUT_EVENT_VOUT_ADDED:
            action = VLC_PLAYER_LIST_ADDED;
            break;
        case VLC_INPUT_EVENT_VOUT_REMOVED:
            action = VLC_PLAYER_LIST_REMOVED;
            break;
        default:
            vlc_assert_unreachable();
    }
    vlc_player_SendEvent(player, on_vout_list_changed, action, ev->vout);
}

static void
input_thread_events(input_thread_t *input_thread,
                    const struct vlc_input_event *event, void *user_data)
{
    struct vlc_player_input *input = user_data;
    vlc_player_t *player = input->player;

    assert(input_thread == input->thread);

    vlc_mutex_lock(&player->lock);

    if (!input->started)
    {
        if (input->stop_async && event->type == INPUT_EVENT_DEAD)
        {
            /* Don't increment wait_count: already done by
             * vlc_player_input_StopAndClose(false) */
            vlc_list_append(&input->node, &player->destructor.inputs);
            vlc_cond_signal(&player->destructor.cond);
        }

        vlc_mutex_unlock(&player->lock);
        return;
    }

    switch (event->type)
    {
        case INPUT_EVENT_STATE:
        {
            enum vlc_player_state player_state;
            bool send_event = true;

            input->state = event->state;
            switch (input->state)
            {
                case OPENING_S:
                    player_state = VLC_PLAYER_STATE_STARTED;
                    break;
                case PLAYING_S:
                    player_state = VLC_PLAYER_STATE_PLAYING;
                    break;
                case PAUSE_S:
                    player_state = VLC_PLAYER_STATE_PAUSED;
                    vlc_player_StartDiscontinuity(player);
                    break;
                case END_S:
                    player_state = VLC_PLAYER_STATE_STOPPED;
                    break;
                case ERROR_S:
                    player_state = VLC_PLAYER_STATE_ERROR;
                    break;
                default:
                    send_event = false;
                    break;
            }
            if (send_event)
                vlc_player_SendEvent(player, on_state_changed, player_state);
            break;
        }
        case INPUT_EVENT_RATE:
            input->rate = event->rate;
            vlc_player_SendEvent(player, on_rate_changed, input->rate);
            vlc_player_StartDiscontinuity(player);
            break;
        case INPUT_EVENT_CAPABILITIES:
            input->capabilities = event->capabilities;
            vlc_player_SendEvent(player, on_capabilities_changed,
                                 input->capabilities);
            break;
        case INPUT_EVENT_POSITION:
#if GAPLESS
            /* XXX case INPUT_EVENT_EOF: */
            if (player->next_input == NULL)
                break;
            vlc_tick_t length = input->length;
            vlc_tick_t time = event->position.ms;
            if (length > 0 && time > 0
             && length - time <= AOUT_MAX_PREPARE_TIME)
                vlc_player_OpenNextItem(player);
#endif
            input->position_ms = event->position.ms;
            input->position_percent = event->position.percentage;
            input->position_date = vlc_tick_now();
            vlc_player_StopDiscontinuity(player,input->position_ms,
                                         input->position_percent, input->rate);
            break;
        case INPUT_EVENT_LENGTH:
            input->length = event->length;
            vlc_player_SendEvent(player, on_length_changed, input->length);
            break;
        case INPUT_EVENT_PROGRAM:
            vlc_player_input_HandleProgramEvent(input, &event->program);
            break;
        case INPUT_EVENT_ES:
            vlc_player_input_HandleEsEvent(input, &event->es);
            break;
        case INPUT_EVENT_TITLE:
            /* TODO */
            break;
        case INPUT_EVENT_CHAPTER:
            /* TODO */
            break;
        case INPUT_EVENT_RECORD:
            input->recording = event->record;
            vlc_player_SendEvent(player, on_record_changed, input->recording);
            break;
        case INPUT_EVENT_STATISTICS:
            input->stats = *event->stats;
            vlc_player_SendEvent(player, on_stats_changed, &input->stats);
            break;
        case INPUT_EVENT_SIGNAL:
            input->signal_quality = event->signal.quality;
            input->signal_strength = event->signal.strength;
            vlc_player_SendEvent(player, on_signal_changed,
                                 input->signal_quality, input->signal_strength);
            break;
        case INPUT_EVENT_AUDIO_DELAY:
            input->audio_delay = event->audio_delay;
            vlc_player_SendEvent(player, on_audio_delay_changed,
                                 input->audio_delay);
            break;
        case INPUT_EVENT_SUBTITLE_DELAY:
            input->subtitle_delay = event->subtitle_delay;
            vlc_player_SendEvent(player, on_subtitle_delay_changed,
                                 input->subtitle_delay);
            break;
        case INPUT_EVENT_CACHE:
            input->cache = event->cache;
            if (input->cache == 0.0f)
                vlc_player_StartDiscontinuity(player);
            vlc_player_SendEvent(player, on_buffering, event->cache);
            break;
        case INPUT_EVENT_VOUT:
            vlc_player_input_HandleVoutEvent(input, &event->vout);
            break;
        case INPUT_EVENT_SUBITEMS:
            /* TODO */
            break;
        case INPUT_EVENT_DEAD:
            input->started = false;
            vlc_list_append(&input->node, &player->destructor.inputs);
            vlc_cond_signal(&player->destructor.cond);
            player->destructor.wait_count++;

            /* XXX: for now, play only one input at a time */
            if (likely(input == player->input))
            {
                player->input = NULL;
                if (vlc_player_OpenNextItem(player) == VLC_SUCCESS)
                {
                    vlc_player_SendNewPlaybackEvent(player, player->media);
                    vlc_player_input_Start(player->input);
                }
            }
            break;
        default:
            break;
    }

    vlc_mutex_unlock(&player->lock);
}

void
vlc_player_Delete(vlc_player_t *player)
{
    vlc_mutex_lock(&player->lock);

    if (player->input)
        vlc_player_input_StopAndClose(player->input, true);
#if GAPLESS
    if (player->next_input)
        vlc_player_input_StopAndClose(player->next_input, true);
#endif

    player->destructor.running = false;
    vlc_cond_signal(&player->destructor.cond);

    if (player->media)
        input_item_Release(player->media);
#if GAPLESS
    if (player->next_media)
        input_item_Release(player->next_media);
#endif

    assert(vlc_list_is_empty(&player->listeners));

    vlc_mutex_unlock(&player->lock);

    vlc_join(player->destructor.thread, NULL);

    vlc_mutex_destroy(&player->lock);
    vlc_cond_destroy(&player->destructor.cond);

    input_resource_Release(player->resource);
    if (player->renderer)
        vlc_renderer_item_release(player->renderer);

    vlc_object_release(player);
}

vlc_player_t *
vlc_player_New(vlc_object_t *parent,
               const struct vlc_player_owner_cbs *owner_cbs,
               void *owner_cbs_data)
{
    vlc_player_t *player = vlc_custom_create(parent, sizeof(*player), "player");
    if (!player)
        return NULL;

    vlc_list_init(&player->destructor.inputs);
    player->renderer = NULL;
    player->owner_cbs = owner_cbs;
    player->owner_cbs_data = owner_cbs_data;
    player->media = NULL;
    player->input = NULL;
#if GAPLESS
    player->next_media = NULL;
    player->next_input = NULL;
#endif
    player->resource = input_resource_New(VLC_OBJECT(player));

    if (!player->resource)
        goto error;

    audio_output_t *aout = input_resource_GetAout(player->resource);
    if (aout != NULL)
        input_resource_PutAout(player->resource, aout);

    player->destructor.running = true;
    vlc_mutex_init(&player->lock);
    vlc_cond_init(&player->destructor.cond);

    if (vlc_clone(&player->destructor.thread, destructor_thread, player,
                  VLC_THREAD_PRIORITY_LOW) != 0)
    {
        vlc_mutex_destroy(&player->lock);
        vlc_cond_destroy(&player->destructor.cond);
        goto error;
    }

    return player;

error:
    if (player->resource)
        input_resource_Release(player->resource);

    vlc_object_release(player);
    return NULL;
}

void
vlc_player_Lock(vlc_player_t *player)
{
    vlc_mutex_lock(&player->lock);
}

void
vlc_player_Unlock(vlc_player_t *player)
{
    vlc_mutex_unlock(&player->lock);
}

struct vlc_player_listener_id *
vlc_player_AddListener(vlc_player_t *player,
                       const struct vlc_player_cbs *cbs, void *cbs_data)
{
    assert(cbs);
    vlc_player_assert_locked(player);

    struct vlc_player_listener_id *listener = malloc(sizeof(*listener));
    if (!listener)
        return NULL;

    listener->cbs = cbs;
    listener->cbs_data = cbs_data;

    vlc_list_append(&listener->node, &player->listeners);

    return listener;
}

void
vlc_player_RemoveListener(vlc_player_t *player,
                          struct vlc_player_listener_id *id)
{
    assert(id);
    vlc_player_assert_locked(player);

    vlc_list_remove(&id->node);
    free(id);
}

int
vlc_player_SetCurrentMedia(vlc_player_t *player, input_item_t *media)
{
    vlc_player_assert_locked(player);

    if (player->input)
    {
        vlc_player_input_StopAndClose(player->input, true);
        player->input = NULL;
    }
#if GAPLESS
    if (player->next_input)
    {
        vlc_player_input_StopAndClose(player->next_input, true);
        player->next_input = NULL;
    }
#endif

    if (player->media)
    {
        input_item_Release(player->media);
        player->media = NULL;
    }
#if GAPLESS
    if (player->next_media)
    {
        input_item_Release(player->next_media);
        player->next_media = NULL;
    }
#endif

    player->media = input_item_Hold(media);
    player->input = vlc_player_input_New(player, player->media);

    if (!player->input)
    {
        input_item_Release(player->media);
        player->media = NULL;
        return VLC_ENOMEM;
    }
    return VLC_SUCCESS;
}

input_item_t *
vlc_player_GetCurrentMedia(vlc_player_t *player)
{
    vlc_player_assert_locked(player);

    return player->media;
}

int
vlc_player_AddAssociatedMedia(vlc_player_t *player,
                              enum es_format_category_e cat, const char *uri,
                              bool select, bool notify, bool check_ext)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    enum slave_type type;
    switch (cat)
    {
        case AUDIO_ES:
            type = SLAVE_TYPE_AUDIO;
            break;
        case SPU_ES:
            type = SLAVE_TYPE_SPU;
            break;
        default:
            return VLC_EGENERIC;
    }
    return input_AddSlave(input->thread, type, uri, select, notify, check_ext);
}

void
vlc_player_InvalidateNextMedia(vlc_player_t *player)
{
    (void) player;
#if GAPLESS
    vlc_player_assert_locked(player);
    if (player->next_media)
    {
        input_item_Release(player->next_media);
        player->next_media = vlc_player_GetNextItem(player);
    }
    if (player->next_input)
    {
        /* Cause the get_next_media callback to be called when this input is
         * dead */
        vlc_player_input_StopAndClose(player->next_input, false);
        player->next_input = NULL;
    }
#endif
}

int
vlc_player_Start(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    assert(player->media);

    if (!player->input)
    {
        /* Possible if the player was stopped by the user */
        assert(player->media);
        player->input = vlc_player_input_New(player, player->media);

        if (!player->input)
            return VLC_EGENERIC;
    }
    assert(!player->input->started);

    return vlc_player_input_Start(player->input);
}

static void
vlc_player_CommonStop(vlc_player_t *player, bool wait)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    assert(input->started);

    vlc_player_input_StopAndClose(input, wait);
    player->input = NULL;

#if GAPLESS
    if (player->next_input)
    {
        vlc_player_input_StopAndClose(player->next_input, wait);
        player->next_input = NULL;
    }
#endif
}

void
vlc_player_Stop(vlc_player_t *player)
{
    vlc_player_CommonStop(player, true);
}

void
vlc_player_RequestStop(vlc_player_t *player)
{
    vlc_player_CommonStop(player, false);
}

void
vlc_player_Pause(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    assert(input->started);
    assert(player->input->capabilities & VLC_INPUT_CAPABILITIES_PAUSEABLE);

    input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_STATE,
                            &(vlc_value_t) {.i_int = PAUSE_S});
}

void
vlc_player_Resume(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    assert(input->started);
    assert(player->input->capabilities & VLC_INPUT_CAPABILITIES_PAUSEABLE);

    input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_STATE,
                            &(vlc_value_t) { .i_int = PLAYING_S });
}

bool
vlc_player_IsStarted(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    return player->input && player->input->started;
}

bool
vlc_player_IsPaused(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    return !player->input || player->input->state != PLAYING_S;
}

int
vlc_player_GetCapabilities(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input->capabilities;
}

vlc_tick_t
vlc_player_GetLength(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input->length;
}

vlc_tick_t
vlc_player_GetTime(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    vlc_tick_t time = input->position_ms;
    return time != VLC_TICK_INVALID ?
        (time + (vlc_tick_now() - input->position_date) * input->rate) :
        VLC_TICK_INVALID;
}

float
vlc_player_GetPosition(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    return input->position_percent;
}

void
vlc_player_Seek(vlc_player_t *player, const struct vlc_player_seek_arg *arg)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    assert(arg);

    switch (arg->type)
    {
        case VLC_PLAYER_SEEK_BY_POS:
            input_ControlPush(input->thread, INPUT_CONTROL_SET_POSITION,
                &(input_control_param_t) {
                    .pos.f_val = arg->position,
                    .pos.b_fast_seek = arg->fast,
                    .pos.b_absolute = arg->absolute
            });
            break;
        case VLC_PLAYER_SEEK_BY_TIME:
            input_ControlPush(input->thread, INPUT_CONTROL_SET_TIME,
                &(input_control_param_t) {
                    .time.i_val = arg->time,
                    .time.b_fast_seek = arg->fast,
                    .time.b_absolute = arg->absolute,
            });
            break;
        default:
            vlc_assert_unreachable();
    }
}

void
vlc_player_SetRenderer(vlc_player_t *player, vlc_renderer_item_t *renderer)
{
    vlc_player_assert_locked(player);

    if (player->renderer)
        vlc_renderer_item_release(player->renderer);
    player->renderer = renderer ? vlc_renderer_item_hold(renderer) : NULL;

    vlc_player_foreach_inputs(input)
    {
        vlc_value_t val = {
            .p_address = renderer ? vlc_renderer_item_hold(renderer) : NULL
        };
        input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_RENDERER,
                                &val);
    }
}

void
vlc_player_Navigate(vlc_player_t *player, enum vlc_player_nav nav)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    enum input_control_e control;
    switch (nav)
    {
        case VLC_PLAYER_NAV_ACTIVATE:
            control = INPUT_CONTROL_NAV_ACTIVATE;
            break;
        case VLC_PLAYER_NAV_UP:
            control = INPUT_CONTROL_NAV_UP;
            break;
        case VLC_PLAYER_NAV_DOWN:
            control = INPUT_CONTROL_NAV_DOWN;
            break;
        case VLC_PLAYER_NAV_LEFT:
            control = INPUT_CONTROL_NAV_LEFT;
            break;
        case VLC_PLAYER_NAV_RIGHT:
            control = INPUT_CONTROL_NAV_RIGHT;
            break;
        case VLC_PLAYER_NAV_POPUP:
            control = INPUT_CONTROL_NAV_POPUP;
            break;
        case VLC_PLAYER_NAV_MENU:
            control = INPUT_CONTROL_NAV_MENU;
            break;
        default:
            vlc_assert_unreachable();
    }
    input_ControlPushHelper(input->thread, control, NULL);
}

bool
vlc_player_IsRecording(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    return input->recording;
}

void
vlc_player_SetAudioDelay(vlc_player_t *player, vlc_tick_t delay,
                         bool absolute)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    input_ControlPush(input->thread, INPUT_CONTROL_SET_AUDIO_DELAY,
        &(input_control_param_t) {
            .delay = {
                .b_absolute = absolute,
                .i_val = delay,
            },
    });
}

vlc_tick_t
vlc_player_GetAudioDelay(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input->audio_delay;
}

void
vlc_player_SetSubtitleDelay(vlc_player_t *player, vlc_tick_t delay,
                            bool absolute)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    input_ControlPush(input->thread, INPUT_CONTROL_SET_SPU_DELAY,
        &(input_control_param_t) {
            .delay = {
                .b_absolute = absolute,
                .i_val = delay,
            },
    });
}

vlc_tick_t
vlc_player_GetSubtitleDelay(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input->subtitle_delay;
}

int
vlc_player_GetSignal(vlc_player_t *player, float *quality, float *strength)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (input->signal_quality >= 0 && input->signal_strength >= 0)
    {
        *quality = input->signal_quality;
        *strength = input->signal_strength;
        return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
}

void
vlc_player_GetStats(vlc_player_t *player, struct input_stats_t *stats)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    *stats = input->stats;
}

size_t
vlc_player_GetVouts(vlc_player_t *player, vout_thread_t ***vouts)
{
    size_t count;
    input_resource_HoldVouts(player->resource, vouts, &count);
    return count;
}

audio_output_t *
vlc_player_GetAout(vlc_player_t *player)
{
    return input_resource_HoldAout(player->resource);
}

int
vlc_player_aout_EnableFilter(vlc_player_t *player, const char *name, bool add)
{
    audio_output_t *aout = vlc_player_GetAout(player);
    if (!aout)
        return -1;
    /* XXX make aout_ChangeFilterString public ? */
    aout_ChangeFilterString(NULL, aout ? VLC_OBJECT(aout) : NULL,
                            "audio-filter", name, add);
    vlc_object_release(aout);
    return 0;
}
