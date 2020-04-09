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

/* The filter chain contains the sequential list of filters, typically given
 * via command-line arguments "--gl-filters=filter1:filter2:...:filtern".
 *
 * There are two types of filters:
 *  - blend filters just draw over the provided framebuffer (containing the
 *    result of the previous filter), without reading the input picture.
 *  - non-blend filters read their input picture and draw whatever they want to
 *    their own output framebuffer.
 *
 * For convenience, the filter chain does not store the filters as a single
 * sequential list, but as a list of non-blend filters, each containing the
 * list of their associated blend filters.
 *
 * For example, given:
 *
 *    --gl-filters=draw:triangle:triangle_mask:triangle_clock:triangle:renderer
 *
 * the filters chain stores the filters as follow:
 *
 *     +- draw               (non-blend)
 *     |  +- triangle        (blend)
 *     +- triangle_mask      (non-blend)
 *     |  +- triangle_clock  (blend)
 *     |  +- triangle        (blend)
 *     +- renderer           (non-blend)
 *
 * An output framebuffer is created for each non-blend filters. It is used as
 * draw framebuffer for that filter and all its associated blend filters.
 *
 * If the first filter is a blend filter, then a "draw" filter is automatically
 * inserted. If the renderer does not appear in the filter list, it is
 * automatically added at the end.
 *
 *
 * ## Multisample anti-aliasing (MSAA)
 *
 * Each filter may also request multisample anti-aliasing, by providing a MSAA
 * level during its Open(), for example:
 *
 *     filter->config.msaa_level = 4;
 *
 * For example:
 *
 *     +- draw               msaa_level=0
 *     |  +- triangle        msaa_level=4
 *     |  +- triangle_clock  msaa_level=2
 *     +- renderer           msaa_level=0
 *
 * Among a "group" of one non-blend filter and its associated blend filters,
 * the highest MSAA level (or 0 if multisampling is not supported) is assigned
 * both to the non-blend filter, to configure its MSAA framebuffer, and to the
 * blend filters, just for information and consistency:
 *
 *     +- draw               msaa_level=4
 *     |  +- triangle        msaa_level=4
 *     |  +- triangle_clock  msaa_level=4
 *     +- renderer           msaa_level=0
 *
 * Some platforms (Android) do not support resolving multisample to the default
 * framebuffer. Therefore, the msaa_level must always be 0 on the last filter.
 * If this is not the case, a "draw" filter is automatically appended.
 *
 * For example:
 *
 *     +- draw               msaa_level=0
 *     |  +- triangle        msaa_level=4
 *     +- renderer           msaa_level=0
 *        +- triangle_clock  msaa_level=2
 *
 * will become:
 *
 *     +- draw               msaa_level=4
 *     |  +- triangle        msaa_level=4
 *     +- renderer           msaa_level=2
 *     |  +- triangle_clock  msaa_level=2
 *     +- draw               msaa_level=0
 */

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
InitPlane(struct vlc_gl_filter_priv *priv, unsigned plane, GLsizei width,
          GLsizei height)
{
    const opengl_vtable_t *vt = &priv->filter.api->vt;

    GLuint framebuffer = priv->framebuffers_out[plane];
    GLuint texture = priv->textures_out[plane];

    vt->BindTexture(GL_TEXTURE_2D, texture);
    vt->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, NULL);
    vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* iOS needs GL_CLAMP_TO_EDGE or power-of-two textures */
    vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Create a framebuffer and attach the texture */
    vt->BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    vt->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, texture, 0);

    GLenum status = vt->CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        return VLC_EGENERIC;

    vt->BindFramebuffer(GL_FRAMEBUFFER, 0);
    return VLC_SUCCESS;
}

