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
        priv->sampler = vlc_gl_sampler_NewFromInterop(filters->interop);
    }
    else
    {
        size_in = prev_filter->size_out;

        video_format_t fmt;
        video_format_Init(&fmt, VLC_CODEC_RGBA);
        fmt.i_width = fmt.i_visible_width = size_in.width;
        fmt.i_height = fmt.i_visible_height = size_in.height;

        priv->sampler =
            vlc_gl_sampler_NewDirect(filters->gl, filters->api, &fmt);
    }

    if (!priv->sampler)
    {
        vlc_gl_filter_Delete(filter);
        return NULL;
    }

    /* By default, the output size is the same as the input size. The filter
     * may change it during its Open(). */
    priv->size_out = size_in;

    int ret = vlc_gl_filter_LoadModule(filters->gl, name, filter, config,
                                       &priv->size_out, priv->sampler);
    if (ret != VLC_SUCCESS)
    {
        /* Creation failed, do not call close() */
        filter->ops = NULL;
        vlc_gl_filter_Delete(filter);
        return NULL;
    }

    if (prev_filter)
    {
        /* It was the last filter before we append this one */
        assert(!prev_filter->has_framebuffer_out);

        /* Every non-last filter needs its own framebuffer */
        ret = InitFramebufferOut(prev_filter);
        if (ret != VLC_SUCCESS)
        {
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

    return vlc_gl_sampler_UpdatePicture(first_filter->sampler, picture);
}

int
vlc_gl_filters_Draw(struct vlc_gl_filters *filters)
{
    const opengl_vtable_t *vt = &filters->api->vt;

    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        GLuint read_fb = 0;

        struct vlc_gl_filter_priv *previous =
            vlc_list_prev_entry_or_null(&filters->list, priv,
                                        struct vlc_gl_filter_priv, node);
        if (previous)
        {
            /* Read from the output of the previous filter */
            read_fb = previous->framebuffer_out;
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

        GLuint draw_fb = priv->has_framebuffer_out ? priv->framebuffer_out : 0;

        vt->BindFramebuffer(GL_READ_FRAMEBUFFER, read_fb);
        vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);

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
