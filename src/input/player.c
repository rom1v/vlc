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
#include "player.h"
#include <vlc_aout.h>
#include <vlc_interface.h>
#include <vlc_renderer_discovery.h>
#include <vlc_list.h>
#include <vlc_vector.h>
#include <vlc_atomic.h>

#include "libvlc.h"
#include "input_internal.h"
#include "resource.h"
#include "../audio_output/aout_internal.h"

#define RETRY_TIMEOUT_BASE VLC_TICK_FROM_MS(100)
#define RETRY_TIMEOUT_MAX VLC_TICK_FROM_MS(3200)

static_assert(VLC_PLAYER_CAP_SEEK == VLC_INPUT_CAPABILITIES_SEEKABLE &&
              VLC_PLAYER_CAP_PAUSE == VLC_INPUT_CAPABILITIES_PAUSEABLE &&
              VLC_PLAYER_CAP_CHANGE_RATE == VLC_INPUT_CAPABILITIES_CHANGE_RATE &&
              VLC_PLAYER_CAP_REWIND == VLC_INPUT_CAPABILITIES_REWINDABLE,
              "player/input capabilities mismatch");

static_assert(VLC_PLAYER_TITLE_MENU == INPUT_TITLE_MENU &&
              VLC_PLAYER_TITLE_INTERACTIVE == INPUT_TITLE_INTERACTIVE,
              "player/input title flag mismatch");


#define GAPLESS 0 /* TODO */

typedef struct VLC_VECTOR(struct vlc_player_program *)
    vlc_player_program_vector;

typedef struct VLC_VECTOR(struct vlc_player_track *)
    vlc_player_track_vector;

struct vlc_player_listener_id
{
    const struct vlc_player_cbs * cbs;
    void * cbs_data;
    struct vlc_list node;
};

struct vlc_player_title_list
{
    vlc_atomic_rc_t rc;
    size_t count;
    struct vlc_player_title array[];
};

struct vlc_player_input
{
    input_thread_t *thread;
    vlc_player_t *player;
    bool started;
    bool close_async;

    enum vlc_player_state state;
    enum vlc_player_error error;
    float rate;
    int capabilities;
    vlc_tick_t length;

    vlc_tick_t position_ms;
    float position_percent;

    bool recording;

    float signal_quality;
    float signal_strength;
    float cache;

    struct input_stats_t stats;

    vlc_tick_t audio_delay;
    vlc_tick_t subtitle_delay;

    vlc_player_program_vector program_vector;
    vlc_player_track_vector video_track_vector;
    vlc_player_track_vector audio_track_vector;
    vlc_player_track_vector spu_track_vector;

    struct vlc_player_title_list *titles;

    size_t title_selected;
    size_t chapter_selected;

    struct vlc_list node;
};

struct vlc_player_t
{
    struct vlc_common_members obj;
    vlc_mutex_t lock;
    vlc_cond_t start_delay_cond;

    enum vlc_player_media_ended_action media_ended_action;
    bool start_paused;

    const struct vlc_player_media_provider *media_provider;
    void *media_provider_data;

    struct vlc_list listeners;

    input_resource_t *resource;
    vlc_renderer_item_t *renderer;

    input_item_t *media;
    struct vlc_player_input *input;
    struct vlc_player_input *closing_input;

    input_item_t *next_media;
#if GAPLESS
    struct vlc_player_input *next_input;
#endif

    enum vlc_player_state global_state;

