/*****************************************************************************
 * vout_helper.c: OpenGL and OpenGL ES output common code
 *****************************************************************************
 * Copyright (C) 2004-2016 VLC authors and VideoLAN
 * Copyright (C) 2009, 2011 Laurent Aimar
 *
 * Authors: Laurent Aimar <fenrir _AT_ videolan _DOT_ org>
 *          Ilkka Ollakka <ileoo@videolan.org>
 *          Rémi Denis-Courmont
 *          Adrien Maglo <magsoft at videolan dot org>
 *          Felix Paul Kühne <fkuehne at videolan dot org>
 *          Pierre d'Herbemont <pdherbemont at videolan dot org>
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

#include <assert.h>
#include <math.h>

#include <vlc_common.h>
#include <vlc_list.h>
#include <vlc_subpicture.h>
#include <vlc_opengl.h>
#include <vlc_modules.h>
#include <vlc_vout.h>
#include <vlc_viewpoint.h>

#include "filter_priv.h"
#include "filters.h"
#include "gl_api.h"
#include "gl_util.h"
#include "vout_helper.h"
#include "internal.h"
#include "renderer.h"
#include "sampler.h"
#include "sampler_priv.h"
#include "sub_renderer.h"

struct vout_display_opengl_t {

    vlc_gl_t   *gl;
    struct vlc_gl_api api;

    struct vlc_gl_interop *interop;
    struct vlc_gl_renderer *renderer;

    struct vlc_gl_filters filters;

    struct vlc_gl_interop *sub_interop;
    struct vlc_gl_sub_renderer *sub_renderer;
};

static const vlc_fourcc_t gl_subpicture_chromas[] = {
    VLC_CODEC_RGBA,
    0
};

static void
ResizeFormatToGLMaxTexSize(video_format_t *fmt, unsigned int max_tex_size)
{
    if (fmt->i_width > fmt->i_height)
    {
        unsigned int const  vis_w = fmt->i_visible_width;
        unsigned int const  vis_h = fmt->i_visible_height;
        unsigned int const  nw_w = max_tex_size;
        unsigned int const  nw_vis_w = nw_w * vis_w / fmt->i_width;

        fmt->i_height = nw_w * fmt->i_height / fmt->i_width;
        fmt->i_width = nw_w;
        fmt->i_visible_height = nw_vis_w * vis_h / vis_w;
        fmt->i_visible_width = nw_vis_w;
    }
    else
    {
        unsigned int const  vis_w = fmt->i_visible_width;
        unsigned int const  vis_h = fmt->i_visible_height;
        unsigned int const  nw_h = max_tex_size;
        unsigned int const  nw_vis_h = nw_h * vis_h / fmt->i_height;

        fmt->i_width = nw_h * fmt->i_width / fmt->i_height;
        fmt->i_height = nw_h;
        fmt->i_visible_width = nw_vis_h * vis_w / vis_h;
        fmt->i_visible_height = nw_vis_h;
    }
}

static struct vlc_gl_sampler *
GetSampler(struct vlc_gl_filter *filter)
{
    struct vlc_gl_filter_priv *priv = vlc_gl_filter_PRIV(filter);
    if (!priv->sampler)
    {
        struct vlc_gl_interop *interop = filter->owner_data;
        priv->sampler = vlc_gl_sampler_NewFromInterop(interop);
    }

    return priv->sampler;
}

vout_display_opengl_t *vout_display_opengl_New(video_format_t *fmt,
                                               const vlc_fourcc_t **subpicture_chromas,
                                               vlc_gl_t *gl,
                                               const vlc_viewpoint_t *viewpoint,
                                               vlc_video_context *context)
{
    vout_display_opengl_t *vgl = calloc(1, sizeof(*vgl));
    if (!vgl)
        return NULL;

    vgl->gl = gl;

    int ret = vlc_gl_api_Init(&vgl->api, gl);
    if (ret != VLC_SUCCESS)
    {
        free(vgl);
        return NULL;
    }

    const opengl_vtable_t *vt = &vgl->api.vt;

    const char *extensions = (const char *) vt->GetString(GL_EXTENSIONS);
    assert(extensions);
    if (!extensions)
    {
        msg_Err(gl, "glGetString returned NULL");
        free(vgl);
        return NULL;
    }
#if !defined(USE_OPENGL_ES2)
    const unsigned char *ogl_version = vt->GetString(GL_VERSION);
    bool supports_shaders = strverscmp((const char *)ogl_version, "2.0") >= 0;
    if (!supports_shaders)
    {
        msg_Err(gl, "shaders not supported, bailing out");
        free(vgl);
        return NULL;
    }
#endif

    /* Resize the format if it is greater than the maximum texture size
     * supported by the hardware */
    GLint       max_tex_size;
    vt->GetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);

    if ((GLint)fmt->i_width > max_tex_size ||
        (GLint)fmt->i_height > max_tex_size)
        ResizeFormatToGLMaxTexSize(fmt, max_tex_size);

    vgl->interop = vlc_gl_interop_New(gl, &vgl->api, context, fmt, false);
    if (!vgl->interop)
    {
        msg_Err(gl, "Could not create interop");
        free(vgl);
        return NULL;
    }

    static const struct vlc_gl_filter_owner_ops owner_ops = {
        .get_sampler = GetSampler,
    };
    /* For now, we only need the interop to create a sampler */
    void *owner_data = vgl->interop;

    struct vlc_gl_renderer *renderer = vgl->renderer =
        vlc_gl_renderer_New(gl, &vgl->api, &owner_ops, owner_data);
    if (!vgl->renderer)
    {
        msg_Warn(gl, "Could not create renderer for %4.4s",
                 (const char *) &fmt->i_chroma);
        vlc_gl_interop_Delete(vgl->interop);
        free(vgl);
        return NULL;
    }

    GL_ASSERT_NOERROR();

    vlc_gl_filters_Init(&vgl->filters);

    /* The renderer is the only filter, for now */
    vlc_gl_filters_Append(&vgl->filters, renderer->filter);

    struct vlc_gl_filter *filter = vlc_gl_filter_LoadModule(gl, &vgl->api, "triangle", NULL, NULL, NULL);
    fprintf(stderr, "==== %p\n", filter);
    if (!filter)
        fprintf(stderr, "OOPS NO FILTER\n");

    vlc_gl_filters_Append(&vgl->filters, filter);

    vgl->sub_interop = vlc_gl_interop_New(gl, &vgl->api, NULL, fmt, true);
    if (!vgl->sub_interop)
    {
        msg_Err(gl, "Could not create sub interop");
        vlc_gl_filters_Close(&vgl->filters);
        vlc_gl_renderer_Delete(vgl->renderer);
        vlc_gl_interop_Delete(vgl->interop);
        free(vgl);
        return NULL;
    }

    vgl->sub_renderer =
        vlc_gl_sub_renderer_New(gl, &vgl->api, vgl->sub_interop);
    if (!vgl->sub_renderer)
    {
        msg_Err(gl, "Could not create sub renderer");
        vlc_gl_interop_Delete(vgl->sub_interop);
        vlc_gl_filters_Close(&vgl->filters);
        vlc_gl_renderer_Delete(vgl->renderer);
        vlc_gl_interop_Delete(vgl->interop);
        free(vgl);
        return NULL;
    }

    GL_ASSERT_NOERROR();

    if (renderer->fmt.projection_mode != PROJECTION_MODE_RECTANGULAR
     && vout_display_opengl_SetViewpoint(vgl, viewpoint) != VLC_SUCCESS)
    {
        vout_display_opengl_Delete(vgl);
        return NULL;
    }

    *fmt = renderer->fmt;
    if (subpicture_chromas) {
        *subpicture_chromas = gl_subpicture_chromas;
    }

    GL_ASSERT_NOERROR();
    return vgl;
}

