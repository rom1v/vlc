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

#include "libvlc.h"
#include "input_internal.h"

struct vlc_player_listener_id
{
    struct vlc_player_cbs   cbs;
    void *                  cbs_data;
    struct vlc_list         node;
};

struct vlc_player_program_owner
{
    struct vlc_player_program   p;
    struct vlc_list             node;
    char                        title[];
};

struct vlc_player_es_owner
{
    struct vlc_player_es    p;
    struct vlc_list         node;
    char                    title[];
};

struct vlc_player_teletext_owner
{
    struct vlc_player_teletext  p;
    struct vlc_list             node;
    char                        title[];
};

struct vlc_player_input
{
    input_thread_t *    thread;
    vlc_player_t *      player;
    bool                started;

    input_state_e       state;
    float               rate;
    int                 capabilities;
    vlc_tick_t          length;

    vlc_tick_t          position_ms;
    float               position_percent;
    vlc_tick_t          position_date;

    struct vlc_list     programs;
    struct vlc_list     eses;
    struct vlc_list     teletexts;

    struct vlc_list     node;
};

struct vlc_player_private
{
    vlc_player_t                player;
    vlc_mutex_t                 lock;

    struct vlc_player_cbs       owner_cbs;
    void *                      owner_cbs_data;

    struct vlc_list             listeners;

    input_resource_t *          resource;
    vlc_renderer_item_t *       renderer;

    input_item_t *              item;
    input_item_t *              next_item;

    struct vlc_player_input *   input;
    struct vlc_player_input *   next_input;

    struct
    {
        bool                running;
        vlc_thread_t        collector;
        vlc_cond_t          cond;
        struct vlc_list     inputs;
        size_t              wait_count;
    } garbage;

};

static void
input_thread_events(input_thread_t *, void *, const struct vlc_input_event *);

static inline struct vlc_player_private *
vlc_player_priv(vlc_player_t *player)
{
    return container_of(player, struct vlc_player_private, player);
}

static inline struct vlc_player_private *
vlc_player_priv_locked(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv(player);
    assert(vlc_mutex_trylock(&priv->lock) != 0);
    return priv;
}

static void *
garbage_collector(void *data)
{
    vlc_player_t *player = data;
    struct vlc_player_private *priv = vlc_player_priv(player);
    struct vlc_player_input *input;

    vlc_mutex_lock(&priv->lock);
    while (priv->garbage.running || priv->garbage.wait_count > 0)
    {
        while (priv->garbage.running
            && vlc_list_is_empty(&priv->garbage.inputs))
            vlc_cond_wait(&priv->garbage.cond, &priv->lock);

        vlc_list_foreach(input, &priv->garbage.inputs, node)
        {
            input_Close(input->thread);
            free(input);
            priv->garbage.wait_count--;
        }
    }
    vlc_mutex_unlock(&priv->lock);
    return NULL;
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
        struct vlc_player_private *priv = vlc_player_priv(input->player);

        input->started = false;
        input_Stop(input->thread);

        priv->garbage.wait_count++;
    }
    else
    {
        input_Close(input->thread);
        free(input);
    }
}

static struct vlc_player_input *
vlc_player_input_Create(vlc_player_t *player, input_item_t *item)
{
    struct vlc_player_private *priv = vlc_player_priv(player);
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

    input->thread = input_Create(player, input_thread_events, input, item,
                                 NULL, priv->resource, priv->renderer);
    if (!input->thread)
    {
        free(input);
        return NULL;
    }
    return input;
}

static void
vlc_player_SendNewItemEvent(vlc_player_t *player, input_item_t *new_item)
{
    struct vlc_player_private *priv = vlc_player_priv(player);

    if (priv->owner_cbs.on_new_item_played)
        priv->owner_cbs.on_new_item_played(player, priv->owner_cbs_data,
                                           new_item);

    struct vlc_player_listener_id *listener;
    vlc_list_foreach(listener, &priv->listeners, node)
    {
        if (listener->cbs.on_new_item_played)
            listener->cbs.on_new_item_played(player, listener->cbs_data,
                                             new_item);
    }
}