    unsigned error_count;

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

void
vlc_player_assert_locked(vlc_player_t *player)
{
    assert(player);
    vlc_assert_locked(&player->lock);
}

static inline struct vlc_player_input *
vlc_player_get_input_locked(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    return player->input;
}

static char *
vlc_player_program_DupTitle(int id, const char *title)
{
    char *dup;
    if (title)
        dup = strdup(title);
    else if (asprintf(&dup, "%d", id) == -1)
        dup = NULL;
    return dup;
}

static struct vlc_player_program *
vlc_player_program_New(int id, const char *title)
{
    struct vlc_player_program *prgm = malloc(sizeof(*prgm));
    if (!prgm)
        return NULL;
    prgm->title = vlc_player_program_DupTitle(id, title);
    if (!prgm->title)
    {
        free(prgm);
        return NULL;
    }
    prgm->id = id;
    prgm->selected = prgm->scrambled = false;

    return prgm;
}

static int
vlc_player_program_Update(struct vlc_player_program *prgm, int id,
                          const char *title)
{
    free((char *)prgm->title);
    prgm->title = vlc_player_program_DupTitle(id, title);
    return prgm->title != NULL ? VLC_SUCCESS : VLC_ENOMEM;
}

struct vlc_player_program *
vlc_player_program_Dup(const struct vlc_player_program *src)
{
    struct vlc_player_program *dup =
        vlc_player_program_New(src->id, src->title);

    if (!dup)
        return NULL;
    dup->selected = src->selected;
    dup->scrambled = src->scrambled;
    return dup;
}

void
vlc_player_program_Delete(struct vlc_player_program *prgm)
{
    free((char *)prgm->title);
    free(prgm);
}

static struct vlc_player_program *
vlc_player_program_vector_FindById(vlc_player_program_vector *vec, int id,
                                   size_t *idx)
{
    for (size_t i = 0; i < vec->size; ++i)
    {
        struct vlc_player_program *prgm = vec->data[i];
        if (prgm->id == id)
        {
            if (idx)
                *idx = i;
            return prgm;
        }
    }
    return NULL;
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
vlc_player_track_Delete(struct vlc_player_track *track)
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

struct vlc_player_title_list *
vlc_player_title_list_Hold(struct vlc_player_title_list *titles)
{
    vlc_atomic_rc_inc(&titles->rc);
    return titles;
}

void
vlc_player_title_list_Release(struct vlc_player_title_list *titles)
{
    if (!vlc_atomic_rc_dec(&titles->rc))
        return;
    for (size_t title_idx = 0; title_idx < titles->count; ++title_idx)
    {
        struct vlc_player_title *title = &titles->array[title_idx];
        free((char *)title->name);
        for (size_t chapter_idx = 0; chapter_idx < title->chapter_count;
             ++chapter_idx)
        {
            const struct vlc_player_chapter *chapter =
                &title->chapters[chapter_idx];
            free((char *)chapter->name);
        }
        free((void *)title->chapters);
    }
    free(titles);
}

static struct vlc_player_title_list *
vlc_player_title_list_Create(const input_title_t **array, size_t count)
{
    size_t size;
    if (mul_overflow(count, sizeof(struct vlc_player_title), &size))
        return NULL;
    if (add_overflow(size, sizeof(struct vlc_player_title_list), &size))
        return NULL;

    struct vlc_player_title_list *titles = malloc(size);
    if (!titles)
        return NULL;

    vlc_atomic_rc_init(&titles->rc);
    titles->count = count;

    for (size_t title_idx = 0; title_idx < titles->count; ++title_idx)
    {
        const struct input_title_t *input_title = array[title_idx];
        struct vlc_player_title *title = &titles->array[title_idx];

        title->name = input_title->psz_name ?
                      strdup(input_title->psz_name): NULL;
        title->length = input_title->i_length;
        title->flags = input_title->i_flags;
        title->chapter_count = input_title->i_seekpoint;

        struct vlc_player_chapter *chapters = title->chapter_count == 0 ? NULL :
            vlc_alloc(title->chapter_count, sizeof(*chapters));

        if (chapters)
        {
            for (size_t chapter_idx = 0; chapter_idx < title->chapter_count;
                 ++chapter_idx)
            {
                struct vlc_player_chapter *chapter = &chapters[chapter_idx];
                seekpoint_t *seekpoint = input_title->seekpoint[chapter_idx];

                chapter->name = seekpoint->psz_name ?
                                strdup(seekpoint->psz_name) : NULL;
                chapter->time = seekpoint->i_time_offset;
            }
        }
        title->chapters = chapters;

        if (!title->chapters && input_title->i_seekpoint > 0)
        {
            titles->count = title_idx;
            vlc_player_title_list_Release(titles);
            return NULL;
        }
    }
    return titles;
}

const struct vlc_player_title *
vlc_player_title_list_GetAt(struct vlc_player_title_list *titles, size_t idx)
{
    assert(idx < titles->count);
    return &titles->array[idx];
}

size_t
vlc_player_title_list_GetCount(struct vlc_player_title_list *titles)
{
    return titles->count;
}

static struct vlc_player_input *
vlc_player_input_New(vlc_player_t *player, input_item_t *item)
{
    struct vlc_player_input *input = malloc(sizeof(*input));
    if (!input)
        return NULL;

    input->player = player;
    input->started = false;
    input->close_async = true;

    input->state = VLC_PLAYER_STATE_IDLE;
    input->error = VLC_PLAYER_ERROR_NONE;
    input->rate = 1.f;
    input->capabilities = 0;
    input->length = input->position_ms =
    input->position_percent = 0.f;

    input->recording = false;

    input->cache = 0.f;
    input->signal_quality = input->signal_strength = -1.f;

    memset(&input->stats, 0, sizeof(input->stats));

    input->audio_delay = input->subtitle_delay = 0;

    vlc_vector_init(&input->program_vector);
    vlc_vector_init(&input->video_track_vector);
    vlc_vector_init(&input->audio_track_vector);
    vlc_vector_init(&input->spu_track_vector);

    input->titles = NULL;
    input->title_selected = input->chapter_selected = 0;

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
    vlc_player_Unlock(input->player);
    const bool keep_sout = var_GetBool(input->thread, "sout-keep");
    input_Close(input->thread);
    if (!keep_sout)
        input_resource_TerminateSout(input->player->resource);
    vlc_player_Lock(input->player);

    if (input->titles)
    {
        vlc_player_title_list_Release(input->titles);
        vlc_player_SendEvent(input->player, on_titles_changed, NULL);
    }

    assert(input->program_vector.size == 0);
    assert(input->video_track_vector.size == 0);
    assert(input->audio_track_vector.size == 0);
    assert(input->spu_track_vector.size == 0);

    vlc_vector_destroy(&input->program_vector);
    vlc_vector_destroy(&input->video_track_vector);
    vlc_vector_destroy(&input->audio_track_vector);
    vlc_vector_destroy(&input->spu_track_vector);

    free(input);
}

static int
vlc_player_input_Start(struct vlc_player_input *input)
{
    int ret = input_Start(input->thread);
    if (ret != VLC_SUCCESS)
        return ret;
    input->started = true;
    input->state = VLC_PLAYER_STATE_STARTED;
    return ret;
}

void
vlc_player_SetMediaEndedAction(vlc_player_t *player,
                               enum vlc_player_media_ended_action action)
{
    vlc_player_assert_locked(player);
    player->media_ended_action = action;
    var_SetBool(player, "play-and-pause",
                action == VLC_PLAYER_MEDIA_ENDED_PAUSE);
}

void
vlc_player_SetStartPaused(vlc_player_t *player, bool start_paused)
{
    vlc_player_assert_locked(player);
    player->start_paused = start_paused;
}

static void
vlc_player_input_StopAndClose(struct vlc_player_input *input, bool async)
{
    vlc_player_t *player = input->player;
    player->input = NULL;

    if (input->started)
    {
        input->started = false;
        input->close_async = async;
        input_Stop(input->thread);
        if (input->close_async)
        {
            player->closing_input = input;
            player->destructor.wait_count++;
            /* This input will be cleaned when we receive the INPUT_EVENT_DEAD
             * event */
            return;
        }
    }

    /* Remove the input from the destructor thread if this input is finally
     * closed synchronously */
    struct vlc_player_input *free_input;
    vlc_list_foreach(free_input, &player->destructor.inputs, node)
    {
        if (input == free_input)
        {
            vlc_list_remove(&input->node);
            player->destructor.wait_count--;
            break;
        }
    }

    vlc_player_input_Delete(input);
}

static void
vlc_player_input_CloseAsync(struct vlc_player_input *input)
{
    vlc_player_t *player = input->player;

    if (input->close_async)
    {
        vlc_list_append(&input->node, &player->destructor.inputs);
        if (input->started)
            player->destructor.wait_count++;
        vlc_cond_signal(&player->destructor.cond);
    }
    input->started = false;

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
            vlc_list_remove(&input->node);
            vlc_player_input_Delete(input);
            player->destructor.wait_count--;
        }
    }
    vlc_mutex_unlock(&player->lock);
    return NULL;
}

