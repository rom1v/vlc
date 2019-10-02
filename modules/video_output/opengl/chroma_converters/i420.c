/*****************************************************************************
 * i420.c: OpenGL chroma converter
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

#include "../internal.h"
#include "../filter.h"
#include "../chroma_converter.h"
#include "../converter.h"

static const char *const MATRIX_BT709_TO_RGB =
    "const mat3 conversion_matrix = mat3(\n"
    "    1.0,           1.0,          1.0,\n"
    "    0.0,          -0.21482,      2.12798,\n"
    "    1.28033,      -0.38059,      0.0\n"
    ");\n";

static const char *const MATRIX_BT601_TO_RGB =
    "const mat3 conversion_matrix = mat3(\n"
    "    1.0,           1.0,          1.0,\n"
    "    0.0,          -0.39465,      2.03211,\n"
    "    1.13983,      -0.5806,       0.0\n"
    ");\n";

static const char *const FRAGMENT_CODE_TEMPLATE =
    "uniform sampler2D planes[3];\n"
    "vec4 vlc_texture(vec2 coords) {\n"
    "  vec3 v = conversion_matrix * vec3(\n"
    "              texture2D(planes[%d], coords).%c,\n"
    "              texture2D(planes[%d], coords).%c - 0.5,\n"
    "              texture2D(planes[%d], coords).%c - 0.5);\n"
    "  return vec4(v, 1.0);\n"
    "}\n";

struct i420_sys {
    unsigned plane_count;
    GLint planes[3];
};

static int
Prepare(const struct vlc_gl_shader_program *program, void *userdata)
{
    struct vlc_gl_chroma_converter *converter = userdata;
    struct i420_sys *sys = converter->sys;
    const struct opengl_vtable_t *vt = converter->vt;

    sys->planes[0] = vt->GetUniformLocation(program->id, "planes[0]");
    sys->planes[1] = vt->GetUniformLocation(program->id, "planes[1]");
    if (sys->plane_count > 2)
        sys->planes[2] = vt->GetUniformLocation(program->id, "planes[2]");

    return VLC_SUCCESS;
}

static int
Load(const struct vlc_gl_picture *pic, void *userdata)
{
    struct vlc_gl_chroma_converter *converter = userdata;
    struct i420_sys *sys = converter->sys;
    const struct opengl_vtable_t *vt = converter->vt;

    assert(pic->texture_count >= 2);

    vt->ActiveTexture(GL_TEXTURE0);
    vt->BindTexture(GL_TEXTURE_2D, pic->textures[0]);
    vt->Uniform1i(sys->planes[0], 0);

    vt->ActiveTexture(GL_TEXTURE1);
    vt->BindTexture(GL_TEXTURE_2D, pic->textures[1]);
    vt->Uniform1i(sys->planes[1], 1);

    if (sys->plane_count > 2)
    {
        vt->ActiveTexture(GL_TEXTURE2);
        vt->BindTexture(GL_TEXTURE_2D, pic->textures[2]);
        vt->Uniform1i(sys->planes[2], 2);
    }

    return VLC_SUCCESS;
}

static void
Unload(const struct vlc_gl_picture *pic, void *userdata)
{
    (void) pic;

    struct vlc_gl_chroma_converter *converter = userdata;
    struct i420_sys *sys = converter->sys;
    const struct opengl_vtable_t *vt = converter->vt;

    assert(sys->plane_count >= 2);

    vt->ActiveTexture(GL_TEXTURE0);
    vt->BindTexture(GL_TEXTURE_2D, 0);

    vt->ActiveTexture(GL_TEXTURE1);
    vt->BindTexture(GL_TEXTURE_2D, 0);

    if (sys->plane_count > 2)
    {
        vt->ActiveTexture(GL_TEXTURE2);
        vt->BindTexture(GL_TEXTURE_2D, 0);
    }
}

static void
Close(struct vlc_gl_chroma_converter *converter)
{
    free(converter->sys);
}

static char *
gen_fragment_code(int p0, char c0, int p1, char c1, int p2, char c2)
{
    char *str;
    int ret = asprintf(&str, FRAGMENT_CODE_TEMPLATE, p0, c0, p1, c1, p2, c2);
    if (ret == -1)
        return NULL;
    return str;
}

static const struct vlc_gl_chroma_converter_ops ops = {
    .close = Close,
};

static int
Open(struct vlc_gl_chroma_converter *converter,
     const video_format_t *fmt_in,
     const video_format_t *fmt_out,
     struct vlc_gl_shader_sampler *sampler_out)
{
    switch (fmt_in->i_chroma)
    {
        case VLC_CODEC_I420:
        case VLC_CODEC_NV12:
            break;
        default:
            return VLC_EGENERIC;
    }

    if (fmt_out->i_chroma != VLC_CODEC_RGBA)
        return VLC_EGENERIC;

    const char *matrix;
    switch (fmt_in->primaries)
    {
        case COLOR_PRIMARIES_BT601_525:
        case COLOR_PRIMARIES_BT601_625:
            matrix = MATRIX_BT601_TO_RGB;
            break;
        case COLOR_PRIMARIES_BT709:
            matrix = MATRIX_BT709_TO_RGB;
            break;
        default:
            return VLC_EGENERIC;
    }

    struct i420_sys *sys = converter->sys = malloc(sizeof(*sys));
    if (!converter->sys)
        return VLC_ENOMEM;

    char **fragment_codes = vlc_alloc(2, sizeof(*fragment_codes));
    if (!fragment_codes)
    {
        free(converter->sys);
        return VLC_ENOMEM;
    }

    fragment_codes[0] = strdup(matrix);
    if (!fragment_codes[0])
    {
        free(fragment_codes);
        free(converter->sys);
        return VLC_ENOMEM;
    }

    unsigned input_plane_count;
    switch (fmt_in->i_chroma)
    {
        case VLC_CODEC_I420:
            /* plane 0: Y
             * plane 1: U
             * plane 2: V */
            fragment_codes[1] = gen_fragment_code(0, 'x', 1, 'x', 2, 'x');
            input_plane_count = 3;
            break;
        case VLC_CODEC_NV12:
            /* plane 0: Y
             * plane 1: UV */
            fragment_codes[1] = gen_fragment_code(0, 'x', 1, 'x', 1, 'y');
            input_plane_count = 2;
            break;
        default:
            vlc_assert_unreachable();
    }

    if (!fragment_codes[1])
    {
        free(fragment_codes[0]);
        free(fragment_codes);
        free(converter->sys);
        return VLC_ENOMEM;
    }

    sys->plane_count = input_plane_count;

    sampler_out->fragment_codes = fragment_codes;
    sampler_out->fragment_code_count = 2;
    sampler_out->input_texture_count = input_plane_count;
    sampler_out->prepare = Prepare;
    sampler_out->load = Load;
    sampler_out->unload = Unload;
    sampler_out->userdata = converter;

    converter->ops = &ops;

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_shortname("chroma converter I420 to RGB")
    set_description("OpenGL I420 to RGB chroma converter")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl chroma converter", 1000)
    set_callback(Open)
vlc_module_end()