static int
InitFramebuffersOut(struct vlc_gl_filter_priv *priv)
{
    assert(priv->size_out.width > 0 && priv->size_out.height > 0);

    const opengl_vtable_t *vt = &priv->filter.api->vt;

    struct vlc_gl_filter *filter = &priv->filter;
    if (filter->config.filter_planes)
    {
        struct vlc_gl_sampler *sampler = vlc_gl_filter_GetSampler(filter);
        if (!sampler)
            return VLC_EGENERIC;

        const vlc_chroma_description_t *desc =
            vlc_fourcc_GetChromaDescription(priv->sampler->fmt.i_chroma);
        if (!desc)
            return VLC_EGENERIC;

        priv->tex_count = desc->plane_count;
        vt->GenFramebuffers(priv->tex_count, priv->framebuffers_out);
        vt->GenTextures(priv->tex_count, priv->textures_out);

        /* Size of the first plane */
        struct vlc_gl_tex_size *main_size = &priv->size_out;

        for (unsigned i = 0; i < desc->plane_count; ++i)
        {
            const vlc_rational_t *scale_w = &desc->p[i].w;
            const vlc_rational_t *scale_h = &desc->p[i].h;
            priv->tex_widths[i] =
                main_size->width * scale_w->num / scale_w->den;
            priv->tex_heights[i] =
                main_size->height * scale_h->num / scale_h->den;
            /* Init one framebuffer and texture for each plane */
            int ret =
                InitPlane(priv, i, priv->tex_widths[i], priv->tex_heights[i]);
            if (ret != VLC_SUCCESS)
                return ret;
        }
    }
    else
    {
        priv->tex_count = 1;

        /* Create a texture having the expected size */

        vt->GenFramebuffers(1, priv->framebuffers_out);
        vt->GenTextures(1, priv->textures_out);

        priv->tex_widths[0] = priv->size_out.width;
        priv->tex_heights[0] = priv->size_out.height;

        int ret = InitPlane(priv, 0, priv->tex_widths[0], priv->tex_heights[0]);
        if (ret != VLC_SUCCESS)
            return ret;
    }

    return VLC_SUCCESS;
}

