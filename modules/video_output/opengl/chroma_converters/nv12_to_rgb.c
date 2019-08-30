/*****************************************************************************
 * nv12_to_rgb.c: OpenGL chroma converter
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
#include "../converter.h"
#include "../../placebo_utils.h"

struct vlc_gl_filter_sys
{
    struct vlc_gl_shader_program *program;

    GLuint vbo; // vertex buffer object
    struct {
        GLint vertex_pos;
        GLint yuv_to_rgb;
        GLint planes[3];
    } loc;
};

// TODO tex_coord en attribute

static const char *vertex_shader =
    "#version 300 es\n"
    "in vec2 vertex_pos;\n"
    "out vec2 tex_coord;\n"
    "void main() {\n"
    " gl_Position = vec4(vertex_pos, 0.0, 1.0);\n"
    " tex_coord = vec2( (vertex_pos.x + 1.0) / 2.0,\n"
    "                   (vertex_pos.y + 1.0) / 2.0);\n"
    "}";

static const char *fragment_shader_header =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 tex_coord;\n"
    "uniform sampler2D planes[3];\n"
    "out vec4 frag_color;\n";

static const char *fragment_shader_main =
    "void main() {\n"
    " vec3 rgb = conversion_matrix * color_sample();\n"
    " frag_color = vec4(rgb, 1.0);\n"
    "}";

static const char *fragment_sample_nv12 =
    "vec3 color_sample() {\n"
    " return vec3(\n"
    "             texture2D(planes[0], tex_coord).x,\n"
    "             texture2D(planes[1], tex_coord).x - 0.5,\n"
    "             texture2D(planes[1], tex_coord).y - 0.5);\n"
    "}\n";

static const char *fragment_sample_i420 =
    "/* i420 sampling */\n"
    "vec3 color_sample() {\n"
    " return vec3(\n"
    "             texture2D(planes[0], tex_coord).x,\n"
    "             texture2D(planes[1], tex_coord).x - 0.5,\n"
    "             texture2D(planes[2], tex_coord).x - 0.5);\n"
    "}\n";
static const char *conversion_matrix_bt709_to_rgb =
    "/* bt709 -> RGB */\n"
    "const mat3 conversion_matrix = mat3(\n"
    "    1.0,           1.0,          1.0,\n"
    "    0.0,          -0.21482,      2.12798,\n"
    "    1.28033,      -0.38059,      0.0\n"
    ");\n";

static const char *conversion_matrix_bt601_to_rgb =
    "/* bt601 -> RGB */\n"
    "const mat3 conversion_matrix = mat3(\n"
    "    1.0,           1.0,          1.0,\n"
    "    1.0,          -0.39465,      2.03211,\n"
    "    1.13983,      -0.5806,       0.0\n"
    ");\n";

static int FilterInput(struct vlc_gl_filter *filter,
                       const struct vlc_gl_filter_input *input)
{
    struct vlc_gl_filter_sys *sys = filter->sys;

    //assert(input->picture.texture_count == 2);

    GLuint program = vlc_gl_shader_program_GetId(sys->program);
    filter->vt->UseProgram(program);

    const GLfloat vertexCoord[] = {
        -1, 1,
        -1, -1,
        1, 1,
        1, -1,
    };

    // TODO use the right matrix

    const struct vlc_gl_picture *pic = &input->picture;

    // Y-plane
    filter->vt->ActiveTexture(GL_TEXTURE0);
    filter->vt->BindTexture(GL_TEXTURE_2D, pic->textures[0]);

    // UV-plane
    filter->vt->ActiveTexture(GL_TEXTURE1);
    filter->vt->BindTexture(GL_TEXTURE_2D, pic->textures[1]);

    /* NV12 */
    filter->vt->Uniform1i(sys->loc.planes[0], 0);
    filter->vt->Uniform1i(sys->loc.planes[1], 1);

    /* I420 */
    filter->vt->Uniform1i(sys->loc.planes[2], 2);

    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoord), vertexCoord, GL_STATIC_DRAW);
    filter->vt->EnableVertexAttribArray(sys->loc.vertex_pos);
    filter->vt->VertexAttribPointer(sys->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0, (const void *) 0);

    filter->vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    filter->vt->ActiveTexture(GL_TEXTURE0);
    filter->vt->BindTexture(GL_TEXTURE_2D, 0);
    filter->vt->ActiveTexture(GL_TEXTURE1);
    filter->vt->BindTexture(GL_TEXTURE_2D, 0);
    filter->vt->ActiveTexture(GL_TEXTURE2);
    filter->vt->BindTexture(GL_TEXTURE_2D, 0);

    return VLC_SUCCESS;
}

