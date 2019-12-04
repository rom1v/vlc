/*****************************************************************************
 * converter_sw.c: OpenGL converters for software video formats
 *****************************************************************************
 * Copyright (C) 2016,2017 VLC authors and VideoLAN
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
#include <limits.h>
#include <stdlib.h>

#include <vlc_common.h>
#include "internal.h"

#ifndef GL_UNPACK_ROW_LENGTH
# define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

#ifndef GL_PIXEL_UNPACK_BUFFER
# define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_DYNAMIC_DRAW
# define GL_DYNAMIC_DRAW 0x88E8
#endif

#define PBO_DISPLAY_COUNT 2 /* Double buffering */
typedef struct
{
    PFNGLDELETEBUFFERSPROC DeleteBuffers;
    GLuint      buffers[PICTURE_PLANE_MAX];
    size_t      bytes[PICTURE_PLANE_MAX];
} picture_sys_t;

struct priv
{
    bool   has_unpack_subimage;
    void * texture_temp_buf;
    size_t texture_temp_buf_size;
    struct {
        picture_t *display_pics[PBO_DISPLAY_COUNT];
        size_t display_idx;
    } pbo;
};

static void
pbo_picture_destroy(picture_t *pic)
{
    picture_sys_t *picsys = pic->p_sys;

    picsys->DeleteBuffers(pic->i_planes, picsys->buffers);

    free(picsys);
}

static picture_t *
pbo_picture_create(const struct vlc_gl_importer *imp)
{
    picture_sys_t *picsys = calloc(1, sizeof(*picsys));
    if (unlikely(picsys == NULL))
        return NULL;

    picture_resource_t rsc = {
        .p_sys = picsys,
        .pf_destroy = pbo_picture_destroy,
    };
    picture_t *pic = picture_NewFromResource(&imp->fmt, &rsc);
    if (pic == NULL)
    {
        free(picsys);
        return NULL;
    }

    imp->vt->GenBuffers(pic->i_planes, picsys->buffers);
    picsys->DeleteBuffers = imp->vt->DeleteBuffers;

    /* XXX: needed since picture_NewFromResource override pic planes */
    if (picture_Setup(pic, &imp->fmt))
    {
        picture_Release(pic);
        return NULL;
    }

    assert(pic->i_planes > 0
        && (unsigned) pic->i_planes == imp->tex_count);

    for (int i = 0; i < pic->i_planes; ++i)
    {
        const plane_t *p = &pic->p[i];

        if( p->i_pitch < 0 || p->i_lines <= 0 ||
            (size_t)p->i_pitch > SIZE_MAX/p->i_lines )
        {
            picture_Release(pic);
            return NULL;
        }
        picsys->bytes[i] = p->i_pitch * p->i_lines;
    }
    return pic;
}

static int
pbo_data_alloc(const struct vlc_gl_importer *imp, picture_t *pic)
{
    picture_sys_t *picsys = pic->p_sys;

    imp->vt->GetError();

    for (int i = 0; i < pic->i_planes; ++i)
    {
        imp->vt->BindBuffer(GL_PIXEL_UNPACK_BUFFER, picsys->buffers[i]);
        imp->vt->BufferData(GL_PIXEL_UNPACK_BUFFER, picsys->bytes[i], NULL,
                            GL_DYNAMIC_DRAW);

        if (imp->vt->GetError() != GL_NO_ERROR)
        {
            msg_Err(imp->gl, "could not alloc PBO buffers");
            imp->vt->DeleteBuffers(i, picsys->buffers);
            return VLC_EGENERIC;
        }
    }
    return VLC_SUCCESS;
}

static int
pbo_pics_alloc(const struct vlc_gl_importer *imp)
{
    struct priv *priv = imp->priv;
    for (size_t i = 0; i < PBO_DISPLAY_COUNT; ++i)
    {
        picture_t *pic = priv->pbo.display_pics[i] = pbo_picture_create(imp);
        if (pic == NULL)
            goto error;

        if (pbo_data_alloc(imp, pic) != VLC_SUCCESS)
            goto error;
    }

    /* turn off pbo */
    imp->vt->BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    return VLC_SUCCESS;
error:
    for (size_t i = 0; i < PBO_DISPLAY_COUNT && priv->pbo.display_pics[i]; ++i)
        picture_Release(priv->pbo.display_pics[i]);
    return VLC_EGENERIC;
}