static int
InitFramebufferMSAA(struct vlc_gl_filter_priv *priv, unsigned msaa_level)
{
    assert(msaa_level);
    assert(priv->size_out.width > 0 && priv->size_out.height > 0);

    const opengl_vtable_t *vt = &priv->filter.api->vt;

    vt->GenRenderbuffers(1, &priv->renderbuffer_msaa);
    vt->BindRenderbuffer(GL_RENDERBUFFER, priv->renderbuffer_msaa);
    vt->RenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level,
                                       GL_RGBA8,
                                       priv->size_out.width,
                                       priv->size_out.height);

    vt->GenFramebuffers(1, &priv->framebuffer_msaa);
    vt->BindFramebuffer(GL_FRAMEBUFFER, priv->framebuffer_msaa);
    vt->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, priv->renderbuffer_msaa);

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

    bool expose_planes = filter->config.filter_planes;
    struct vlc_gl_sampler *sampler;
    if (!priv->prev_filter)
        sampler = vlc_gl_sampler_NewFromInterop(filters->interop,
                                                expose_planes);
    else
    {
        video_format_t fmt;

        /* If the previous filter operated on planes, then its output chroma is
         * the same as its input chroma. Otherwise, it's RGBA. */
        vlc_fourcc_t chroma = prev_filter->filter.config.filter_planes
                            ? prev_filter->sampler->fmt.i_chroma
                            : VLC_CODEC_RGBA;

        video_format_Init(&fmt, chroma);
        fmt.i_width = fmt.i_visible_width = prev_filter->size_out.width;
        fmt.i_height = fmt.i_visible_height = prev_filter->size_out.height;

        sampler = vlc_gl_sampler_NewDirect(filters->gl, filters->api, &fmt,
                                           expose_planes);
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

    /* Assign the default draw framebuffer to the filter */
    priv->framebuffers_out[0] = filters->draw_framebuffer;

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

    /* A filter operating on planes may not blend. */
    assert(!filter->config.filter_planes || !filter->config.blend);

    /* A filter operating on planes may not use anti-aliasing. */
    assert(!filter->config.filter_planes || !filter->config.msaa_level);

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
        if (!prev_filter || prev_filter->filter.config.filter_planes)
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
    struct vlc_gl_filter_priv *priv = NULL;
    struct vlc_gl_filter_priv *subfilter_priv;

    vlc_list_foreach(priv, &filters->list, node)
    {
        /* Compute the highest msaa_level among the filter and its subfilters */
        unsigned msaa_level = 0;
        if (filters->api->supports_multisample)
        {
            msaa_level = priv->filter.config.msaa_level;
            vlc_list_foreach(subfilter_priv, &priv->blend_subfilters, node)
            {
                if (subfilter_priv->filter.config.msaa_level > msaa_level)
                    msaa_level = subfilter_priv->filter.config.msaa_level;
            }
        }

        /* Update the actual msaa_level used to create the MSAA framebuffer */
        priv->filter.config.msaa_level = msaa_level;
        /* Also update the actual msaa_level for subfilters, just for info */
        vlc_list_foreach(subfilter_priv, &priv->blend_subfilters, node)
            subfilter_priv->filter.config.msaa_level = msaa_level;
    }

    /* "priv" is the last filter */
    assert(priv); /* There is at least one filter */

    bool insert_draw =
        /* Resolving multisampling to the default framebuffer might fail,
         * because its format may be different. */
        priv->filter.config.msaa_level ||
        /* A filter operating on planes may produce several textures.
         * They need to be chroma-converted to a single RGBA texture. */
        priv->filter.config.filter_planes;
    if (insert_draw)
    {
        struct vlc_gl_filter *draw =
            vlc_gl_filters_Append(filters, "draw", NULL);
        if (!draw)
            return VLC_EGENERIC;
    }

    vlc_list_foreach(priv, &filters->list, node)
    {
        unsigned msaa_level = priv->filter.config.msaa_level;
        if (msaa_level)
        {
            int ret = InitFramebufferMSAA(priv, msaa_level);
            if (ret != VLC_SUCCESS)
                return ret;
        }

        bool is_last = vlc_list_is_last(&priv->node, &filters->list);
        if (!is_last)
        {
            /* It was the last non-blend filter before we append this one */
            assert(priv->tex_count == 0);

            /* Every non-blend filter needs its own framebuffer, except the last
             * one */
            int ret = InitFramebuffersOut(priv);
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

    struct vlc_gl_input_meta meta = {
        .pts = filters->pts,
        .plane = 0,
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
            int ret = vlc_gl_sampler_UpdateTextures(priv->sampler,
                                                    previous->textures_out,
                                                    previous->tex_widths,
                                                    previous->tex_heights);
            if (ret != VLC_SUCCESS)
            {
                msg_Err(filters->gl, "Could not update sampler texture");
                return ret;
            }
        }

        struct vlc_gl_filter *filter = &priv->filter;

        if (filter->config.filter_planes)
        {
            for (unsigned i = 0; i < priv->tex_count; ++i)
            {
                meta.plane = i;

                /* Select the output texture associated to this plane */
                GLuint draw_fb = priv->framebuffers_out[i];
                vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);

                assert(!vlc_list_is_last(&priv->node, &filters->list));
                vt->Viewport(0, 0, priv->tex_widths[i], priv->tex_heights[i]);

                vlc_gl_sampler_SelectPlane(priv->sampler, i);
                int ret = filter->ops->draw(filter, &meta);
                if (ret != VLC_SUCCESS)
                    return ret;
            }
        }
        else
        {
            assert(priv->tex_count <= 1);
            unsigned msaa_level = priv->filter.config.msaa_level;
            GLuint draw_fb;
            if (msaa_level)
                draw_fb = priv->framebuffer_msaa;
            else
                draw_fb = priv->tex_count > 0 ? priv->framebuffers_out[0]
                                              : filters->draw_framebuffer;

            vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);

            if (vlc_list_is_last(&priv->node, &filters->list))
            {
                /* The output viewport must be applied on the last filter */
                struct vlc_gl_filters_viewport *vp = &filters->viewport;
                vt->Viewport(vp->x, vp->y, vp->width, vp->height);
            }
            else
                vt->Viewport(0, 0, priv->tex_widths[0], priv->tex_heights[0]);

            meta.plane = 0;
            int ret = filter->ops->draw(filter, &meta);
            if (ret != VLC_SUCCESS)
                return ret;

            /* Draw blend subfilters */
            struct vlc_gl_filter_priv *subfilter_priv;
            vlc_list_foreach(subfilter_priv, &priv->blend_subfilters, node)
            {
                /* Reset the draw buffer, in case it has been changed from a
                 * filter draw() callback */
                vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);

                struct vlc_gl_filter *subfilter = &subfilter_priv->filter;
                ret = subfilter->ops->draw(subfilter, &meta);
                if (ret != VLC_SUCCESS)
                    return ret;
            }

            if (filter->config.msaa_level)
            {
                /* Never resolve multisampling to the default framebuffer */
                assert(priv->tex_count == 1);
                assert(priv->framebuffers_out[0] != filters->draw_framebuffer);

                /* Resolve the MSAA into the target framebuffer */
                vt->BindFramebuffer(GL_READ_FRAMEBUFFER,
                                    priv->framebuffer_msaa);
                vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER,
                                    priv->framebuffers_out[0]);

                GLint width = priv->size_out.width;
                GLint height = priv->size_out.height;
                vt->BlitFramebuffer(0, 0, width, height,
                                    0, 0, width, height,
                                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
        }
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
