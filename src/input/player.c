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

struct vlc_player_es_priv
{
    struct vlc_player_es p;
    es_format_t fmt;
    char *title;
};
typedef struct VLC_VECTOR(struct vlc_player_es_priv *)
    vlc_player_es_vector;

static inline const struct vlc_player_es_priv *
vlc_player_es_get_owner(const struct vlc_player_es *es)
{
    return container_of(es, struct vlc_player_es_priv, p);
}

struct vlc_player_input
{
    input_thread_t *thread;
    vlc_player_t *player;
    bool started;

    input_state_e state;
    float rate;
    int capabilities;
    vlc_tick_t length;

    vlc_tick_t position_ms;
    float position_percent;
    vlc_tick_t position_date;

    vlc_player_program_vector program_vector;
    vlc_player_es_vector video_es_vector;
    vlc_player_es_vector audio_es_vector;
    vlc_player_es_vector spu_es_vector;

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

    input_item_t *item;
    struct vlc_player_input *input;

#if GAPLESS
    input_item_t *next_item;
    struct vlc_player_input *next_input;
#endif

    struct
    {
        bool running;
        vlc_thread_t collector;
        vlc_cond_t cond;
        struct vlc_list inputs;
        size_t wait_count;
    } garbage;

};

static void
input_thread_events(input_thread_t *, const struct vlc_input_event *, void *);

static inline void
vlc_player_assert_locked(vlc_player_t *player)
{
    vlc_assert_locked(&player->lock);
}