static void
vlc_player_GetNextMedia(vlc_player_t *player)
{
    vlc_player_assert_locked(player);

    assert(player->next_media == NULL);
    if (!player->media_provider)
        return;
    player->next_media =
        player->media_provider->get_next(player, player->media_provider_data);
}

static int
vlc_player_OpenNextMedia(vlc_player_t *player)
{
    assert(player->input == NULL);

    if (!player->next_media)
        return VLC_EGENERIC;

    if (player->media)
        input_item_Release(player->media);
    player->media = player->next_media;
    player->next_media = NULL;

    player->input = vlc_player_input_New(player, player->media);

    return player->input ? VLC_SUCCESS : VLC_EGENERIC;
}

size_t
vlc_player_GetProgramCount(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    return input ? input->program_vector.size : 0;
}

const struct vlc_player_program *
vlc_player_GetProgramAt(vlc_player_t *player, size_t index)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (!input)
        return NULL;

    assert(index < input->program_vector.size);
    return input->program_vector.data[index];
}

const struct vlc_player_program *
vlc_player_GetProgram(vlc_player_t *player, int id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (!input)
        return NULL;

    struct vlc_player_program *prgm =
        vlc_player_program_vector_FindById(&input->program_vector, id, NULL);
    return prgm;
}

void
vlc_player_SelectProgram(vlc_player_t *player, int id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (input)
        input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_PROGRAM,
                                &(vlc_value_t) { .i_int = id });
}

static void
vlc_player_input_HandleProgramEvent(struct vlc_player_input *input,
                                    const struct vlc_input_event_program *ev)
{
    vlc_player_t *player = input->player;
    struct vlc_player_program *prgm;
    vlc_player_program_vector *vec = &input->program_vector;

    switch (ev->action)
    {
        case VLC_INPUT_PROGRAM_ADDED:
            prgm = vlc_player_program_New(ev->id, ev->title);
            if (!prgm)
                break;

            if (!vlc_vector_push(vec, prgm))
            {
                vlc_player_program_Delete(prgm);
                break;
            }
            vlc_player_SendEvent(player, on_program_list_changed,
                                 VLC_PLAYER_LIST_ADDED, prgm);
            break;
        case VLC_INPUT_PROGRAM_DELETED:
        {
            size_t idx;
            prgm = vlc_player_program_vector_FindById(vec, ev->id, &idx);
            if (prgm)
            {
                vlc_player_SendEvent(player, on_program_list_changed,
                                     VLC_PLAYER_LIST_REMOVED, prgm);
                vlc_vector_remove(vec, idx);
                vlc_player_program_Delete(prgm);
            }
            break;
        }
        case VLC_INPUT_PROGRAM_UPDATED:
        case VLC_INPUT_PROGRAM_SCRAMBLED:
            prgm = vlc_player_program_vector_FindById(vec, ev->id, NULL);
            if (!prgm)
                break;
            if (ev->action == VLC_INPUT_PROGRAM_UPDATED)
            {
                if (vlc_player_program_Update(prgm, ev->id, ev->title) != 0)
                    break;
            }
            else
                prgm->scrambled = ev->scrambled;
            vlc_player_SendEvent(player, on_program_list_changed,
                                 VLC_PLAYER_LIST_UPDATED, prgm);
            break;
        case VLC_INPUT_PROGRAM_SELECTED:
        {
            int unselected_id = -1, selected_id = -1;
            vlc_vector_foreach(prgm, vec)
            {
                if (prgm->id == ev->id)
                {
                    if (!prgm->selected)
                    {
                        assert(selected_id == -1);
                        prgm->selected = true;
                        selected_id = prgm->id;
                    }
                }
                else
                {
                    if (prgm->selected)
                    {
                        assert(unselected_id == -1);
                        prgm->selected = false;
                        unselected_id = prgm->id;
                    }
                }
            }
            if (unselected_id != -1 || selected_id != -1)
                vlc_player_SendEvent(player, on_program_selection_changed,
                                     unselected_id, selected_id);
            break;
        }
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
    if (!vec)
        return 0;
    return vec->size;
}

const struct vlc_player_track *
vlc_player_GetTrackAt(vlc_player_t *player, enum es_format_category_e cat,
                      size_t index)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (!input)
        return NULL;
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

    if (!input)
        return NULL;
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

    if (input)
        input_ControlPushEsHelper(input->thread, INPUT_CONTROL_SET_ES, id);
}

