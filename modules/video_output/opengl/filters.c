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
    memset(&filters->viewport, 0, sizeof(filters->viewport));
    filters->pts = 0;

    GLint value;
    api->vt.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
    filters->draw_framebuffer = value; /* as GLuint */
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

    /* Create a framebuffer and attach the texture */
    vt->GenFramebuffers(1, &priv->framebuffer_out);
    vt->BindFramebuffer(GL_FRAMEBUFFER, priv->framebuffer_out);
    GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
    vt->FramebufferTexture2D(GL_FRAMEBUFFER, draw_buffer, GL_TEXTURE_2D,
                             priv->texture_out, 0);

    vt->DrawBuffers(1, &draw_buffer);

    GLenum status = vt->CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        return VLC_EGENERIC;

    vt->BindFramebuffer(GL_FRAMEBUFFER, 0);
    return VLC_SUCCESS;
}

static struct vlc_gl_filter_priv *
FindLastNonBlend(struct vlc_gl_filters *filters)
{
    struct vlc_gl_filter_priv *last =
        vlc_list_last_entry_or_null(&filters->list, struct vlc_gl_filter_priv,
                                    node);
    while (last && last->filter.config.blend)
    {
        last = vlc_list_prev_entry_or_null(&filters->list, last,
                                           struct vlc_gl_filter_priv, node);
    }
    return last;
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

    struct vlc_gl_filter_priv *prev_filter = FindLastNonBlend(filters);
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

    if (filter->config.blend && !prev_filter)
    {
        /* We cannot blend with nothing, so insert a "draw" filter to draw the
         * input picture to blend with. */
        struct vlc_gl_filter *draw =
            vlc_gl_filters_Append(filters, "draw", NULL);
        if (!draw)
        {
            vlc_gl_filter_Delete(filter);
            return NULL;
        }
    }
    else if (!filter->config.blend && prev_filter)
    {
        /* It was the last non-blend filter before we append this one */
        assert(prev_filter->framebuffer_out == filters->draw_framebuffer);

        /* Every non-blend filter needs its own framebuffer, except the last
         * one */
        ret = InitFramebufferOut(prev_filter);
        if (ret != VLC_SUCCESS)
        {
            vlc_gl_sampler_Delete(priv->sampler);
            vlc_gl_filter_Delete(filter);
            return NULL;
        }
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

    filters->pts = picture->date;

    return vlc_gl_sampler_UpdatePicture(first_filter->sampler, picture);
}

int
vlc_gl_filters_Draw(struct vlc_gl_filters *filters)
{
    const opengl_vtable_t *vt = &filters->api->vt;

    struct vlc_gl_input_meta meta = {
        .pts = filters->pts,
    };

    struct vlc_gl_filter_priv *last_non_blend = FindLastNonBlend(filters);
    bool apply_viewport = false;

    /* Previous filter which is not a blend filter */
    struct vlc_gl_filter_priv *previous = NULL;

    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        struct vlc_gl_filter *filter = &priv->filter;
        if (!filter->config.blend)
        {
            if (!previous)
            {
                /* We don't use it */
                vt->BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }
            else
            {
                /* Read from the output of the previous filter */
                vt->BindFramebuffer(GL_READ_FRAMEBUFFER,
                                    previous->framebuffer_out);
                int ret =
                    vlc_gl_sampler_UpdateTexture(priv->sampler,
                                                 previous->texture_out,
                                                 previous->size_out.width,
                                                 previous->size_out.height);
                if (ret != VLC_SUCCESS)
                {
                    msg_Err(filters->gl, "Could not update sampler texture");
                    return ret;
                }

                vlc_gl_sampler_PrepareShader(priv->sampler);
            }

            vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, priv->framebuffer_out);

            /* This is the last non-blend filter so far */
            previous = priv;
        }

        /* The output viewport must be applied on the last non-blend filter and
         * all the following blend filters. */
        if (priv == last_non_blend)
            apply_viewport = true;

        if (apply_viewport)
        {
            struct vlc_gl_filters_viewport *vp = &filters->viewport;
            vt->Viewport(vp->x, vp->y, vp->width, vp->height);
        }
        else
            vt->Viewport(0, 0, priv->size_out.width, priv->size_out.height);

        int ret = filter->ops->draw(filter, &meta);
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

void
vlc_gl_filters_SetViewport(struct vlc_gl_filters *filters, int x, int y,
                           unsigned width, unsigned height)
{
    filters->viewport.x = x;
    filters->viewport.y = y;
    filters->viewport.width = width;
    filters->viewport.height = height;
}
