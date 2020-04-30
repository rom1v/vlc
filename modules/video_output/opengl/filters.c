/*****************************************************************************
 * filters.c
 *****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
 * Copyright (C) 2020 Videolabs
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
#include <vlc_list.h>

#include "filter_priv.h"
#include "renderer.h"
#include "sampler_priv.h"

struct vlc_gl_filters {
    struct vlc_gl_t *gl;
    const struct vlc_gl_api *api;

    /**
     * Interop to use for the sampler of the first filter of the chain,
     * the one which uses the picture_t as input.
     */
    struct vlc_gl_interop *interop;

    struct vlc_list list; /**< list of vlc_gl_filter.node */

    struct vlc_gl_filters_viewport {
        int x;
        int y;
        unsigned width;
        unsigned height;
    } viewport;

    /** Last updated picture PTS */
    vlc_tick_t pts;
};

struct vlc_gl_filters *
vlc_gl_filters_New(struct vlc_gl_t *gl, const struct vlc_gl_api *api,
                   struct vlc_gl_interop *interop)
{
    struct vlc_gl_filters *filters = malloc(sizeof(*filters));
    if (!filters)
        return NULL;

    filters->gl = gl;
    filters->api = api;
    filters->interop = interop;
    vlc_list_init(&filters->list);

    memset(&filters->viewport, 0, sizeof(filters->viewport));
    filters->pts = 0;

    return filters;
}

void
vlc_gl_filters_Delete(struct vlc_gl_filters *filters)
{
    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        struct vlc_gl_filter *filter = &priv->filter;
        vlc_gl_filter_Delete(filter);
    }

    free(filters);
}

static int
InitFramebufferOut(struct vlc_gl_filter_priv *priv)
{
    assert(priv->size_out.width > 0 && priv->size_out.height > 0);

    const opengl_vtable_t *vt = &priv->filter.api->vt;

    /* Create a texture having the expected size */
    vt->GenTextures(1, &priv->texture_out);
    vt->BindTexture(GL_TEXTURE_2D, priv->texture_out);
    vt->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, priv->size_out.width,
                   priv->size_out.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* iOS needs GL_CLAMP_TO_EDGE or power-of-two textures */
    vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Create a framebuffer and attach the texture */
    vt->GenFramebuffers(1, &priv->framebuffer_out);
    vt->BindFramebuffer(GL_FRAMEBUFFER, priv->framebuffer_out);
    vt->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, priv->texture_out, 0);

    priv->has_framebuffer_out = true;

    GLenum status = vt->CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        return VLC_EGENERIC;

    vt->BindFramebuffer(GL_FRAMEBUFFER, 0);
    return VLC_SUCCESS;
}

static struct vlc_gl_sampler *
GetSampler(struct vlc_gl_filter *filter)
{
    struct vlc_gl_filter_priv *priv = vlc_gl_filter_PRIV(filter);
    if (priv->sampler)
        /* already initialized */
        return priv->sampler;

    struct vlc_gl_filters *filters = priv->filters;
    struct vlc_gl_filter_priv *prev_filter = priv->prev_filter;

    struct vlc_gl_sampler *sampler;
    if (!priv->prev_filter)
        sampler = vlc_gl_sampler_NewFromInterop(filters->interop);
    else
    {
        video_format_t fmt;
        video_format_Init(&fmt, VLC_CODEC_RGBA);
        fmt.i_width = fmt.i_visible_width = prev_filter->size_out.width;
        fmt.i_height = fmt.i_visible_height = prev_filter->size_out.height;

        sampler = vlc_gl_sampler_NewDirect(filters->gl, filters->api, &fmt);
    }

    priv->sampler = sampler;

    return sampler;
}