void
vlc_player_UnselectTrack(vlc_player_t *player, vlc_es_id_t *id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (input)
        input_ControlPushEsHelper(input->thread, INPUT_CONTROL_UNSET_ES, id);
}

void
vlc_player_RestartTrack(vlc_player_t *player, vlc_es_id_t *id)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (input)
        input_ControlPushEsHelper(input->thread, INPUT_CONTROL_RESTART_ES, id);
}

void
vlc_player_SelectDefaultTrack(vlc_player_t *player,
                              enum es_format_category_e cat, const char *lang)
{
    vlc_player_assert_locked(player);
    /* TODO */ (void) cat; (void) lang;
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
                break;

            if (!vlc_vector_push(vec, track))
            {
                vlc_player_track_Delete(track);
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
                vlc_player_track_Delete(track);
            }
            break;
        }
        case VLC_INPUT_ES_UPDATED:
            track = vlc_player_track_vector_FindById(vec, ev->id, NULL);
            if (!track)
                break;
            if (vlc_player_track_Update(track, ev->title, ev->fmt) != 0)
                break;
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
vlc_player_input_HandleTitleEvent(struct vlc_player_input *input,
                                  const struct vlc_input_event_title *ev)
{
    vlc_player_t *player = input->player;
    switch (ev->action)
    {
        case VLC_INPUT_TITLE_NEW_LIST:
        {
            if (input->titles)
                vlc_player_title_list_Release(input->titles);
            input->title_selected = input->chapter_selected = 0;
            if (ev->list.count > 0)
                input->titles = vlc_player_title_list_Create(ev->list.array,
                                                         ev->list.count);
            else
                input->titles = NULL;
            vlc_player_SendEvent(player, on_titles_changed, input->titles);
            if (input->titles)
                vlc_player_SendEvent(player, on_title_selection_changed,
                                     &input->titles->array[0], 0);
            break;
        }
        case VLC_INPUT_TITLE_SELECTED:
            if (!input->titles)
                return; /* a previous VLC_INPUT_TITLE_NEW_LIST failed */
            assert(ev->selected_idx < input->titles->count);
            input->title_selected = ev->selected_idx;
            vlc_player_SendEvent(player, on_title_selection_changed,
                                 &input->titles->array[input->title_selected],
                                 input->title_selected);
            break;
        default:
            vlc_assert_unreachable();
    }
}

static void
vlc_player_input_HandleChapterEvent(struct vlc_player_input *input,
                                    const struct vlc_input_event_chapter *ev)
{
    vlc_player_t *player = input->player;
    if (!input->titles || ev->title < 0 || ev->seekpoint < 0)
        return; /* a previous VLC_INPUT_TITLE_NEW_LIST failed */

    assert((size_t)ev->title < input->titles->count);
    const struct vlc_player_title *title = &input->titles->array[ev->title];
    if (!title->chapter_count)
        return;

    assert(ev->seekpoint < (int)title->chapter_count);
    input->title_selected = ev->title;
    input->chapter_selected = ev->seekpoint;

    const struct vlc_player_chapter *chapter = &title->chapters[ev->seekpoint];
    vlc_player_SendEvent(player, on_chapter_selection_changed, title, ev->title,
                         chapter, ev->seekpoint);
}

struct vlc_player_title_list *
vlc_player_GetTitleList(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input ? input->titles : NULL;
}

ssize_t
vlc_player_GetSelectedTitleIdx(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (!input)
        return -1;
    return input->title_selected;
}

static ssize_t
vlc_player_GetTitleIdx(vlc_player_t *player,
                       const struct vlc_player_title *title)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input && input->titles)
        for (size_t i = 0; i < input->titles->count; ++i)
            if (&input->titles->array[i] == title)
                return i;
    return -1;
}

void
vlc_player_SelectTitle(vlc_player_t *player,
                       const struct vlc_player_title *title)
{
    ssize_t idx = vlc_player_GetTitleIdx(player, title);
    if (idx != -1)
        vlc_player_SelectTitleIdx(player, idx);
}

void
vlc_player_SelectChapter(vlc_player_t *player,
                         const struct vlc_player_title *title,
                         size_t chapter_idx)
{
    ssize_t idx = vlc_player_GetTitleIdx(player, title);
    if (idx != -1)
        vlc_player_SelectChapterIdx(player, chapter_idx);
}

void
vlc_player_SelectTitleIdx(vlc_player_t *player, size_t index)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input)
        input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_TITLE,
                                &(vlc_value_t){ .i_int = index });
}

void
vlc_player_SelectNextTitle(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input)
        input_ControlPush(input->thread, INPUT_CONTROL_SET_TITLE_NEXT, NULL);
}

