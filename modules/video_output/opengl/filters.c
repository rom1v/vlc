/*****************************************************************************
 * filters.c
 *****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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

#include "filters.h"

#include <vlc_common.h>

#include "filter_priv.h"
#include "renderer.h"
#include "sampler_priv.h"

void
vlc_gl_filters_Init(struct vlc_gl_filters *filters, struct vlc_gl_t *gl,
                    const struct vlc_gl_api *api,
                    struct vlc_gl_interop *interop)
{
    filters->gl = gl;
    filters->api = api;
    filters->interop = interop;
    vlc_list_init(&filters->list);
}

struct vlc_gl_filter *
vlc_gl_filters_Append(struct vlc_gl_filters *filters, const char *name,
                      const config_chain_t *config)
{
    struct vlc_gl_filter *filter = vlc_gl_filter_New(filters->gl, filters->api);
    if (!filter)
        return NULL;

    struct vlc_gl_filter_priv *priv = vlc_gl_filter_PRIV(filter);

    struct vlc_gl_filter_priv *prev_filter =
        vlc_list_last_entry_or_null(&filters->list, struct vlc_gl_filter_priv,
                                    node);
    if (!prev_filter)
        priv->sampler = vlc_gl_sampler_NewFromInterop(filters->interop);
    else
    {
        // XXX the format is used only for the width/height
        const video_format_t *fmt = &filters->interop->fmt;
        priv->sampler =
            vlc_gl_sampler_NewDirect(filters->gl, filters->api, fmt);
    }

    if (!priv->sampler)
    {
        vlc_gl_filter_Delete(filter);
        return NULL;
    }

    int ret = vlc_gl_filter_LoadModule(filters->gl, name, filter, config,
                                       priv->sampler);
    if (ret != VLC_SUCCESS)
    {
        vlc_gl_sampler_Delete(priv->sampler);
        vlc_gl_filter_Delete(filter);
        return NULL;
    }

    if (prev_filter)
    {
        /* It was the last filter before we append this one */
        assert(!prev_filter->framebuffer_out);

        const opengl_vtable_t *vt = &filters->api->vt;
        /* Every non-last filter needs its own framebuffer */
        vt->GenFramebuffers(1, &prev_filter->framebuffer_out);

        priv->framebuffer_in = prev_filter->framebuffer_out;
    }

    vlc_list_append(&priv->node, &filters->list);

    return filter;
}

int
vlc_gl_filters_UpdatePicture(struct vlc_gl_filters *filters,
                             picture_t *picture)
{
    assert(!vlc_list_is_empty(&filters->list));

    struct vlc_gl_filter_priv *first_filter =
        vlc_list_first_entry_or_null(&filters->list, struct vlc_gl_filter_priv,
                                     node);

    assert(first_filter);

    return vlc_gl_sampler_UpdatePicture(first_filter->sampler, picture);
}

int
vlc_gl_filters_Draw(struct vlc_gl_filters *filters)
{
    const opengl_vtable_t *vt = &filters->api->vt;

    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        vt->BindFramebuffer(GL_READ_FRAMEBUFFER, priv->framebuffer_in);
        vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, priv->framebuffer_out);

        struct vlc_gl_filter *filter = &priv->filter;
        int ret = filter->ops->draw(filter);
        if (ret != VLC_SUCCESS)
            return ret;
    }

    return VLC_SUCCESS;
}

void
vlc_gl_filters_Destroy(struct vlc_gl_filters *filters)
{
    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        struct vlc_gl_filter *filter = &priv->filter;
        vlc_gl_filter_Delete(filter);
    }
}
