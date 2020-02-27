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

#include <vlc_common.h>
#include <vlc_plugin.h>
#include "interop.h"
#include "../android/utils.h"
#include "gl_util.h"

struct priv
{
    android_video_context_t *avctx;
    AWindowHandler *awh;
    float transform_mtx[16];
    bool stex_attached;
};

static int
tc_anop_allocate_textures(const struct vlc_gl_interop *interop, GLuint *textures,
                          const GLsizei *tex_width, const GLsizei *tex_height)
{
    (void) tex_width; (void) tex_height;
    struct priv *priv = interop->priv;
    assert(textures[0] != 0);
    if (SurfaceTexture_attachToGLContext(priv->awh, textures[0]) != 0)
    {
        msg_Err(interop->gl, "SurfaceTexture_attachToGLContext failed");
        return VLC_EGENERIC;
    }
    priv->stex_attached = true;
    return VLC_SUCCESS;
}

static int
tc_anop_update(const struct vlc_gl_interop *interop, GLuint *textures,
               const GLsizei *tex_width, const GLsizei *tex_height,
               picture_t *pic, const size_t *plane_offset)
{
    (void) tex_width; (void) tex_height; (void) plane_offset;
    assert(pic->context);
    assert(textures[0] != 0);

    if (plane_offset != NULL)
        return VLC_EGENERIC;

    struct priv *priv = interop->priv;

    if (!priv->avctx->render(pic->context))
        return VLC_SUCCESS; /* already rendered */

    const float *mtx;
    if (SurfaceTexture_waitAndUpdateTexImage(priv->awh, &mtx)
        != VLC_SUCCESS)
        return VLC_EGENERIC;

    if (!mtx)
        memcpy(priv->transform_mtx, MATRIX4_IDENTITY, sizeof(MATRIX4_IDENTITY));
    else
    {
        /* The transform matrix given by the SurfaceTexture is not using the
         * same origin than us. Apply an additional vertical flip. */
        for (int row = 0; row < 4; ++row)
        {
            /* Multiply mtx by a vertical flip:
             *
             *          / 1  0  0  0 \
             *  VFlip = | 0 -1  0  1 |
             *          | 0  0  1  0 |
             *          \ 0  0  0  1 /
             *
             * This negates the second column, and adds the second and the
             * fourth into the fourth:
             *
             *                 / mtx_00 -mtx_01  mtx_02  mtx_01+mtx_03 \
             * transform_mtx = | mtx_10 -mtx_11  mtx_12  mtx_11+mtx_13 |
             *                 | mtx_20 -mtx_21  mtx_22  mtx_21+mtx_23 |
             *                 \ mtx_30 -mtx_31  mtx_32  mtx_31+mtx_33 /
             */
            priv->transform_mtx[0*4 + row] = mtx[0*4 + row];
            priv->transform_mtx[1*4 + row] = -mtx[1*4 + row];
            priv->transform_mtx[2*4 + row] = mtx[2*4 + row];
            priv->transform_mtx[3*4 + row] = mtx[1*4 + row] + mtx[3*4 + row];
        }
    }

    interop->vt->ActiveTexture(GL_TEXTURE0);
    interop->vt->BindTexture(interop->tex_target, textures[0]);

    return VLC_SUCCESS;
}

static const float *
tc_get_transform_matrix(const struct vlc_gl_interop *interop)
{
    struct priv *priv = interop->priv;
    return priv->transform_mtx;
}

static void
Close(struct vlc_gl_interop *interop)
{
    struct priv *priv = interop->priv;

    if (priv->stex_attached)
        SurfaceTexture_detachFromGLContext(priv->awh);

    free(priv);
}

static int
Open(vlc_object_t *obj)
{
    struct vlc_gl_interop *interop = (void *) obj;

    if (interop->fmt.i_chroma != VLC_CODEC_ANDROID_OPAQUE
     || !interop->gl->surface->handle.anativewindow
     || !interop->vctx)
        return VLC_EGENERIC;

    android_video_context_t *avctx =
        vlc_video_context_GetPrivate(interop->vctx, VLC_VIDEO_CONTEXT_AWINDOW);

    if (avctx->id != AWindow_SurfaceTexture)
        return VLC_EGENERIC;

    interop->priv = malloc(sizeof(struct priv));
    if (unlikely(interop->priv == NULL))
        return VLC_ENOMEM;

    struct priv *priv = interop->priv;
    priv->avctx = avctx;
    priv->awh = interop->gl->surface->handle.anativewindow;
    memcpy(priv->transform_mtx, MATRIX4_IDENTITY, sizeof(MATRIX4_IDENTITY));
    priv->stex_attached = false;

    static const struct vlc_gl_interop_ops ops = {
        .allocate_textures = tc_anop_allocate_textures,
        .update_textures = tc_anop_update,
        .get_transform_matrix = tc_get_transform_matrix,
        .close = Close,
    };
    interop->ops = &ops;

    int ret = opengl_interop_init(interop, GL_TEXTURE_EXTERNAL_OES,
                                  VLC_CODEC_RGB32,
                                  COLOR_SPACE_UNDEF);

    if (ret != VLC_SUCCESS)
    {
        free(priv);
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

vlc_module_begin ()
    set_description("Android OpenGL SurfaceTexture converter")
    set_capability("glinterop", 1)
    set_callback(Open)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
vlc_module_end ()