void
vlc_player_SelectPrevTitle(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input)
        input_ControlPush(input->thread, INPUT_CONTROL_SET_TITLE_PREV, NULL);
}

ssize_t
vlc_player_GetSelectedChapterIdx(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (!input)
        return -1;
    return input->chapter_selected;
}

void
vlc_player_SelectChapterIdx(vlc_player_t *player, size_t index)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input)
        input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_SEEKPOINT,
                                &(vlc_value_t){ .i_int = index });
}

void
vlc_player_SelectNextChapter(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input)
        input_ControlPush(input->thread, INPUT_CONTROL_SET_SEEKPOINT_NEXT, NULL);
}

void
vlc_player_SelectPrevChapter(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input)
        input_ControlPush(input->thread, INPUT_CONTROL_SET_SEEKPOINT_PREV, NULL);
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
        case VLC_INPUT_EVENT_VOUT_DELETED:
            action = VLC_PLAYER_LIST_REMOVED;
            break;
        default:
            vlc_assert_unreachable();
    }
    vlc_player_SendEvent(player, on_vout_list_changed, action, ev->vout);
}

static void
vlc_player_CancelWaitError(vlc_player_t *player)
{
    if (player->error_count == 0)
    {
        player->error_count = 0;
        vlc_cond_signal(&player->start_delay_cond);
    }
}

static bool
vlc_player_WaitRetryDelay(vlc_player_t *player)
{
    if (player->error_count)
    {
        /* Delay the next opening in case of error to avoid busy loops */
        vlc_tick_t delay = RETRY_TIMEOUT_BASE;
        for (unsigned i = 1; i < player->error_count
          && delay < RETRY_TIMEOUT_MAX; ++i)
            delay *= 2; /* Wait 100, 200, 400, 800, 1600 and finally 3200ms */
        delay += vlc_tick_now();

        while (player->error_count > 0
            && vlc_cond_timedwait(&player->start_delay_cond, &player->lock,
                                  delay) == 0);
        if (player->error_count == 0)
            return false; /* canceled */
    }
    return true;
}

static void
vlc_player_input_HandleDeadEvent(struct vlc_player_input *input)
{
    vlc_player_t *player = input->player;

    vlc_player_input_CloseAsync(input);

    if (input == player->input)
        player->input = NULL;
    else if (input == player->closing_input)
        player->closing_input = NULL;
    else
        return; /* XXX gapless case: next input closed */

    if (input->error != VLC_PLAYER_ERROR_NONE)
        player->error_count++;
    else
        player->error_count = 0;

    switch (player->media_ended_action)
    {
        case VLC_PLAYER_MEDIA_ENDED_STOP:
            break;
        case VLC_PLAYER_MEDIA_ENDED_EXIT:
            libvlc_Quit(player->obj.libvlc);
            break;
        case VLC_PLAYER_MEDIA_ENDED_PAUSE:
            break;
        case VLC_PLAYER_MEDIA_ENDED_CONTINUE:
            if (!vlc_player_WaitRetryDelay(player))
                break;
            if (vlc_player_OpenNextMedia(player) != VLC_SUCCESS)
                return;

            vlc_player_SendEvent(player, on_current_media_changed,
                                 player->media);

            vlc_player_input_Start(player->input);
            break;
    }
}

static void
vlc_player_input_HandleStateEvent(struct vlc_player_input *input,
                                  input_state_e state)
{
    vlc_player_t *player = input->player;

    switch (state)
    {
        case OPENING_S:
            input->state = VLC_PLAYER_STATE_STARTED;
            if (!player->next_media)
                vlc_player_GetNextMedia(player);
            break;
        case PLAYING_S:
            input->state = VLC_PLAYER_STATE_PLAYING;
            break;
        case PAUSE_S:
            input->state = VLC_PLAYER_STATE_PAUSED;
            break;
        case END_S:
            input->state = VLC_PLAYER_STATE_STOPPED;
            break;
        case ERROR_S:
            input->error = VLC_PLAYER_ERROR_GENERIC;
            vlc_player_SendEvent(player, on_error_changed, input->error);
            return;
        default:
            vlc_assert_unreachable();
    }

    bool send_event = true;
    /* Override the global state if the player has a next media to play, the
     * input is not stopped by the user (started = true) and if the player was
     * playing. */
    if (player->global_state == VLC_PLAYER_STATE_PLAYING)
    {
        switch (input->state)
        {
            case VLC_PLAYER_STATE_STOPPED:
                if (player->next_media)
                    send_event = false;
                break;
            case VLC_PLAYER_STATE_STARTED:
            case VLC_PLAYER_STATE_PLAYING:
                send_event = false;
                break;

            case VLC_PLAYER_STATE_PAUSED:
                break;
            default:
                vlc_assert_unreachable();
        }
    }
    if (send_event)
        player->global_state = input->state;

    if (send_event)
        vlc_player_SendEvent(player, on_state_changed, player->global_state);
}