static void
vlc_player_SendInputEvent(vlc_player_t *player,
                          const struct vlc_input_event *event)
{
    struct vlc_player_private *priv = vlc_player_priv(player);

    if (priv->owner_cbs.on_input_event)
        priv->owner_cbs.on_input_event(player, priv->owner_cbs_data, event);

    struct vlc_player_listener_id *listener;
    vlc_list_foreach(listener, &priv->listeners, node)
    {
        if (listener->cbs.on_input_event)
            listener->cbs.on_input_event(player, listener->cbs_data, event);
    }
}

static int
vlc_player_OpenNextItem(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv(player);
    assert(priv->input == NULL);

    if (priv->next_item)
    {
        if (priv->item)
            input_item_Release(priv->item);
        priv->item = priv->next_item;
        priv->next_item = NULL;

        priv->input = vlc_player_input_Create(player, priv->item);
        if (priv->input)
            vlc_player_SendNewItemEvent(player, priv->item);

        return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
}

void
vlc_player_program_FreeArray(struct vlc_player_program *prgms, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        free(prgms[i].title);
    free(prgms);
}

ssize_t
vlc_player_GetProgramsArray(vlc_player_t *player, int id, bool selected_only,
                            struct vlc_player_program **prgms_out)
{
#define PROGRAM_DROP(prgm, id, selected_only) \
    (selected_only && !prgm->p.selected) || \
    (id >= 0 && id != prgm->p.id)

    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    struct vlc_player_input *input = priv->input;
    size_t count = 0, i = 0;
    struct vlc_player_program_owner *it;

    vlc_list_foreach(it, &input->programs, node)
    {
        if (PROGRAM_DROP(it, id, selected_only))
            continue;
        count++;
    }

    if (count == 0)
        return 0;

    struct vlc_player_program *prgms = vlc_alloc(count, sizeof(*prgms));
    if (!prgms)
        return -1;

    vlc_list_foreach(it, &input->programs, node)
    {
        if (PROGRAM_DROP(it, id, selected_only))
            continue;
        prgms[i].title = strdup(it->p.title);
        if (!prgms[i].title)
        {
            vlc_player_program_FreeArray(prgms, i);
            return -1;
        }
        i++;
    }
    assert(count == i);
    *prgms_out = prgms;
    return count;
#undef PROGRAM_DROP
}

void
vlc_player_SetProgram(vlc_player_t *player, int id)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    assert(priv->input != NULL);

    input_ControlPushHelper(priv->input->thread, INPUT_CONTROL_SET_PROGRAM,
                            &(vlc_value_t) { .i_int = id });
}

static int
vlc_player_input_handle_program_event(struct vlc_player_input *input,
                                      const struct vlc_input_event *event)
{
    assert(event->type == INPUT_EVENT_PROGRAM);
    struct vlc_player_program_owner *prgm;

    switch (event->program.action)
    {
        case VLC_INPUT_PROGRAM_ADDED:
        {
            size_t title_len = strlen(event->program.title);
            prgm = malloc(sizeof(*prgm) + title_len + 1);
            if (!prgm)
                return VLC_ENOMEM;
            prgm->p.id = event->program.id;
            strcpy(prgm->title, event->program.title);
            prgm->title[title_len] = 0;
            prgm->p.title = prgm->title;
            prgm->p.selected = prgm->p.scrambled = false;
            vlc_list_append(&prgm->node, &input->programs);
            return VLC_SUCCESS;
        }
        case VLC_INPUT_PROGRAM_DELETED:
            vlc_list_foreach(prgm, &input->programs, node)
                if (event->program.id == prgm->p.id)
                {
                    vlc_list_remove(&prgm->node);
                    free(prgm);
                    return VLC_SUCCESS;
                }
            return VLC_EGENERIC;
        case VLC_INPUT_PROGRAM_SELECTED:
        {
            int ret = VLC_EGENERIC;
            vlc_list_foreach(prgm, &input->programs, node)
                if (event->program.id == prgm->p.id)
                {
                    prgm->p.selected = true;
                    ret = VLC_SUCCESS;
                }
                else
                    prgm->p.selected = false;
            return ret;
        }
        case VLC_INPUT_PROGRAM_SCRAMBLED:
            vlc_list_foreach(prgm, &input->programs, node)
                if (event->program.id == prgm->p.id)
                {
                    prgm->p.scrambled = event->program.scrambled;
                    return VLC_SUCCESS;
                }
            return VLC_EGENERIC;
        default:
            vlc_assert_unreachable();
    }
}