struct vlc_gl_filter *
vlc_gl_filters_Append(struct vlc_gl_filters *filters, const char *name,
                      const config_chain_t *config)
{
    struct vlc_gl_filter *filter = vlc_gl_filter_New(filters->gl, filters->api);
    if (!filter)
        return NULL;

    struct vlc_gl_filter_priv *priv = vlc_gl_filter_PRIV(filter);

    struct vlc_gl_tex_size size_in;

    struct vlc_gl_filter_priv *prev_filter =
        vlc_list_last_entry_or_null(&filters->list, struct vlc_gl_filter_priv,
                                    node);
    if (!prev_filter)
    {
        size_in.width = filters->interop->fmt.i_visible_width;
        size_in.height = filters->interop->fmt.i_visible_height;
    }
    else
    {
        size_in = prev_filter->size_out;
    }

    priv->filters = filters;
    priv->prev_filter = prev_filter;

    static const struct vlc_gl_filter_owner_ops owner_ops = {
        .get_sampler = GetSampler,
    };
    filter->owner_ops = &owner_ops;

    /* By default, the output size is the same as the input size. The filter
     * may change it during its Open(). */
    priv->size_out = size_in;

    int ret = vlc_gl_filter_LoadModule(filters->gl, name, filter, config,
                                       &priv->size_out);
    if (ret != VLC_SUCCESS)
    {
        /* Creation failed, do not call close() */
        filter->ops = NULL;
        vlc_gl_filter_Delete(filter);
        return NULL;
    }

    /* A blend filter may not change its output size. */
    assert(!filter->config.blend
           || (priv->size_out.width == size_in.width
            && priv->size_out.height == size_in.height));

    /* A blend filter may not read its input, so it is an error if a sampler
     * has been requested.
     *
     * We assert it here instead of in vlc_gl_filter_GetSampler() because the
     * filter implementation may set the "blend" flag after it get the sampler
     * in its Open() function.
     */
    assert(!filter->config.blend || !priv->sampler);

    if (filter->config.blend)
    {
        if (!prev_filter)
        {
            /* We cannot blend with nothing, so insert a "draw" filter to draw
             * the input picture to blend with. */
            struct vlc_gl_filter *draw =
                vlc_gl_filters_Append(filters, "draw", NULL);
            if (!draw)
            {
                vlc_gl_filter_Delete(filter);
                return NULL;
            }
        }

        /* Append as a subfilter of a non-blend filter */
        struct vlc_gl_filter_priv *last_filter =
            vlc_list_last_entry_or_null(&filters->list,
                                        struct vlc_gl_filter_priv, node);
        assert(last_filter);
        vlc_list_append(&priv->node, &last_filter->blend_subfilters);
    }
    else
        /* Append to the main filter list */
        vlc_list_append(&priv->node, &filters->list);

    return filter;
}

int
vlc_gl_filters_InitFramebuffers(struct vlc_gl_filters *filters)
{
    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        bool is_last = vlc_list_is_last(&priv->node, &filters->list);
        if (!is_last)
        {
            /* It was the last non-blend filter before we append this one */
            assert(!priv->has_framebuffer_out);

            /* Every non-blend filter needs its own framebuffer, except the last
             * one */
            int ret = InitFramebufferOut(priv);
            if (ret != VLC_SUCCESS)
                return ret;
        }
    }

    return VLC_SUCCESS;
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

    filters->pts = picture->date;

    return vlc_gl_sampler_UpdatePicture(first_filter->sampler, picture);
}

int
vlc_gl_filters_Draw(struct vlc_gl_filters *filters)
{
    const opengl_vtable_t *vt = &filters->api->vt;

    GLint value;
    vt->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
    GLuint draw_framebuffer = value; /* as GLuint */

    struct vlc_gl_input_meta meta = {
        .pts = filters->pts,
    };

    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        struct vlc_gl_filter_priv *previous =
            vlc_list_prev_entry_or_null(&filters->list, priv,
                                        struct vlc_gl_filter_priv, node);
        if (previous)
        {
            /* Read from the output of the previous filter */
            int ret = vlc_gl_sampler_UpdateTexture(priv->sampler,
                                                   previous->texture_out,
                                                   previous->size_out.width,
                                                   previous->size_out.height);
            if (ret != VLC_SUCCESS)
            {
                msg_Err(filters->gl, "Could not update sampler texture");
                return ret;
            }
        }

        GLuint draw_fb = priv->has_framebuffer_out ? priv->framebuffer_out
                                                   : draw_framebuffer;

        vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);

        if (vlc_list_is_last(&priv->node, &filters->list))
        {
            /* The output viewport must be applied on the last filter */
            struct vlc_gl_filters_viewport *vp = &filters->viewport;
            vt->Viewport(vp->x, vp->y, vp->width, vp->height);
        }
        else
            vt->Viewport(0, 0, priv->size_out.width, priv->size_out.height);

        struct vlc_gl_filter *filter = &priv->filter;
        int ret = filter->ops->draw(filter, &meta);
        if (ret != VLC_SUCCESS)
            return ret;

        /* Draw blend subfilters */
        struct vlc_gl_filter_priv *subfilter_priv;
        vlc_list_foreach(subfilter_priv, &priv->blend_subfilters, node)
        {
            /* Reset the draw buffer, in case it has been changed from a filter
             * draw() callback */
            vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);

            struct vlc_gl_filter *subfilter = &subfilter_priv->filter;
            ret = subfilter->ops->draw(subfilter, &meta);
            if (ret != VLC_SUCCESS)
                return ret;
        }
    }

    return VLC_SUCCESS;
}

void
vlc_gl_filters_SetViewport(struct vlc_gl_filters *filters, int x, int y,
                           unsigned width, unsigned height)
{
    filters->viewport.x = x;
    filters->viewport.y = y;
    filters->viewport.width = width;
    filters->viewport.height = height;
}
