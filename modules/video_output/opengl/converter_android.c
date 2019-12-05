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
tc_anop_allocate_textures(const struct vlc_gl_importer *imp, GLuint *textures,
                          const GLsizei *tex_width, const GLsizei *tex_height)
{
    (void) tex_width; (void) tex_height;
    struct priv *priv = imp->priv;
    assert(textures[0] != 0);
    if (SurfaceTexture_attachToGLContext(priv->awh, textures[0]) != 0)
    {
        msg_Err(imp->gl, "SurfaceTexture_attachToGLContext failed");
        return VLC_EGENERIC;
    }
    priv->stex_attached = true;
    return VLC_SUCCESS;
}

static int
tc_anop_update(const struct vlc_gl_importer *imp, GLuint *textures,
               const GLsizei *tex_width, const GLsizei *tex_height,
               picture_t *pic, const size_t *plane_offset)
{
    (void) tex_width; (void) tex_height; (void) plane_offset;
    assert(pic->context);
    assert(textures[0] != 0);

    if (plane_offset != NULL)
        return VLC_EGENERIC;

    struct priv *priv = imp->priv;

    if (!priv->avctx->render(pic->context))
        return VLC_SUCCESS; /* already rendered */

    if (SurfaceTexture_waitAndUpdateTexImage(priv->awh, &priv->transform_mtx)
        != VLC_SUCCESS)
    {
        priv->transform_mtx = NULL;
        return VLC_EGENERIC;
    }

    imp->vt->ActiveTexture(GL_TEXTURE0);
    imp->vt->BindTexture(imp->tex_target, textures[0]);

    return VLC_SUCCESS;
}

static const float *
tc_get_transform_matrix(const struct vlc_gl_importer *imp)
{
    struct priv *priv = imp->priv;
    return priv->transform_mtx;
}

static void
Close(vlc_object_t *obj)
{
    struct vlc_gl_importer *imp = (void *)obj;
    struct priv *priv = imp->priv;

    if (priv->stex_attached)
        SurfaceTexture_detachFromGLContext(priv->awh);

    free(priv);
}

static int
Open(vlc_object_t *obj)
{
    struct vlc_gl_importer *imp = (void *) obj;

    if (imp->fmt.i_chroma != VLC_CODEC_ANDROID_OPAQUE
     || !imp->gl->surface->handle.anativewindow
     || !imp->vctx)
        return VLC_EGENERIC;

    android_video_context_t *avctx =
        vlc_video_context_GetPrivate(imp->vctx, VLC_VIDEO_CONTEXT_AWINDOW);

    if (avctx->id != AWindow_SurfaceTexture)
        return VLC_EGENERIC;

    imp->priv = malloc(sizeof(struct priv));
    if (unlikely(imp->priv == NULL))
        return VLC_ENOMEM;

    struct priv *priv = imp->priv;
    priv->avctx = avctx;
    priv->awh = imp->gl->surface->handle.anativewindow;
    priv->transform_mtx = NULL;
    priv->stex_attached = false;

    static const struct vlc_gl_importer_ops ops = {
        .allocate_textures = tc_anop_allocate_textures,
        .update_textures = tc_anop_update,
        .get_transform_matrix = tc_get_transform_matrix,
    };
    imp->ops = &ops;

    /* The transform Matrix (uSTMatrix) given by the SurfaceTexture is not
     * using the same origin than us. Ask the caller to rotate textures
     * coordinates, via the vertex shader, by forcing an orientation. */
    switch (imp->fmt.orientation)
    {
        case ORIENT_TOP_LEFT:
            imp->fmt.orientation = ORIENT_BOTTOM_LEFT;
            break;
        case ORIENT_TOP_RIGHT:
            imp->fmt.orientation = ORIENT_BOTTOM_RIGHT;
            break;
        case ORIENT_BOTTOM_LEFT:
            imp->fmt.orientation = ORIENT_TOP_LEFT;
            break;
        case ORIENT_BOTTOM_RIGHT:
            imp->fmt.orientation = ORIENT_TOP_RIGHT;
            break;
        case ORIENT_LEFT_TOP:
            imp->fmt.orientation = ORIENT_RIGHT_TOP;
            break;
        case ORIENT_LEFT_BOTTOM:
            imp->fmt.orientation = ORIENT_RIGHT_BOTTOM;
            break;
        case ORIENT_RIGHT_TOP:
            imp->fmt.orientation = ORIENT_LEFT_TOP;
            break;
        case ORIENT_RIGHT_BOTTOM:
            imp->fmt.orientation = ORIENT_LEFT_BOTTOM;
            break;
    }

    int ret = opengl_importer_init(imp, GL_TEXTURE_EXTERNAL_OES,
                                        VLC_CODEC_RGB32,
                                        COLOR_SPACE_UNDEF);

    if (ret != VLC_SUCCESS)
    {
        free(imp->priv);
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