static void
vlc_player_es_clean(struct vlc_player_es *es)
{
    es_format_Clean(&es->fmt);
    free(es->title);
}

static int
vlc_player_es_copy(struct vlc_player_es *dst, const struct vlc_player_es *src)
{
    int ret = es_format_Copy(&dst->fmt, &src->fmt);
    if (ret != VLC_SUCCESS)
        return ret;

    dst->title = strdup(src->title);
    if (!dst->title)
    {
        es_format_Clean(&dst->fmt);
        return VLC_ENOMEM;
    }
    return VLC_SUCCESS;
}

void
vlc_player_es_FreeArray(struct vlc_player_es *eses, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        vlc_player_es_clean(&eses[i]);
    free(eses);
}

ssize_t
vlc_player_GetEsesArray(vlc_player_t *player, enum es_format_category_e cat,
                        int id, bool selected_only,
                        struct vlc_player_es **eses_out)
{
#define ES_DROP(es, cat, id, selected_only) \
    (selected_only && !es->p.selected) || \
    (cat != UNKNOWN_ES && cat != es->p.fmt.i_cat) || \
    (id >= 0 && id != es->p.fmt.i_id)

    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    struct vlc_player_input *input = priv->input;
    size_t count = 0, i = 0;
    struct vlc_player_es_owner *it;

    vlc_list_foreach(it, &input->eses, node)
    {
        if (ES_DROP(it, cat, id, selected_only))
            continue;
        count++;
    }

    if (count == 0)
        return 0;

    struct vlc_player_es *eses = vlc_alloc(count, sizeof(*eses));
    if (!eses)
        return -1;

    vlc_list_foreach(it, &input->eses, node)
    {
        if (ES_DROP(it, cat, id, selected_only))
            continue;
        if (vlc_player_es_copy(&eses[i], &it->p) != VLC_SUCCESS)
        {
            vlc_player_es_FreeArray(eses, i);
            return -1;
        }
        i++;
    }
    assert(count == i);
    *eses_out = eses;
    return count;
#undef ES_DROP
}

void
vlc_player_SetEs(vlc_player_t *player, enum es_format_category_e cat, int id)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    assert(priv->input != NULL);

    if (id < 0)
        id = -cat;
    input_ControlPushHelper(priv->input->thread, INPUT_CONTROL_SET_ES,
                            &(vlc_value_t) { .i_int = id });
}

static int
vlc_player_input_handle_es_event(struct vlc_player_input *input,
                                 const struct vlc_input_event *event)
{
    assert(event->type == INPUT_EVENT_ES);
    struct vlc_player_es_owner *es;

    switch (event->es.action)
    {
        case VLC_INPUT_ES_ADDED:
        {
            size_t title_len = strlen(event->es.added.title);
            es = malloc(sizeof(*es) + title_len + 1);
            if (!es)
                return VLC_ENOMEM;

            int ret = es_format_Copy(&es->p.fmt, event->es.added.fmt);
            if (ret != VLC_SUCCESS)
            {
                free(es);
                return ret;
            }
            strcpy(es->title, event->es.added.title);
            es->title[title_len] = 0;
            es->p.title = es->title;
            es->p.selected = false;
            vlc_list_append(&es->node, &input->eses);
            return VLC_SUCCESS;
        }
        case VLC_INPUT_ES_DELETED:
            vlc_list_foreach(es, &input->eses, node)
                if (event->es.deleted.id == es->p.fmt.i_id)
                {
                    vlc_list_remove(&es->node);
                    es_format_Clean(&es->p.fmt);
                    free(es);
                    return VLC_SUCCESS;
                }
            return VLC_EGENERIC;
        case VLC_INPUT_ES_UPDATED:
            vlc_list_foreach(es, &input->eses, node)
                if (event->es.updated.fmt->i_id == es->p.fmt.i_id)
                {
                    es_format_Clean(&es->p.fmt);
                    return es_format_Copy(&es->p.fmt, event->es.updated.fmt);
                }
            return VLC_EGENERIC;
        case VLC_INPUT_ES_SELECTED:
            if (event->es.selected.id < 0)
            {
                vlc_list_foreach(es, &input->eses, node)
                    if (event->es.selected.cat == es->p.fmt.i_cat)
                        es->p.selected = false;
                return VLC_SUCCESS;
            }
            else
            {
                int ret = VLC_EGENERIC;
                vlc_list_foreach(es, &input->eses, node)
                    if (event->es.selected.id == es->p.fmt.i_id)
                    {
                        es->p.selected = true;
                        ret = VLC_SUCCESS;
                    }
                    else
                        es->p.selected = false;
                return ret;
            }
        default:
            vlc_assert_unreachable();
    }
}

