/*****************************************************************************
 * converter_android.c: OpenGL Android opaque converter
 *****************************************************************************
 * Copyright (C) 2016 VLC authors and VideoLAN
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

#ifndef __ANDROID__
# error this file must be built from android
#endif

#include "converter.h"
#include "../android/utils.h"

struct priv
{
    android_video_context_t *avctx;
    AWindowHandler *awh;
    const float *transform_mtx;
    bool stex_attached;
};

static int
tc_anop_allocate_textures(const opengl_tex_converter_t *tc, GLuint *textures,
                          const GLsizei *tex_width, const GLsizei *tex_height)
{
    (void) tex_width; (void) tex_height;
    struct priv *priv = tc->priv;
    assert(textures[0] != 0);
    if (SurfaceTexture_attachToGLContext(priv->awh, textures[0]) != 0)
    {
        msg_Err(tc->gl, "SurfaceTexture_attachToGLContext failed");
        return VLC_EGENERIC;
    }
    priv->stex_attached = true;
    return VLC_SUCCESS;
}

static int
tc_anop_update(const opengl_tex_converter_t *tc, GLuint *textures,
               const GLsizei *tex_width, const GLsizei *tex_height,
               picture_t *pic, const size_t *plane_offset)
{
    (void) tex_width; (void) tex_height; (void) plane_offset;
    assert(pic->context);
    assert(textures[0] != 0);

    if (plane_offset != NULL)
        return VLC_EGENERIC;

    struct priv *priv = tc->priv;

    if (!priv->avctx->render(pic->context))
        return VLC_SUCCESS; /* already rendered */

    if (SurfaceTexture_waitAndUpdateTexImage(priv->awh, &priv->transform_mtx)
        != VLC_SUCCESS)
    {
        priv->transform_mtx = NULL;
        return VLC_EGENERIC;
    }

    tc->vt->ActiveTexture(GL_TEXTURE0);
    tc->vt->BindTexture(tc->tex_target, textures[0]);

    return VLC_SUCCESS;
}

static const float *
tc_get_transform_matrix(const opengl_tex_converter_t *tc)
{
    struct priv *priv = tc->priv;
    return priv->transform_mtx;
}

static void
Close(vlc_object_t *obj)
{
    opengl_tex_converter_t *tc = (void *)obj;
    struct priv *priv = tc->priv;

    if (priv->stex_attached)
        SurfaceTexture_detachFromGLContext(priv->awh);

    free(priv);
}

static int
Open(vlc_object_t *obj)
{
    opengl_tex_converter_t *tc = (void *) obj;

    if (tc->fmt.i_chroma != VLC_CODEC_ANDROID_OPAQUE
     || !tc->gl->surface->handle.anativewindow
     || !tc->vctx)
        return VLC_EGENERIC;

    android_video_context_t *avctx =
        vlc_video_context_GetPrivate(tc->vctx, VLC_VIDEO_CONTEXT_AWINDOW);

    if (avctx->id != AWindow_SurfaceTexture)
        return VLC_EGENERIC;

    tc->priv = malloc(sizeof(struct priv));
    if (unlikely(tc->priv == NULL))
        return VLC_ENOMEM;

    struct priv *priv = tc->priv;
    priv->avctx = avctx;
    priv->awh = tc->gl->surface->handle.anativewindow;
    priv->transform_mtx = NULL;
    priv->stex_attached = false;

    tc->pf_allocate_textures = tc_anop_allocate_textures;
    tc->pf_update         = tc_anop_update;
    tc->pf_get_transform_matrix = tc_get_transform_matrix;

    /* The transform Matrix (uSTMatrix) given by the SurfaceTexture is not
     * using the same origin than us. Ask the caller to rotate textures
     * coordinates, via the vertex shader, by forcing an orientation. */
    switch (tc->fmt.orientation)
    {
        case ORIENT_TOP_LEFT:
            tc->fmt.orientation = ORIENT_BOTTOM_LEFT;
            break;
        case ORIENT_TOP_RIGHT:
            tc->fmt.orientation = ORIENT_BOTTOM_RIGHT;
            break;
        case ORIENT_BOTTOM_LEFT:
            tc->fmt.orientation = ORIENT_TOP_LEFT;
            break;
        case ORIENT_BOTTOM_RIGHT:
            tc->fmt.orientation = ORIENT_TOP_RIGHT;
            break;
        case ORIENT_LEFT_TOP:
            tc->fmt.orientation = ORIENT_RIGHT_TOP;
            break;
        case ORIENT_LEFT_BOTTOM:
            tc->fmt.orientation = ORIENT_RIGHT_BOTTOM;
            break;
        case ORIENT_RIGHT_TOP:
            tc->fmt.orientation = ORIENT_LEFT_TOP;
            break;
        case ORIENT_RIGHT_BOTTOM:
            tc->fmt.orientation = ORIENT_LEFT_BOTTOM;
            break;
    }

    tc->fshader = opengl_fragment_shader_init(tc, GL_TEXTURE_EXTERNAL_OES,
                                              VLC_CODEC_RGB32,
                                              COLOR_SPACE_UNDEF);
    if (!tc->fshader)
    {
        free(tc->priv);
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

vlc_module_begin ()
    set_description("Android OpenGL SurfaceTexture converter")
    set_capability("glconv", 1)
    set_callbacks(Open, Close)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
vlc_module_end ()