void vout_display_opengl_Delete(vout_display_opengl_t *vgl)
{
    GL_ASSERT_NOERROR();

    const opengl_vtable_t *vt = &vgl->api.vt;

    /* */
    vt->Finish();
    vt->Flush();

    vlc_gl_sub_renderer_Delete(vgl->sub_renderer);
    vlc_gl_interop_Delete(vgl->sub_interop);

    vlc_gl_filters_Close(&vgl->filters);
    vlc_gl_renderer_Delete(vgl->renderer);
    vlc_gl_interop_Delete(vgl->interop);

    GL_ASSERT_NOERROR();

    free(vgl);
}

int vout_display_opengl_SetViewpoint(vout_display_opengl_t *vgl,
                                     const vlc_viewpoint_t *p_vp)
{
    return vlc_gl_renderer_SetViewpoint(vgl->renderer, p_vp);
}

void vout_display_opengl_SetWindowAspectRatio(vout_display_opengl_t *vgl,
                                              float f_sar)
{
    vlc_gl_renderer_SetWindowAspectRatio(vgl->renderer, f_sar);
}

void vout_display_opengl_Viewport(vout_display_opengl_t *vgl, int x, int y,
                                  unsigned width, unsigned height)
{
    const opengl_vtable_t *vt = &vgl->api.vt;
    vt->Viewport(x, y, width, height);
}

int vout_display_opengl_Prepare(vout_display_opengl_t *vgl,
                                picture_t *picture, subpicture_t *subpicture)
{
    GL_ASSERT_NOERROR();

    struct vlc_gl_filter *renderer_filter = vgl->renderer->filter;
    struct vlc_gl_sampler *renderer_sampler =
        vlc_gl_filter_GetSampler(renderer_filter);
    int ret = vlc_gl_sampler_UpdatePicture(renderer_sampler, picture);
    if (ret != VLC_SUCCESS)
        return ret;

    ret = vlc_gl_sub_renderer_Prepare(vgl->sub_renderer, subpicture);
    GL_ASSERT_NOERROR();
    return ret;
}
int vout_display_opengl_Display(vout_display_opengl_t *vgl)
{
    GL_ASSERT_NOERROR();

    /* Why drawing here and not in Render()? Because this way, the
       OpenGL providers can call vout_display_opengl_Display to force redraw.
       Currently, the OS X provider uses it to get a smooth window resizing */

    int ret = vlc_gl_filters_Draw(&vgl->filters);
    if (ret != VLC_SUCCESS)
        return ret;

    ret = vlc_gl_sub_renderer_Draw(vgl->sub_renderer);
    if (ret != VLC_SUCCESS)
        return ret;

    /* Display */
    vlc_gl_Swap(vgl->gl);

    GL_ASSERT_NOERROR();

    return VLC_SUCCESS;
}