void
vlc_player_teletext_FreeArray(struct vlc_player_teletext *tts, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        free(tts[i].title);
    free(tts);
}

ssize_t
vlc_player_GetTeletextsArray(vlc_player_t *player, int id, bool selected_only,
                             struct vlc_player_teletext **tts_out)
{
#define TELETEXT_DROP(tt, id, selected_only) \
    (selected_only && !tt->p.selected) || \
    (id >= 0 && id != tt->p.id)

    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    struct vlc_player_input *input = priv->input;
    size_t count = 0, i = 0;
    struct vlc_player_teletext_owner *it;

    vlc_list_foreach(it, &input->teletexts, node)
    {
        if (TELETEXT_DROP(it, id, selected_only))
            continue;
        count++;
    }

    if (count == 0)
        return 0;

    struct vlc_player_teletext *tts = vlc_alloc(count, sizeof(*tts));
    if (!tts)
        return -1;

    vlc_list_foreach(it, &input->teletexts, node)
    {
        if (TELETEXT_DROP(it, id, selected_only))
            continue;
        tts[i].title = strdup(it->p.title);
        if (!tts[i].title)
        {
            vlc_player_teletext_FreeArray(tts, i);
            return -1;
        }
        i++;
    }
    assert(count == i);
    *tts_out = tts;
    return count;
#undef TELETEXT_DROP
}

static int
vlc_player_input_handle_teletext_event(struct vlc_player_input *input,
                                       const struct vlc_input_event *event)
{
    assert(event->type == INPUT_EVENT_TELETEXT);
    struct vlc_player_teletext_owner *tt;

    switch (event->teletext.action)
    {
        case VLC_INPUT_TELETEXT_ADDED:
        {
            size_t title_len = strlen(event->teletext.title);
            tt = malloc(sizeof(*tt) + title_len + 1);
            if (!tt)
                return VLC_ENOMEM;
            tt->p.id = event->teletext.id;
            strcpy(tt->title, event->teletext.title);
            tt->title[title_len] = 0;
            tt->p.title = tt->title;
            tt->p.selected = false;
            vlc_list_append(&tt->node, &input->teletexts);
            return VLC_SUCCESS;
        }
        case VLC_INPUT_TELETEXT_DELETED:
            vlc_list_foreach(tt, &input->teletexts, node)
                if (event->teletext.id == tt->p.id)
                {
                    vlc_list_remove(&tt->node);
                    free(tt);
                    return VLC_SUCCESS;
                }
            return VLC_EGENERIC;
        case VLC_INPUT_TELETEXT_SELECTED:
        {
            int ret = event->teletext.id < 0 ? VLC_SUCCESS : VLC_EGENERIC;
            vlc_list_foreach(tt, &input->teletexts, node)
                if (event->teletext.id == tt->p.id)
                {
                    tt->p.selected = true;
                    ret = VLC_SUCCESS;
                }
                else
                    tt->p.selected = false;
            return ret;
        }
        default:
            vlc_assert_unreachable();
    }
}