static void FilterClose(struct vlc_gl_filter *filter)
{
    struct vlc_gl_filter_sys *sys = filter->sys;
    vlc_gl_shader_program_Release(sys->program);
    filter->vt->DeleteBuffers(1, &sys->vbo);
}

static struct vlc_gl_shader_program *
create_program(struct vlc_gl_filter *filter, const video_format_t *fmt_in)
{
    struct vlc_gl_shader_builder *builder =
        vlc_gl_shader_builder_Create(filter->vt, NULL, NULL);
    if (!builder)
    {
        msg_Err(filter, "cannot alloc vlc_gl_shader_builder");
        return NULL;
    }

    int ret;
    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_VERTEX, NULL, 0,
                                           &vertex_shader, 1);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "cannot attach vertex shader");
        vlc_gl_shader_builder_Release(builder);
        return NULL;
    }

    const char *sample_function = NULL;
    const char *conversion_matrix = NULL;

    switch (fmt_in->i_chroma)
    {
        case VLC_CODEC_NV12:
            sample_function = fragment_sample_nv12;
            break;
        case VLC_CODEC_I420:
            sample_function = fragment_sample_i420;
            break;
        default:
            break;
    }

    switch (fmt_in->primaries)
    {
        case COLOR_PRIMARIES_BT601_525:
        case COLOR_PRIMARIES_BT601_625:
            conversion_matrix = conversion_matrix_bt601_to_rgb;
            break;
        case COLOR_PRIMARIES_BT709:
            conversion_matrix = conversion_matrix_bt709_to_rgb;
            break;
        default:
            break;
    }

    /* TODO */
    if (sample_function == NULL || conversion_matrix == NULL)
        return NULL;

    const char *fragment_sources[] = {
        fragment_shader_header,
        conversion_matrix,
        sample_function,
        fragment_shader_main
    };

    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_FRAGMENT, NULL, 0,
                                           fragment_sources, ARRAY_SIZE(fragment_sources));
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "cannot attach fragment shader");
        vlc_gl_shader_builder_Release(builder);
        return NULL;
    }

    struct vlc_gl_shader_program *program =
        vlc_gl_shader_program_Create(builder);

    vlc_gl_shader_builder_Release(builder);

    return program;
}

static int
Open(struct vlc_gl_filter *filter,
     const video_format_t *fmt_in,
     const video_format_t *fmt_out)
{
    switch (fmt_in->i_chroma)
    {
        case VLC_CODEC_NV12:
        case VLC_CODEC_I420:
            break;
        default:
            return VLC_EGENERIC;
    }

    if (fmt_out->i_chroma != VLC_CODEC_RGBA)
        return VLC_EGENERIC;

    struct vlc_gl_filter_sys *sys = filter->sys = malloc(sizeof(*sys));
    if (!sys)
    {
        msg_Err(filter, "cannot allocate vlc_gl_filter_sys");
        return VLC_ENOMEM;
    }

    sys->program = create_program(filter, fmt_in);
    if (!sys->program)
    {
        msg_Err(filter, "cannot create vlc_gl_shader_program");
        return VLC_EGENERIC;
    }

    filter->vt->GenBuffers(1, &sys->vbo);

    GLuint program = vlc_gl_shader_program_GetId(sys->program);

    sys->loc.vertex_pos = filter->vt->GetAttribLocation(program, "vertex_pos");
    sys->loc.yuv_to_rgb = filter->vt->GetUniformLocation(program, "yuv_to_rgb");
    sys->loc.planes[0] = filter->vt->GetUniformLocation(program, "planes[0]");
    sys->loc.planes[1] = filter->vt->GetUniformLocation(program, "planes[1]");
    sys->loc.planes[2] = filter->vt->GetUniformLocation(program, "planes[2]");

    filter->filter = FilterInput;
    filter->close = FilterClose;

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_shortname("chroma converter NV12 to RGB")
    set_description("OpenGL NV12 to RGB chroma converter")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl chroma converter", 100)
    set_callback(Open)
    add_shortcut("glchroma")
vlc_module_end()