static void
input_thread_events(input_thread_t *input_thread,
                    const struct vlc_input_event *event, void *user_data)
{
    struct vlc_player_input *input = user_data;
    vlc_player_t *player = input->player;

    assert(input_thread == input->thread);

    vlc_mutex_lock(&player->lock);

    switch (event->type)
    {
        case INPUT_EVENT_STATE:
            vlc_player_input_HandleStateEvent(input, event->state);
            break;
        case INPUT_EVENT_RATE:
            input->rate = event->rate;
            vlc_player_SendEvent(player, on_rate_changed, input->rate);
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
                vlc_player_OpenNextMedia(player);
#endif
            if (input->position_ms != event->position.ms ||
                input->position_percent != event->position.percentage)
            {
                input->position_ms = event->position.ms;
                input->position_percent = event->position.percentage;
                vlc_player_SendEvent(player, on_position_changed,
                                     input->position_ms,
                                     input->position_percent);
            }
            break;
        case INPUT_EVENT_LENGTH:
            if (input->length != event->length)
            {
                input->length = event->length;
                vlc_player_SendEvent(player, on_length_changed, input->length);
            }
            break;
        case INPUT_EVENT_PROGRAM:
            vlc_player_input_HandleProgramEvent(input, &event->program);
            break;
        case INPUT_EVENT_ES:
            vlc_player_input_HandleEsEvent(input, &event->es);
            break;
        case INPUT_EVENT_TITLE:
            vlc_player_input_HandleTitleEvent(input, &event->title);
            break;
        case INPUT_EVENT_CHAPTER:
            vlc_player_input_HandleChapterEvent(input, &event->chapter);
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
            vlc_player_SendEvent(player, on_buffering_changed, event->cache);
            break;
        case INPUT_EVENT_VOUT:
            vlc_player_input_HandleVoutEvent(input, &event->vout);
            break;
        case INPUT_EVENT_ITEM_META:
            vlc_player_SendEvent(player, on_media_meta_changed,
                                 input_GetItem(input->thread));
            break;
        case INPUT_EVENT_ITEM_EPG:
            vlc_player_SendEvent(player, on_media_epg_changed,
                                 input_GetItem(input->thread));
            break;
        case INPUT_EVENT_SUBITEMS:
            vlc_player_SendEvent(player, on_subitems_changed, event->subitems);
            break;
        case INPUT_EVENT_DEAD:
            vlc_player_input_HandleDeadEvent(input);
            break;
        default:
            break;
    }

    vlc_mutex_unlock(&player->lock);
}

static int
vlc_player_aout_cb(vlc_object_t *this, const char *var,
                   vlc_value_t oldval, vlc_value_t newval, void *data)
{
    vlc_player_t *player = data;

    vlc_player_Lock(player);
    if (strcmp(var, "volume") == 0)
    {
        if (oldval.f_float != newval.f_float)
            vlc_player_SendEvent(player, on_aout_volume_changed,
                                 (audio_output_t *)this, newval.f_float);
    }
    else if (strcmp(var, "mute") == 0)
    {
        if (oldval.b_bool != newval.b_bool )
            vlc_player_SendEvent(player, on_aout_mute_changed,
                                 (audio_output_t *)this, newval.b_bool);
    }
    else
        vlc_assert_unreachable();
    vlc_player_Unlock(player);

    return VLC_SUCCESS;
}

void
vlc_player_Delete(vlc_player_t *player)
{
    vlc_mutex_lock(&player->lock);

    if (player->input)
        vlc_player_input_StopAndClose(player->input, false);
#if GAPLESS
    if (player->next_input)
        vlc_player_input_StopAndClose(player->next_input, false);
#endif

    player->destructor.running = false;
    vlc_cond_signal(&player->destructor.cond);

    if (player->media)
        input_item_Release(player->media);
    if (player->next_media)
        input_item_Release(player->next_media);

    assert(vlc_list_is_empty(&player->listeners));

    vlc_mutex_unlock(&player->lock);

    vlc_join(player->destructor.thread, NULL);

    vlc_mutex_destroy(&player->lock);
    vlc_cond_destroy(&player->start_delay_cond);
    vlc_cond_destroy(&player->destructor.cond);

    audio_output_t *aout = vlc_player_GetAout(player);
    if (aout)
    {
        var_DelCallback(aout, "volume", vlc_player_aout_cb, player);
        var_DelCallback(aout, "mute", vlc_player_aout_cb, player);
        vlc_object_release(aout);
    }
    input_resource_Release(player->resource);
    if (player->renderer)
        vlc_renderer_item_release(player->renderer);

    vlc_object_release(player);
}

vlc_player_t *
vlc_player_New(vlc_object_t *parent,
               const struct vlc_player_media_provider *media_provider,
               void *media_provider_data)
{
    audio_output_t *aout = NULL;
    vlc_player_t *player = vlc_custom_create(parent, sizeof(*player), "player");
    if (!player)
        return NULL;

    assert(!media_provider || media_provider->get_next);

    vlc_list_init(&player->listeners);
    vlc_list_init(&player->destructor.inputs);
    player->media_ended_action = VLC_PLAYER_MEDIA_ENDED_CONTINUE;
    player->start_paused = false;
    player->renderer = NULL;
    player->media_provider = media_provider;
    player->media_provider_data = media_provider_data;
    player->media = NULL;
    player->input = player->closing_input = NULL;
    player->global_state = VLC_PLAYER_STATE_IDLE;

    player->error_count = 0;

    player->next_media = NULL;
#if GAPLESS
    player->next_input = NULL;
#endif

#define VAR_CREATE(var, flag) do { \
    if (var_Create(player, var, flag) != VLC_SUCCESS) \
        goto error; \
} while(0)

    VAR_CREATE("rate", VLC_VAR_FLOAT | VLC_VAR_DOINHERIT);
    VAR_CREATE("fullscreen", VLC_VAR_BOOL | VLC_VAR_DOINHERIT);
    VAR_CREATE("video-on-top", VLC_VAR_BOOL | VLC_VAR_DOINHERIT);
    VAR_CREATE("video-wallpaper", VLC_VAR_BOOL | VLC_VAR_DOINHERIT);
    /* TODO: Override these variables since the player handle media ended
     * action itself. */
    VAR_CREATE("start-paused", VLC_VAR_BOOL);
    VAR_CREATE("play-and-pause", VLC_VAR_BOOL);