static void
input_thread_events(input_thread_t *input_thread, void *user_data,
                    const struct vlc_input_event *event)
{
    struct vlc_player_input *input = user_data;
    vlc_player_t *player = input->player;
    struct vlc_player_private *priv = vlc_player_priv(player);

    assert(input_thread == input->thread);

    vlc_mutex_lock(&priv->lock);

    if (!input->started)
    {
        if (event->type == INPUT_EVENT_DEAD)
        {
            vlc_list_append(&input->node, &priv->garbage.inputs);
            vlc_cond_signal(&priv->garbage.cond);
        }

        vlc_mutex_unlock(&priv->lock);
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
        #if 0
        /* XXX case INPUT_EVENT_EOF: */
            if (priv->next_input == NULL)
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
            if (vlc_player_input_handle_program_event(input, event))
                skip_event = true;
            break;
        case INPUT_EVENT_ES:
            if (vlc_player_input_handle_es_event(input, event))
                skip_event = true;
            break;
        case INPUT_EVENT_TELETEXT:
            if (vlc_player_input_handle_teletext_event(input, event))
                skip_event = true;
            break;
        case INPUT_EVENT_DEAD:
            input->started = false;
            vlc_list_append(&input->node, &priv->garbage.inputs);
            vlc_cond_signal(&priv->garbage.cond);
            priv->garbage.wait_count++;

            /* XXX: for now, play only one input at a time */
            if (likely(input == priv->input))
            {
                priv->input = NULL;
                if (vlc_player_OpenNextItem(player) == VLC_SUCCESS)
                    skip_event = true;
            }
            break;
        default:
            break;
    }

    if (!skip_event)
        vlc_player_SendInputEvent(player, event);
    vlc_mutex_unlock(&priv->lock);
}

static void
vlc_player_Destructor(vlc_object_t *obj)
{
    vlc_player_t *player = (void *)obj;
    struct vlc_player_private *priv = vlc_player_priv(player);

    vlc_mutex_lock(&priv->lock);

    if (priv->input)
        vlc_player_input_StopAndClose(priv->input);
    if (priv->next_input)
        vlc_player_input_StopAndClose(priv->next_input);

    priv->garbage.running = false;
    vlc_cond_signal(&priv->garbage.cond);

    if (priv->item)
        input_item_Release(priv->item);
    if (priv->next_item)
        input_item_Release(priv->next_item);

    vlc_mutex_unlock(&priv->lock);

    vlc_join(priv->garbage.collector, NULL);

    assert(vlc_list_is_empty(&priv->listeners));

    vlc_mutex_destroy(&priv->lock);
    vlc_cond_destroy(&priv->garbage.cond);

    input_resource_Release(priv->resource);
    if (priv->renderer)
        vlc_renderer_item_release(priv->renderer);
}

vlc_player_t *
vlc_player_New(vlc_object_t *parent, struct vlc_player_cbs *owner_cbs,
               void *owner_cbs_data)
{
    struct vlc_player_private *priv =
        vlc_custom_create(parent, sizeof(*priv), "player");
    if (!priv)
        return NULL;
    vlc_player_t *player = &priv->player;

    vlc_list_init(&priv->garbage.inputs);
    priv->renderer = NULL;
    priv->next_item = NULL;
    priv->owner_cbs = *owner_cbs;
    priv->owner_cbs_data = owner_cbs_data;
    priv->input = priv->next_input = NULL;
    priv->resource = input_resource_New(VLC_OBJECT(player));

    if (!priv->resource)
        goto error;

    priv->garbage.running = true;
    vlc_mutex_init(&priv->lock);
    vlc_cond_init(&priv->garbage.cond);

    if (vlc_clone(&priv->garbage.collector, garbage_collector, player,
                  VLC_THREAD_PRIORITY_LOW) != 0)
    {
        vlc_mutex_destroy(&priv->lock);
        vlc_cond_destroy(&priv->garbage.cond);
        goto error;
    }

    vlc_object_set_destructor(player, vlc_player_Destructor);
    return player;

error:
    if (priv->resource)
        input_resource_Release(priv->resource);

    vlc_object_release(player);
    return NULL;
}

void
vlc_player_Lock(vlc_player_t *player)
{
    vlc_mutex_lock(&vlc_player_priv(player)->lock);
}

void
vlc_player_Unlock(vlc_player_t *player)
{
    vlc_mutex_unlock(&vlc_player_priv(player)->lock);
}