static struct vlc_player_input *
vlc_player_input_Create(vlc_player_t *player, input_item_t *item)
{
    struct vlc_player_input *input = malloc(sizeof(*input));
    if (!input)
        return NULL;

    input->player = player;
    input->started = false;

    input->state = INIT_S;
    input->rate = 1.f;
    input->capabilities = 0;
    input->length = input->position_ms =
    input->position_date = VLC_TICK_INVALID;
    input->position_percent = 0.f;

    vlc_vector_init(&input->program_vector);
    vlc_vector_init(&input->video_es_vector);
    vlc_vector_init(&input->audio_es_vector);
    vlc_vector_init(&input->spu_es_vector);

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
vlc_player_input_Destroy(struct vlc_player_input *input)
{
    assert(input->program_vector.size == 0);
    assert(input->video_es_vector.size == 0);
    assert(input->audio_es_vector.size == 0);
    assert(input->spu_es_vector.size == 0);

    vlc_vector_clear(&input->program_vector);
    vlc_vector_clear(&input->video_es_vector);
    vlc_vector_clear(&input->audio_es_vector);
    vlc_vector_clear(&input->spu_es_vector);

    input_Close(input->thread);
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
vlc_player_input_StopAndClose(struct vlc_player_input *input)
{
    if (input->started)
    {
        input->started = false;
        input_Stop(input->thread);

        input->player->garbage.wait_count++;
        /* This input will be cleaned when we receive the INPUT_EVENT_DEAD
         * event */
    }
    else
        vlc_player_input_Destroy(input);
}

static void *
garbage_collector(void *data)
{
    vlc_player_t *player = data;
    struct vlc_player_input *input;

    vlc_mutex_lock(&player->lock);
    while (player->garbage.running || player->garbage.wait_count > 0)
    {
        while (player->garbage.running
            && vlc_list_is_empty(&player->garbage.inputs))
            vlc_cond_wait(&player->garbage.cond, &player->lock);

        vlc_list_foreach(input, &player->garbage.inputs, node)
        {
            vlc_player_input_Destroy(input);
            vlc_list_remove(&input->node);
            player->garbage.wait_count--;
        }
    }
    vlc_mutex_unlock(&player->lock);
    return NULL;
}

static void
vlc_player_SendNewPlaybackEvent(vlc_player_t *player, input_item_t *new_item)
{
    vlc_player_assert_locked(player);

    if (player->owner_cbs->on_current_playback_changed)
        player->owner_cbs->on_current_playback_changed(player, new_item,
                                                     player->owner_cbs_data);
}

static input_item_t *
vlc_player_GetNextItem(vlc_player_t *player)
{
    vlc_player_assert_locked(player);

    return player->owner_cbs->get_next_item ?
           player->owner_cbs->get_next_item(player, player->owner_cbs_data) :
           NULL;
}

static void
vlc_player_SendInputEvent(vlc_player_t *player,
                          const struct vlc_input_event *event)
{
    struct vlc_player_listener_id *listener;
    vlc_list_foreach(listener, &player->listeners, node)
    {
        if (listener->cbs->on_input_event)
            listener->cbs->on_input_event(player, player->item, event,
                                          listener->cbs_data);
    }
}

static int
vlc_player_OpenNextItem(vlc_player_t *player)
{
    assert(player->input == NULL);

    if (player->item)
        input_item_Release(player->item);
    player->item = vlc_player_GetNextItem(player);

    if (player->item)
    {
        player->input = vlc_player_input_Create(player, player->item);

        return player->input ? VLC_SUCCESS : VLC_EGENERIC;
    }
    return VLC_EGENERIC;
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
    vlc_player_assert_locked(player);
    assert(player->input);

    return player->input->program_vector.size;
}

const struct vlc_player_program *
vlc_player_GetProgramAt(vlc_player_t *player, size_t index)
{
    vlc_player_assert_locked(player);
    assert(player->input);

    assert(index < player->input->program_vector.size);
    return &player->input->program_vector.data[index]->p;
}

const struct vlc_player_program *
vlc_player_GetProgram(vlc_player_t *player, int id)
{
    vlc_player_assert_locked(player);
    assert(player->input);

    struct vlc_player_program_priv *prgm =
        vlc_player_program_vector_FindById(&player->input->program_vector, id,
                                           NULL);
    return prgm ? &prgm->p : NULL;
}

void
vlc_player_SelectProgram(vlc_player_t *player, int id)
{
    vlc_player_assert_locked(player);
    assert(player->input != NULL);

    input_ControlPushHelper(player->input->thread, INPUT_CONTROL_SET_PROGRAM,
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

static inline vlc_player_es_vector *
vlc_player_input_GetEsVector(struct vlc_player_input *input,
                               enum es_format_category_e i_cat)
{
    switch (i_cat)
    {
        case VIDEO_ES:
            return &input->video_es_vector;
        case AUDIO_ES:
            return &input->audio_es_vector;
        case SPU_ES:
            return &input->spu_es_vector;
        default:
            return NULL;
    }
}

static struct vlc_player_es_priv *
vlc_player_es_vector_FindById(vlc_player_es_vector *vec, vlc_es_id_t *id,
                              size_t *idx)
{
    for (size_t i = 0; i < vec->size; ++i)
    {
        struct vlc_player_es_priv *es = vec->data[i];
        if (es->p.id == id)
        {
            if (idx)
                *idx = i;
            return es;
        }
    }
    return NULL;
}

size_t
vlc_player_GetEsCount(vlc_player_t *player, enum es_format_category_e cat)
{
    vlc_player_assert_locked(player);
    assert(player->input);

    vlc_player_es_vector *vec =
        vlc_player_input_GetEsVector(player->input, cat);
    if (vec)
        return 0;
    return vec->size;
}

const struct vlc_player_es *
vlc_player_GetEsAt(vlc_player_t *player, enum es_format_category_e cat,
                   size_t index)
{
    vlc_player_assert_locked(player);
    assert(player->input);

    vlc_player_es_vector *vec =
        vlc_player_input_GetEsVector(player->input, cat);
    if (!vec)
        return NULL;
    assert(index < vec->size);
    return &vec->data[index]->p;
}

const struct vlc_player_es *
vlc_player_GetEs(vlc_player_t *player, vlc_es_id_t *id)
{
    vlc_player_assert_locked(player);
    struct vlc_player_input *input = player->input;
    assert(input);

    vlc_player_es_vector *vec =
        vlc_player_input_GetEsVector(input, vlc_es_id_GetCat(id));
    if (!vec)
        return NULL;
    struct vlc_player_es_priv *es = vlc_player_es_vector_FindById(vec, id,
                                                                   NULL);
    return es ? &es->p : NULL;
}

void
vlc_player_SelectEs(vlc_player_t *player, vlc_es_id_t *id)
{
    vlc_player_assert_locked(player);
    assert(player->input != NULL);

    input_ControlPushEsHelper(player->input->thread, INPUT_CONTROL_SET_ES, id);
}

void
vlc_player_UnselectEs(vlc_player_t *player, vlc_es_id_t *id)
{
    vlc_player_assert_locked(player);
    assert(player->input != NULL);

    input_ControlPushEsHelper(player->input->thread, INPUT_CONTROL_UNSET_ES,
                              id);
}

void
vlc_player_RestartEs(vlc_player_t *player, vlc_es_id_t *id)
{
    vlc_player_assert_locked(player);
    assert(player->input != NULL);

    input_ControlPushEsHelper(player->input->thread, INPUT_CONTROL_RESTART_ES,
                              id);
}

static struct vlc_player_es_priv *
vlc_player_es_Create(vlc_es_id_t *id, const char *title, const es_format_t *fmt)
{
    struct vlc_player_es_priv *es = malloc(sizeof(*es));
    if (!es)
        return NULL;
    es->title = strdup(title);
    if (!es->title)
    {
        free(es);
        return NULL;
    }

    int ret = es_format_Copy(&es->fmt, fmt);
    if (ret != VLC_SUCCESS)
    {
        free(es->title);
        free(es);
        return NULL;
    }
    es->p.id = vlc_es_id_Hold(id);
    es->p.fmt = &es->fmt;
    es->p.title = es->title;
    es->p.selected = false;

    return es;
}

static void
vlc_player_es_Destroy(struct vlc_player_es_priv *es)
{
    es_format_Clean(&es->fmt);
    vlc_es_id_Release(es->p.id);
    free(es->title);
    free(es);
}

static int
vlc_player_es_Update(struct vlc_player_es_priv *es, const char *title,
                     const es_format_t *fmt)
{
    if (strcmp(title, es->title) != 0)
    {
        char *dup = strdup(title);
        if (!dup)
            return VLC_ENOMEM;
        free(es->title);
        es->title = dup;
    }

    es_format_t fmtdup;
    int ret = es_format_Copy(&fmtdup, fmt);
    if (ret != VLC_SUCCESS)
        return ret;

    es_format_Clean(&es->fmt);
    es->fmt = fmtdup;
    return VLC_SUCCESS;
}

static int
vlc_player_input_HandleEsEvent(struct vlc_player_input *input,
                               const struct vlc_input_event_es *ev)
{
    assert(ev->id && ev->title && ev->fmt);

    struct vlc_player_es_priv *es;
    vlc_player_es_vector *vec =
        vlc_player_input_GetEsVector(input, ev->fmt->i_cat);

    if (!vec)
        return VLC_EGENERIC; /* UNKNOWN_ES or DATA_ES not handled */

    switch (ev->action)
    {
        case VLC_INPUT_ES_ADDED:
            es = vlc_player_es_Create(ev->id, ev->title, ev->fmt);
            if (!es)
                return VLC_ENOMEM;

            if (vlc_vector_push(vec, es))
                return VLC_SUCCESS;
            vlc_player_es_Destroy(es);
            return VLC_ENOMEM;
        case VLC_INPUT_ES_DELETED:
        {
            size_t idx;
            es = vlc_player_es_vector_FindById(vec, ev->id, &idx);
            if (es)
            {
                vlc_vector_remove(vec, idx);
                vlc_player_es_Destroy(es);
                return VLC_SUCCESS;
            }
            return VLC_EGENERIC;

        }
        case VLC_INPUT_ES_UPDATED:
            es = vlc_player_es_vector_FindById(vec, ev->id, NULL);
            if (es)
                return vlc_player_es_Update(es, ev->title, ev->fmt);
            return VLC_EGENERIC;
        case VLC_INPUT_ES_SELECTED:
        case VLC_INPUT_ES_UNSELECTED:
            es = vlc_player_es_vector_FindById(vec, ev->id, NULL);
            if (es)
            {
                es->p.selected = ev->action == VLC_INPUT_ES_SELECTED;
                return VLC_SUCCESS;
            }
            return VLC_EGENERIC;
        default:
            vlc_assert_unreachable();
    }
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
        if (event->type == INPUT_EVENT_DEAD)
        {
            /* Don't increment wait_count: already done by
             * vlc_player_input_StopAndClose() */
            vlc_list_append(&input->node, &player->garbage.inputs);
            vlc_cond_signal(&player->garbage.cond);
        }

        vlc_mutex_unlock(&player->lock);
        return;
    }

    bool skip_event = false;
    switch (event->type)
    {
        case INPUT_EVENT_STATE:
            input->state = event->state;
            break;
        case INPUT_EVENT_RATE:
            input->rate = event->rate;
            break;
        case INPUT_EVENT_CAPABILITIES:
            input->capabilities = event->capabilities;
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
            break;
        case INPUT_EVENT_LENGTH:
            input->length = event->length;
            break;
        case INPUT_EVENT_PROGRAM:
            if (vlc_player_input_HandleProgramEvent(input, &event->program))
                skip_event = true;
            break;
        case INPUT_EVENT_ES:
            if (vlc_player_input_HandleEsEvent(input, &event->es))
                skip_event = true;
            break;
        case INPUT_EVENT_DEAD:
            input->started = false;
            vlc_list_append(&input->node, &player->garbage.inputs);
            vlc_cond_signal(&player->garbage.cond);
            player->garbage.wait_count++;

            /* XXX: for now, play only one input at a time */
            if (likely(input == player->input))
            {
                player->input = NULL;
                if (vlc_player_OpenNextItem(player) == VLC_SUCCESS)
                {
                    vlc_player_SendNewPlaybackEvent(player, player->item);
                    vlc_player_input_Start(player->input);
                    skip_event = true;
                }
            }
            break;
        default:
            break;
    }

    if (!skip_event)
        vlc_player_SendInputEvent(player, event);
    vlc_mutex_unlock(&player->lock);
}

void
vlc_player_Delete(vlc_player_t *player)
{
    vlc_mutex_lock(&player->lock);

    if (player->input)
        vlc_player_input_StopAndClose(player->input);
#if GAPLESS
    if (player->next_input)
        vlc_player_input_StopAndClose(player->next_input);
#endif

    player->garbage.running = false;
    vlc_cond_signal(&player->garbage.cond);

    if (player->item)
        input_item_Release(player->item);
#if GAPLESS
    if (player->next_item)
        input_item_Release(player->next_item);
#endif

    assert(vlc_list_is_empty(&player->listeners));

    vlc_mutex_unlock(&player->lock);

    vlc_join(player->garbage.collector, NULL);

    vlc_mutex_destroy(&player->lock);
    vlc_cond_destroy(&player->garbage.cond);

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

    vlc_list_init(&player->garbage.inputs);
    player->renderer = NULL;
    player->owner_cbs = owner_cbs;
    player->owner_cbs_data = owner_cbs_data;
    player->item = NULL;
    player->input = NULL;
#if GAPLESS
    player->next_item = NULL;
    player->next_input = NULL;
#endif
    player->resource = input_resource_New(VLC_OBJECT(player));

    if (!player->resource)
        goto error;

    player->garbage.running = true;
    vlc_mutex_init(&player->lock);
    vlc_cond_init(&player->garbage.cond);

    if (vlc_clone(&player->garbage.collector, garbage_collector, player,
                  VLC_THREAD_PRIORITY_LOW) != 0)
    {
        vlc_mutex_destroy(&player->lock);
        vlc_cond_destroy(&player->garbage.cond);
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
vlc_player_OpenItem(vlc_player_t *player, input_item_t *item)
{
    vlc_player_assert_locked(player);

    bool was_started = false;
    if (player->input)
    {
        was_started = player->input->started;
        vlc_player_input_StopAndClose(player->input);
        player->input = NULL;
    }
#if GAPLESS
    if (player->next_input)
    {
        vlc_player_input_StopAndClose(player->next_input);
        player->next_input = NULL;
    }
#endif

    if (player->item)
    {
        input_item_Release(player->item);
        player->item = NULL;
    }
#if GAPLESS
    if (player->next_item)
    {
        input_item_Release(player->next_item);
        player->next_item = NULL;
    }
#endif

    player->item = input_item_Hold(item);
    player->input = vlc_player_input_Create(player, player->item);

    int ret = player->input ? VLC_SUCCESS : VLC_ENOMEM;
    if (ret == VLC_SUCCESS && was_started)
        ret = vlc_player_input_Start(player->input);

    if (ret != VLC_SUCCESS)
    {
        if (player->input)
        {
            vlc_player_input_StopAndClose(player->input);
            player->input = NULL;
        }
        input_item_Release(player->item);
        player->item = NULL;
    }
    return ret;
}


void
vlc_player_InvalidateNext(vlc_player_t *player)
{
    (void) player;
#if GAPLESS
    vlc_player_assert_locked(player);
    if (player->next_item)
    {
        input_item_Release(player->next_item);
        player->next_item = vlc_player_GetNextItem(player);
    }
    if (player->next_input)
    {
        /* Cause the get_next_item callback to be called when this input is
         * dead */
        vlc_player_input_StopAndClose(player->input);
        player->next_input = NULL;
    }
#endif
}

int
vlc_player_Start(vlc_player_t *player)
{
    vlc_player_assert_locked(player);

    if (!player->input)
    {
        /* Possible if the player was stopped by the user */
        assert(player->item);
        player->input = vlc_player_input_Create(player, player->item);

        if (!player->input)
            return VLC_EGENERIC;
    }
    assert(!player->input->started);

    return vlc_player_input_Start(player->input);
}

void
vlc_player_Stop(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    assert(player->input && player->input->started);

    vlc_player_input_StopAndClose(player->input);
    player->input = NULL;

#if GAPLESS
    if (player->next_input)
    {
        vlc_player_input_StopAndClose(player->next_input);
        player->next_input = NULL;
    }
#endif
}

void
vlc_player_Resume(vlc_player_t *player)
{
    vlc_player_assert_locked(player);

    assert(player->input && player->input->started);
    assert(player->input->capabilities & VLC_INPUT_CAPABILITIES_PAUSEABLE);

    input_ControlPushHelper(player->input->thread, INPUT_CONTROL_SET_STATE,
                            &(vlc_value_t) { .i_int = PLAYING_S });
}

void
vlc_player_Pause(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    assert(player->input && player->input->started);
    assert(player->input->capabilities & VLC_INPUT_CAPABILITIES_PAUSEABLE);

    input_ControlPushHelper(player->input->thread, INPUT_CONTROL_SET_STATE,
                            &(vlc_value_t) {.i_int = PAUSE_S});
}

bool
vlc_player_IsStarted(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    return player->input && player->input->started;
}

bool
vlc_player_IsPlaying(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    return player->input && player->input->state == PLAYING_S;
}

int
vlc_player_GetCapabilities(vlc_player_t *player)
{
    vlc_player_assert_locked(player);
    assert(player->input);
    return player->input->capabilities;
}

input_item_t *
vlc_player_GetCurrentItem(vlc_player_t *player)
{
    vlc_player_assert_locked(player);

    return player->item;
}

void
vlc_player_SetRenderer(vlc_player_t *player, vlc_renderer_item_t *renderer)
{
    vlc_player_assert_locked(player);

    if (player->renderer)
        vlc_renderer_item_release(player->renderer);
    player->renderer = vlc_renderer_item_hold(renderer);

    if (player->input)
        input_Control(player->input->thread, INPUT_SET_RENDERER,
                      player->renderer);
#if GAPLESS
    if (player->next_input)
        input_Control(player->next_input->thread, INPUT_SET_RENDERER,
                      player->renderer);
#endif
}