#undef VAR_CREATE

    player->resource = input_resource_New(VLC_OBJECT(player));

    if (!player->resource)
        goto error;

    aout = input_resource_GetAout(player->resource);
    if (aout != NULL)
    {
        var_AddCallback(aout, "volume", vlc_player_aout_cb, player);
        var_AddCallback(aout, "mute", vlc_player_aout_cb, player);
        input_resource_PutAout(player->resource, aout);
    }

    player->destructor.running = true;
    vlc_mutex_init(&player->lock);
    vlc_cond_init(&player->start_delay_cond);
    vlc_cond_init(&player->destructor.cond);

    if (vlc_clone(&player->destructor.thread, destructor_thread, player,
                  VLC_THREAD_PRIORITY_LOW) != 0)
    {
        vlc_mutex_destroy(&player->lock);
        vlc_cond_destroy(&player->start_delay_cond);
        vlc_cond_destroy(&player->destructor.cond);
        goto error;
    }

    return player;

error:
    if (aout)
    {
        var_DelCallback(aout, "volume", vlc_player_aout_cb, player);
        var_DelCallback(aout, "mute", vlc_player_aout_cb, player);
    }
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

void
vlc_player_CondWait(vlc_player_t *player, vlc_cond_t *cond)
{
    vlc_player_assert_locked(player);
    vlc_cond_wait(cond, &player->lock);
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

    vlc_player_CancelWaitError(player);

#if GAPLESS
    if (player->next_input)
    {
        vlc_player_input_StopAndClose(player->next_input, false);
        player->next_input = NULL;
    }
#endif

    if (player->media)
    {
        input_item_Release(player->media);
        player->media = NULL;
    }
    if (player->next_media)
    {
        input_item_Release(player->next_media);
        player->next_media = NULL;
    }

    if (player->input)
        vlc_player_input_StopAndClose(player->input, player->input->started);

    if (player->closing_input)
    {
        /* This media will be opened when the input is finally closed */
        if (media)
            player->next_media = input_item_Hold(media);
        return VLC_SUCCESS;
    }

    int ret = VLC_SUCCESS;
    if (media)
    {
        player->media = input_item_Hold(media);
        player->input = vlc_player_input_New(player, player->media);

        if (!player->input)
        {
            input_item_Release(player->media);
            player->media = NULL;
            ret = VLC_ENOMEM;
        }
    }
    vlc_player_SendEvent(player, on_current_media_changed, player->media);
    return ret;
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

    if (!input)
        return VLC_EGENERIC;

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
    vlc_player_assert_locked(player);
    if (player->next_media)
    {
        input_item_Release(player->next_media);
        player->next_media = NULL;
    }
    vlc_player_GetNextMedia(player);

#if GAPLESS
    if (player->next_input)
    {
        /* Cause the get_next callback to be called when this input is
         * dead */
        vlc_player_input_StopAndClose(player->next_input, true);
        player->next_input = NULL;
    }
    vlc_player_CancelWaitError(player)
#endif
}

int
vlc_player_Start(vlc_player_t *player)
{
    vlc_player_assert_locked(player);

    if (player->closing_input)
        return VLC_SUCCESS;

    assert(player->media);

    vlc_player_CancelWaitError(player);

    if (!player->input)
    {
        /* Possible if the player was stopped by the user */
        player->input = vlc_player_input_New(player, player->media);

        if (!player->input)
            return VLC_EGENERIC;
    }
    if (player->input->started)
        return VLC_SUCCESS;

    if (player->start_paused)
    {
        var_Create(player->input->thread, "start-paused", VLC_VAR_BOOL);
        var_SetBool(player->input->thread, "start-paused", true);
    }

    return vlc_player_input_Start(player->input);
}

static void
vlc_player_CommonStop(vlc_player_t *player, bool async)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    vlc_player_CancelWaitError(player);

    if (!input)
        return;

    vlc_player_input_StopAndClose(input, async);

#if GAPLESS
    if (player->next_input)
    {
        vlc_player_input_StopAndClose(player->next_input, async);
        player->next_input = NULL;
    }
#endif
}

void
vlc_player_Stop(vlc_player_t *player)
{
    vlc_player_CommonStop(player, false);
}

void
vlc_player_RequestStop(vlc_player_t *player)
{
    vlc_player_CommonStop(player, true);
}

static void
vlc_player_SetPause(vlc_player_t *player, bool pause)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (!input || !input->started)
        return;

    vlc_value_t val = { .i_int = pause ? PAUSE_S : PLAYING_S };
    input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_STATE, &val);
}

void
vlc_player_Pause(vlc_player_t *player)
{
    vlc_player_SetPause(player, true);
}

void
vlc_player_Resume(vlc_player_t *player)
{
    vlc_player_SetPause(player, false);
}

enum vlc_player_state
vlc_player_GetState(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    return player->global_state;
}

enum vlc_player_error
vlc_player_GetError(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input ? input->error : VLC_PLAYER_ERROR_NONE;
}

