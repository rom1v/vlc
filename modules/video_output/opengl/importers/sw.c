/*****************************************************************************
 * sw.c: OpenGL importer
 *****************************************************************************
 * Copyright (C) 2019 Videolabs
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

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_modules.h>
#include <vlc_opengl.h>

#include "../importer.h"

struct sw_sys {
    int dummy;
};

static int
get_tex_format_size(struct vlc_gl_importer *importer, int target,
                    int tex_format, int tex_internal, int tex_type)
{
    const opengl_vtable_t *gl = importer->gl;
    if (!gl->GetTexLevelParameteriv)
        return -1;

    GLint tex_param_size;
    int mul = 1;
    switch (tex_format)
    {
        case GL_BGRA:
            mul = 4;
            /* fall through */
        case GL_RED:
        case GL_RG:
            tex_param_size = GL_TEXTURE_RED_SIZE;
            break;
        case GL_LUMINANCE:
            tex_param_size = GL_TEXTURE_LUMINANCE_SIZE;
            break;
        default:
            return -1;
    }

    GLuint texture;
    gl->GenTextures(1, &texture);
    gl->BindTexture(target, texture);
    gl->TexImage2D(target, 0, tex_internal, 64, 64, 0, tex_format, tex_type,
                   NULL);

    GLint size = 0;
    gl->GetTexLevelParameteriv(target, 0, tex_param_size, &size);

    gl->DeleteTextures(1, &texture);

    if (size > 0)
        size *= mul;
    return size;
}

static int
upload_plane(const struct vlc_gl_importer *importer, unsigned idx, GLsizei width,
             GLsizei height, unsigned pitch, unsigned visible_pitch,
             const void *pixels)
{
    const struct vlc_gl_tex_cfg *cfg = &importer->cfg[idx];
    const opengl_vtable_t *gl = importer->gl;

    /* This unpack alignment is the default, but setting it just in case. */
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // TODO handle !has_unpack_subimage
    if (visible_pitch == 0)
        visible_pitch = 1;
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, pitch * width / visible_pitch);
    gl->TexSubImage2D(importer->tex_target, 0, 0, 0, width, height, cfg->format,
                      cfg->type, pixels);
    return VLC_SUCCESS;
}

static int
AllocTextures(const struct vlc_gl_importer *importer, GLuint textures[],
              const GLsizei tex_width[], const GLsizei tex_height[])
{
    for (unsigned i = 0; i < importer->tex_count; ++i)
    {
        const opengl_vtable_t *gl = importer->gl;
        gl->BindTexture(importer->tex_target, textures[i]);
        gl->TexImage2D(importer->tex_target, 0, importer->cfg[i].internal,
                       tex_width[i], tex_height[i], 0, importer->cfg[i].format,
                       importer->cfg[i].type, NULL);
    }
    return VLC_SUCCESS;
}

static int
Import(const struct vlc_gl_importer *importer, GLuint textures[],
       const GLsizei tex_width[], const GLsizei tex_height[],
       picture_t *pic, const size_t plane_offsets[])
{
    const opengl_vtable_t *gl = importer->gl;

    for (unsigned i = 0; i < importer->tex_count; ++i)
    {
        assert(textures[i]);
        gl->ActiveTexture(GL_TEXTURE0 + i);
        gl->BindTexture(importer->tex_target, textures[i]);

        size_t offset = plane_offsets ? plane_offsets[i] : 0;
        const void *pixels = pic->p[i].p_pixels + offset;

        int ret =
            upload_plane(importer, i, tex_width[i], tex_height[i],
                         pic->p[i].i_pitch, pic->p[i].i_visible_pitch, pixels);
        if (ret != VLC_SUCCESS)
            return ret;
    }
    return VLC_SUCCESS;
}

static void
Close(struct vlc_gl_importer *importer)
{
    free(importer->sys);
}

static int
fill_cfg_xyz12(struct vlc_gl_importer *importer)
{
    // TODO
}

static int
fill_cfg_yuv(struct vlc_gl_importer *importer, vlc_fourcc_t chroma)
{
    // TODO
    return VLC_EGENERIC;
}

static int
fill_cfg_rgb(struct vlc_gl_importer *importer, vlc_fourcc_t chroma)
{
    switch (chroma)
    {
        case VLC_CODEC_RGB32:
        case VLC_CODEC_RGBA:
            importer->cfg[0] = (struct vlc_gl_tex_cfg) {
                .w = { 1, 1 },
                .h = { 1, 1 },
                .internal = GL_RGBA,
                .format = GL_RGBA,
                .type = GL_UNSIGNED_BYTE,
            };
            break;
        case VLC_CODEC_BGRA: {
            if (get_tex_format_size(importer, importer->tex_target, GL_BGRA,
                                    GL_RGBA, GL_UNSIGNED_BYTE) != 32)
                return VLC_EGENERIC;
            importer->cfg[0] = (struct vlc_gl_tex_cfg) {
                .w = { 1, 1 },
                .h = { 1, 1 },
                .internal = GL_RGBA,
                .format = GL_BGRA,
                .type = GL_UNSIGNED_BYTE,
            };
            break;
        }
        default:
            return VLC_EGENERIC;
    }

    importer->tex_count = 1;
    return VLC_SUCCESS;
}

static int
fill_cfg_for_chroma(struct vlc_gl_importer *importer, vlc_fourcc_t chroma)
{
    if (chroma == VLC_CODEC_XYZ12)
        return fill_cfg_xyz12(importer);

    if (vlc_fourcc_IsYUV(chroma))
        return fill_cfg_yuv(importer, chroma);

    return fill_cfg_rgb(importer, chroma);
}

static inline const vlc_fourcc_t *
fallback_list(vlc_fourcc_t chroma)
{
    if (chroma == VLC_CODEC_XYZ12)
    {
        static const vlc_fourcc_t xyz12_list[] = { VLC_CODEC_XYZ12, 0 };
        return xyz12_list;
    }

    return vlc_fourcc_GetFallback(chroma);
}

static int
fill_cfg(struct vlc_gl_importer *importer)
{
    const vlc_fourcc_t *list = fallback_list(importer->fmt.i_chroma);

    while (*list)
    {
        int ret = fill_cfg_for_chroma(importer, *list);
        if (ret == VLC_SUCCESS)
            return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
}

static const struct vlc_gl_importer_ops ops = {
    .alloc_textures = AllocTextures,
    .import = Import,
    .close = Close,
};

static int
Open(struct vlc_gl_importer *importer)
{
    struct sw_sys *sys = importer->sys = malloc(sizeof(*sys));
    if (!importer->sys)
        return VLC_ENOMEM;

    importer->ops = &ops;

    int ret = fill_cfg(importer);
    if (ret != VLC_SUCCESS)
        return ret;

    return VLC_SUCCESS;
}

vlc_module_begin ()
    set_description("OpenGL importer generic software")
    set_capability("glimporter", 0)
    set_callback(Open)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    add_shortcut("sw")
vlc_module_end ()