static int
tc_pbo_update(const struct vlc_gl_importer *imp, GLuint *textures,
              const GLsizei *tex_width, const GLsizei *tex_height,
              picture_t *pic, const size_t *plane_offset)
{
    (void) plane_offset; assert(plane_offset == NULL);
    struct priv *priv = imp->priv;

    picture_t *display_pic = priv->pbo.display_pics[priv->pbo.display_idx];
    picture_sys_t *p_sys = display_pic->p_sys;
    priv->pbo.display_idx = (priv->pbo.display_idx + 1) % PBO_DISPLAY_COUNT;

    for (int i = 0; i < pic->i_planes; i++)
    {
        GLsizeiptr size = pic->p[i].i_lines * pic->p[i].i_pitch;
        const GLvoid *data = pic->p[i].p_pixels;
        imp->vt->BindBuffer(GL_PIXEL_UNPACK_BUFFER,
                           p_sys->buffers[i]);
        imp->vt->BufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, size, data);

        imp->vt->ActiveTexture(GL_TEXTURE0 + i);
        imp->vt->BindTexture(imp->tex_target, textures[i]);

        imp->vt->PixelStorei(GL_UNPACK_ROW_LENGTH, pic->p[i].i_pitch
            * tex_width[i] / (pic->p[i].i_visible_pitch ? pic->p[i].i_visible_pitch : 1));

        imp->vt->TexSubImage2D(imp->tex_target, 0, 0, 0, tex_width[i], tex_height[i],
                               imp->texs[i].format, imp->texs[i].type, NULL);
        imp->vt->PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    /* turn off pbo */
    imp->vt->BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    return VLC_SUCCESS;
}

static int
tc_common_allocate_textures(const struct vlc_gl_importer *imp, GLuint *textures,
                            const GLsizei *tex_width, const GLsizei *tex_height)
{
    for (unsigned i = 0; i < imp->tex_count; i++)
    {
        imp->vt->BindTexture(imp->tex_target, textures[i]);
        imp->vt->TexImage2D(imp->tex_target, 0, imp->texs[i].internal,
                           tex_width[i], tex_height[i], 0, imp->texs[i].format,
                           imp->texs[i].type, NULL);
    }
    return VLC_SUCCESS;
}