struct vlc_player_listener_id *
vlc_player_AddListener(vlc_player_t *player,
                       const struct vlc_player_cbs *cbs, void *cbs_data)
{
    assert(cbs);
    struct vlc_player_private *priv = vlc_player_priv_locked(player);

    struct vlc_player_listener_id *listener = malloc(sizeof(*listener));
    if (!listener)
        return NULL;

    listener->cbs = *cbs;
    listener->cbs_data = cbs_data;

    vlc_list_append(&listener->node, &priv->listeners);

    return listener;
}

void
vlc_player_RemoveListener(vlc_player_t *player,
                          struct vlc_player_listener_id *id)
{
    assert(id);
    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    struct vlc_player_listener_id *listener;

    vlc_list_foreach(listener, &priv->listeners, node)
    {
        if (listener == id)
        {
            vlc_list_remove(&listener->node);
            free(listener);
            break;
        }
    }
}

int
vlc_player_OpenItem(vlc_player_t *player, input_item_t *item)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);

    if (priv->input)
        vlc_player_input_StopAndClose(priv->input);
    if (priv->next_input)
        vlc_player_input_StopAndClose(priv->next_input);
    priv->input = priv->next_input = NULL;

    if (priv->item)
    {
        input_item_Release(priv->item);
        priv->item = NULL;
    }
    if (priv->next_item)
    {
        input_item_Release(priv->next_item);
        priv->next_item = NULL;
    }

    priv->item = input_item_Hold(item);
    priv->input = vlc_player_input_Create(player, priv->item);

    return priv->input ? VLC_SUCCESS : VLC_EGENERIC;
}

int
vlc_player_SetNextItem(vlc_player_t *player, input_item_t *item)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);

    if (priv->next_item)
        input_item_Release(priv->next_item);
    priv->next_item = item ? input_item_Hold(item) : NULL;

    if (!priv->input)
        vlc_player_OpenNextItem(player);

    return 0;
}

int
vlc_player_Start(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);

    int ret;
    if (!priv->input)
    {
        /* Possible if the player was stopped by the user */
        assert(priv->item);
        priv->input = vlc_player_input_Create(player, priv->item);

        if (!priv->input)
            return VLC_EGENERIC;;
    }

    if (priv->input->started)
    {
        input_ControlPushHelper(priv->input->thread, INPUT_CONTROL_SET_STATE,
                                &(vlc_value_t) { .i_int = PLAYING_S });
        ret = VLC_SUCCESS;
    }
    else
        ret = vlc_player_input_Start(priv->input);
    return ret;
}

void
vlc_player_Stop(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    assert(priv->input);

    vlc_player_input_StopAndClose(priv->input);
    priv->input = NULL;

    if (priv->next_input)
    {
        vlc_player_input_StopAndClose(priv->next_input);
        priv->next_input = NULL;
    }
}

void
vlc_player_Pause(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    assert(priv->input && priv->input->started);
    assert(priv->input->capabilities & VLC_INPUT_CAPABILITIES_PAUSEABLE);

    input_ControlPushHelper(priv->input->thread, INPUT_CONTROL_SET_STATE,
                            &(vlc_value_t) {.i_int = PAUSE_S});
}

bool
vlc_player_IsStarted(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    return priv->input && priv->input->started;
}

bool
vlc_player_IsPlaying(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    return priv->input && priv->input->state == PLAYING_S;
}

int
vlc_player_GetCapabilities(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);
    assert(priv->input);
    return priv->input->capabilities;
}

input_item_t *
vlc_player_GetCurrentItem(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);

    return priv->item;
}

void
vlc_player_SetRenderer(vlc_player_t *player, vlc_renderer_item_t *renderer)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);

    if (priv->renderer)
        vlc_renderer_item_release(priv->renderer);
    priv->renderer = vlc_renderer_item_hold(renderer);

    if (priv->input)
        input_Control(priv->input->thread, INPUT_SET_RENDERER, priv->renderer);
    if (priv->next_input)
        input_Control(priv->next_input->thread, INPUT_SET_RENDERER,
                      priv->renderer);
}

void
vlc_player_ResetAout(vlc_player_t *player)
{
    struct vlc_player_private *priv = vlc_player_priv_locked(player);

    input_resource_ResetAout(priv->resource);
}