int
vlc_player_GetCapabilities(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input ? input->capabilities : 0;
}

float
vlc_player_GetRate(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input)
        return input->rate;
    else
        return var_GetFloat(player, "rate");
}

void
vlc_player_ChangeRate(vlc_player_t *player, float rate)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (rate == 0.0)
        return;

    /* Save rate accross inputs */
    var_SetFloat(player, "rate", rate);

    if (input)
    {
        input_ControlPushHelper(input->thread, INPUT_CONTROL_SET_RATE,
            &(vlc_value_t) { .i_int = INPUT_RATE_DEFAULT / rate });
    }
}

static void
vlc_player_ChangeRateOffset(vlc_player_t *player, bool increment)
{
    static const float rates[] = {
        1.0/64, 1.0/32, 1.0/16, 1.0/8, 1.0/4, 1.0/3, 1.0/2, 2.0/3,
        1.0/1,
        3.0/2, 2.0/1, 3.0/1, 4.0/1, 8.0/1, 16.0/1, 32.0/1, 64.0/1,
    };
    float rate = vlc_player_GetRate(player) * (increment ? 1.1f : 0.9f);

    /* find closest rate (if any) in the desired direction */
    for (size_t i = 0; i < ARRAY_SIZE(rates); ++i)
    {
        if ((increment && rates[i] > rate) ||
            (!increment && rates[i] >= rate && i))
        {
            rate = increment ? rates[i] : rates[i-1];
            break;
        }
    }

    vlc_player_ChangeRate(player, rate);
}

void
vlc_player_IncrementRate(vlc_player_t *player)
{
    vlc_player_ChangeRateOffset(player, true);
}

void
vlc_player_DecrementRate(vlc_player_t *player)
{
    vlc_player_ChangeRateOffset(player, false);
}

vlc_tick_t
vlc_player_GetLength(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input ? input->length : VLC_TICK_INVALID;
}

vlc_tick_t
vlc_player_GetTime(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (!input || input->position_ms == VLC_TICK_INVALID)
        return VLC_TICK_INVALID;

    return input->position_ms;
}

float
vlc_player_GetPosition(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    return input ? input->position_percent : 0;
}

static inline void
vlc_player_assert_seek_params(enum vlc_player_seek_speed speed,
                            enum vlc_player_seek_whence whence)
{
    assert(speed == VLC_PLAYER_SEEK_PRECISE || speed == VLC_PLAYER_SEEK_FAST);
    assert(whence == VLC_PLAYER_SEEK_ABSOLUTE
        || whence == VLC_PLAYER_SEEK_RELATIVE);
    (void) speed; (void) whence;
}

void
vlc_player_SeekByPos(vlc_player_t *player, float position,
                     enum vlc_player_seek_speed speed,
                     enum vlc_player_seek_whence whence)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    vlc_player_assert_seek_params(speed, whence);

    int type = whence == VLC_PLAYER_SEEK_ABSOLUTE ? INPUT_CONTROL_SET_POSITION
                                                  : INPUT_CONTROL_JUMP_POSITION;
    if (input)
        input_ControlPush(input->thread, type,
            &(input_control_param_t) {
                .pos.f_val = position,
                .pos.b_fast_seek = speed == VLC_PLAYER_SEEK_FAST,
        });
}

void
vlc_player_SeekByTime(vlc_player_t *player, vlc_tick_t time,
                      enum vlc_player_seek_speed speed,
                      enum vlc_player_seek_whence whence)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    vlc_player_assert_seek_params(speed, whence);

    int type = whence == VLC_PLAYER_SEEK_ABSOLUTE ? INPUT_CONTROL_SET_TIME
                                                  : INPUT_CONTROL_JUMP_TIME;
    if (input)
        input_ControlPush(input->thread, type,
            &(input_control_param_t) {
                .time.i_val = time,
                .time.b_fast_seek = speed == VLC_PLAYER_SEEK_FAST,
        });
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

    if (!input)
        return;

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

    return input ? input->recording : false;
}

void
vlc_player_SetAudioDelay(vlc_player_t *player, vlc_tick_t delay,
                         bool absolute)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    if (input)
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
    return input ? input->audio_delay : 0;
}

void
vlc_player_SetSubtitleDelay(vlc_player_t *player, vlc_tick_t delay,
                            bool absolute)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (input)
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
    return input ? input->subtitle_delay : 0;
}

int
vlc_player_GetSignal(vlc_player_t *player, float *quality, float *strength)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (input && input->signal_quality >= 0 && input->signal_strength >= 0)
    {
        *quality = input->signal_quality;
        *strength = input->signal_strength;
        return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
}

int
vlc_player_GetStats(vlc_player_t *player, struct input_stats_t *stats)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);

    if (!input)
        return VLC_EGENERIC;

    *stats = input->stats;
    return VLC_SUCCESS;
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


bool
vlc_player_vout_IsFullscreen(vlc_player_t *player)
{
    return var_GetBool(player, "fullscreen");
}

void
vlc_player_vout_SetFullscreen(vlc_player_t *player, bool enabled)
{
    var_SetBool(player, "fullscreen", enabled);

    vout_thread_t **vouts;
    size_t count = vlc_player_GetVouts(player, &vouts);
    for (size_t i = 0; i < count; i++)
    {
        var_SetBool(vouts[i], "fullscreen", enabled);
        vlc_object_release(vouts[i]);
    }
    free(vouts);
}