static int
upload_plane(const struct vlc_gl_importer *imp, unsigned tex_idx,
             GLsizei width, GLsizei height,
             unsigned pitch, unsigned visible_pitch, const void *pixels)
{
    struct priv *priv = imp->priv;
    GLenum tex_format = imp->texs[tex_idx].format;
    GLenum tex_type = imp->texs[tex_idx].type;

    /* This unpack alignment is the default, but setting it just in case. */
    imp->vt->PixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (!priv->has_unpack_subimage)
    {
        if (pitch != visible_pitch)
        {
#define ALIGN(x, y) (((x) + ((y) - 1)) & ~((y) - 1))
            visible_pitch = ALIGN(visible_pitch, 4);
#undef ALIGN
            size_t buf_size = visible_pitch * height;
            const uint8_t *source = pixels;
            uint8_t *destination;
            if (priv->texture_temp_buf_size < buf_size)
            {
                priv->texture_temp_buf =
                    realloc_or_free(priv->texture_temp_buf, buf_size);
                if (priv->texture_temp_buf == NULL)
                {
                    priv->texture_temp_buf_size = 0;
                    return VLC_ENOMEM;
                }
                priv->texture_temp_buf_size = buf_size;
            }
            destination = priv->texture_temp_buf;

            for (GLsizei h = 0; h < height ; h++)
            {
                memcpy(destination, source, visible_pitch);
                source += pitch;
                destination += visible_pitch;
            }
            imp->vt->TexSubImage2D(imp->tex_target, 0, 0, 0, width, height,
                                   tex_format, tex_type, priv->texture_temp_buf);
        }
        else
        {
            imp->vt->TexSubImage2D(imp->tex_target, 0, 0, 0, width, height,
                                   tex_format, tex_type, pixels);
        }
    }
    else
    {
        imp->vt->PixelStorei(GL_UNPACK_ROW_LENGTH, pitch * width / (visible_pitch ? visible_pitch : 1));
        imp->vt->TexSubImage2D(imp->tex_target, 0, 0, 0, width, height,
                              tex_format, tex_type, pixels);
        imp->vt->PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    return VLC_SUCCESS;
}

static int
tc_common_update(const struct vlc_gl_importer *imp, GLuint *textures,
                 const GLsizei *tex_width, const GLsizei *tex_height,
                 picture_t *pic, const size_t *plane_offset)
{
    int ret = VLC_SUCCESS;
    for (unsigned i = 0; i < imp->tex_count && ret == VLC_SUCCESS; i++)
    {
        assert(textures[i] != 0);
        imp->vt->ActiveTexture(GL_TEXTURE0 + i);
        imp->vt->BindTexture(imp->tex_target, textures[i]);
        const void *pixels = plane_offset != NULL ?
                             &pic->p[i].p_pixels[plane_offset[i]] :
                             pic->p[i].p_pixels;

        ret = upload_plane(imp, i, tex_width[i], tex_height[i],
                           pic->p[i].i_pitch, pic->p[i].i_visible_pitch, pixels);
    }
    return ret;
}

int
opengl_importer_generic_init(struct vlc_gl_importer *imp, bool allow_dr)
{
    video_color_space_t space;
    const vlc_fourcc_t *list;

    if (vlc_fourcc_IsYUV(imp->fmt.i_chroma))
    {
        GLint max_texture_units = 0;
        imp->vt->GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
        if (max_texture_units < 3)
            return VLC_EGENERIC;

        list = vlc_fourcc_GetYUVFallback(imp->fmt.i_chroma);
        space = imp->fmt.space;
    }
    else if (imp->fmt.i_chroma == VLC_CODEC_XYZ12)
    {
        static const vlc_fourcc_t xyz12_list[] = { VLC_CODEC_XYZ12, 0 };
        list = xyz12_list;
        space = COLOR_SPACE_UNDEF;
    }
    else
    {
        list = vlc_fourcc_GetRGBFallback(imp->fmt.i_chroma);
        space = COLOR_SPACE_UNDEF;
    }

    int ret = VLC_EGENERIC;
    while (*list)
    {
        ret = opengl_importer_init(imp, GL_TEXTURE_2D, *list, space);
        if (ret == VLC_SUCCESS)
        {
            imp->fmt.i_chroma = *list;

            if (imp->fmt.i_chroma == VLC_CODEC_RGB32)
            {
#if defined(WORDS_BIGENDIAN)
                imp->fmt.i_rmask  = 0xff000000;
                imp->fmt.i_gmask  = 0x00ff0000;
                imp->fmt.i_bmask  = 0x0000ff00;
#else
                imp->fmt.i_rmask  = 0x000000ff;
                imp->fmt.i_gmask  = 0x0000ff00;
                imp->fmt.i_bmask  = 0x00ff0000;
#endif
                video_format_FixRgb(&imp->fmt);
            }
            break;
        }
        list++;
    }
    if (ret != VLC_SUCCESS)
        return ret;

    struct priv *priv = imp->priv = calloc(1, sizeof(struct priv));
    if (unlikely(priv == NULL))
        return VLC_ENOMEM;

    static const struct vlc_gl_importer_ops ops = {
        .allocate_textures = tc_common_allocate_textures,
        .update_textures = tc_common_update,
    };
    imp->ops = &ops;

    /* OpenGL or OpenGL ES2 with GL_EXT_unpack_subimage ext */
    priv->has_unpack_subimage =
        !imp->is_gles || vlc_gl_StrHasToken(imp->glexts, "GL_EXT_unpack_subimage");

    if (allow_dr && priv->has_unpack_subimage)
    {
        /* Ensure we do direct rendering / PBO with OpenGL 3.0 or higher. */
        const unsigned char *ogl_version = imp->vt->GetString(GL_VERSION);
        const bool glver_ok = strverscmp((const char *)ogl_version, "3.0") >= 0;

        const bool has_pbo = glver_ok &&
            (vlc_gl_StrHasToken(imp->glexts, "GL_ARB_pixel_buffer_object") ||
             vlc_gl_StrHasToken(imp->glexts, "GL_EXT_pixel_buffer_object"));

        const bool supports_pbo = has_pbo && imp->vt->BufferData
            && imp->vt->BufferSubData;
        if (supports_pbo && pbo_pics_alloc(imp) == VLC_SUCCESS)
        {
            static const struct vlc_gl_importer_ops pbo_ops = {
                .allocate_textures = tc_common_allocate_textures,
                .update_textures = tc_pbo_update,
            };
            imp->ops = &pbo_ops;
            msg_Dbg(imp->gl, "PBO support enabled");
        }
    }

    return VLC_SUCCESS;
}

void
opengl_importer_generic_deinit(struct vlc_gl_importer *imp)
{
    struct priv *priv = imp->priv;
    for (size_t i = 0; i < PBO_DISPLAY_COUNT && priv->pbo.display_pics[i]; ++i)
        picture_Release(priv->pbo.display_pics[i]);
    free(priv->texture_temp_buf);
    free(priv);
}
