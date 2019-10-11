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

static const float MATRIX_COLOR_RANGE_LIMITED_TO_FULL[] = {
    255.f/219,         0,         0, -255.f/219 *  16.f/255,
            0, 255.f/224,         0, -255.f/224 * 128.f/255,
            0,         0, 255.f/224, -255.f/224 * 128.f/255,
};

/*
 * Construct the transformation matrix from the luma weight of the red and blue
 * component (the green component is deduced).
 */
#define MATRIX_YUV_TO_RGB(KR, KB) \
    MATRIX_YUV_TO_RGB_(KR, (1-(KR)-(KB)), KB)

/*
 * Construct the transformation matrix from the luma weight of the RGB
 * components.
 *
 * KR: luma weight of the red component
 * KG: luma weight of the green component
 * KB: luma weight of the blue component
 *
 * By definition, KR + KG + KB == 1.
 *
 * Ref: <https://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.601_conversion>
 * Ref: libplacebo: src/colorspace.c:luma_coeffs()
 * */
#define MATRIX_YUV_TO_RGB_(KR, KG, KB) { \
    1,                         0,              2*(1.f-(KR)), \
    1, -2*(1.f-(KB))*((KB)/(KG)), -2*(1.f-(KR))*((KR)/(KG)), \
    1,              2*(1.f-(KB)),                         0, \
}

static const float MATRIX_BT601[] = MATRIX_YUV_TO_RGB(0.299f, 0.114f);
static const float MATRIX_BT709[] = MATRIX_YUV_TO_RGB(0.2126f, 0.0722f);

static const char *const FRAGMENT_CODE_TEMPLATE =
    "uniform mat4x3 vlc_conv_matrix;\n"
    "uniform sampler2D vlc_planes[3];\n"
    "vec4 vlc_texture(vec2 c) {\n"
    "  vec2 coords = vlc_picture_coords(c);\n"
    "  vec4 pix_in = vec4(\n"
    "                    texture2D(vlc_planes[%d], coords).%c,\n"
    "                    texture2D(vlc_planes[%d], coords).%c,\n"
    "                    texture2D(vlc_planes[%d], coords).%c,\n"
    "                    1.0\n"
    "                  );\n"
    /* mat4x3 * vec4 -> vec3 */
    "  vec3 pix_out = vlc_conv_matrix * pix_in;\n"
    /* add alpha component */
    "  return vec4(pix_out, 1.0);\n"
    "}\n";

struct i420_sys {
    unsigned plane_count;
    float matrix[4 * 3];
    struct {
        GLint planes[3];
        GLint matrix;
    } loc;
};

static int
Prepare(const struct vlc_gl_shader_program *program, void *userdata)
{
    struct vlc_gl_chroma_converter *converter = userdata;
    struct i420_sys *sys = converter->sys;
    const struct opengl_vtable_t *vt = converter->vt;

    sys->loc.matrix =
        vt->GetUniformLocation(program->id, "vlc_conv_matrix");

    sys->loc.planes[0] = vt->GetUniformLocation(program->id, "vlc_planes[0]");
    sys->loc.planes[1] = vt->GetUniformLocation(program->id, "vlc_planes[1]");
    if (sys->plane_count > 2)
        sys->loc.planes[2] =
            vt->GetUniformLocation(program->id, "vlc_planes[2]");

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
    vt->Uniform1i(sys->loc.planes[0], 0);

    vt->ActiveTexture(GL_TEXTURE1);
    vt->BindTexture(GL_TEXTURE_2D, pic->textures[1]);
    vt->Uniform1i(sys->loc.planes[1], 1);

    if (sys->plane_count > 2)
    {
        vt->ActiveTexture(GL_TEXTURE2);
        vt->BindTexture(GL_TEXTURE_2D, pic->textures[2]);
        vt->Uniform1i(sys->loc.planes[2], 2);
    }

    vt->UniformMatrix4x3fv(sys->loc.matrix, 1, true, sys->matrix);

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

static void
init_conv_matrix(video_color_space_t color_space,
                 video_color_range_t color_range,
                 float conv_matrix_out[])
{
    const float *space_matrix;
    if (color_space == COLOR_SPACE_BT601)
        space_matrix = MATRIX_BT601;
    else
        space_matrix = MATRIX_BT709;

    if (color_range == COLOR_RANGE_FULL) {
        memcpy(conv_matrix_out, space_matrix, 3 * sizeof(float));
        conv_matrix_out[3] = 0;
        memcpy(conv_matrix_out + 4, space_matrix + 3, 3 * sizeof(float));
        conv_matrix_out[7] = 0;
        memcpy(conv_matrix_out + 8, space_matrix + 6, 3 * sizeof(float));
        conv_matrix_out[11] = 0;
    } else {
        // multiply the matrices on CPU once for all
        for (int x = 0; x < 4; ++x) {
            for (int y = 0; y < 3; ++y) {
                float sum = 0;
                for (int k = 0; k < 3; ++k) {
                    sum += space_matrix[y * 3 + k]
                         * MATRIX_COLOR_RANGE_LIMITED_TO_FULL[k * 4 + x];
                }
                conv_matrix_out[y * 4 + x] = sum;
            }
        }
    }
}

static const struct vlc_gl_chroma_converter_ops ops = {
    .close = Close,
};

static int
Open(struct vlc_gl_chroma_converter *converter,
     const video_format_t *fmt_in,
     const video_format_t *fmt_out,
     bool vflip,
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

    struct i420_sys *sys = converter->sys = malloc(sizeof(*sys));
    if (!converter->sys)
        return VLC_ENOMEM;

    init_conv_matrix(fmt_in->space, fmt_in->color_range, sys->matrix);

    char **fragment_codes = vlc_alloc(2, sizeof(*fragment_codes));
    if (!fragment_codes)
    {
        free(converter->sys);
        return VLC_ENOMEM;
    }

    const char *coords = vflip ? FRAGMENT_COORDS_VFLIPPED
                               : FRAGMENT_COORDS_NORMAL;
    fragment_codes[0] = strdup(coords);
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
    sampler_out->input_texture_first_index = 0;
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
