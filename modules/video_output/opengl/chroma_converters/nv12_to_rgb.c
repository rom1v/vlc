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
        GLint tex_y;
        GLint tex_uv;
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

static const char *fragment_shader =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 tex_coord;\n"
    "uniform mat3 yuv_to_rgb;\n"
    "uniform sampler2D tex_y;\n"
    "uniform sampler2D tex_uv;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    " vec3 yuv = vec3(\n"
    "                 texture2D(tex_y, tex_coord).x,\n"
    "                 texture2D(tex_uv, tex_coord).x - 0.5,\n"
    "                 texture2D(tex_uv, tex_coord).y - 0.5);\n"
    " vec3 rgb = yuv_to_rgb * yuv;\n"
    " frag_color = vec4(rgb, 1.0);\n"
    "}";

static int FilterInput(struct vlc_gl_filter *filter,
                       const struct vlc_gl_filter_input *input)
{
    struct vlc_gl_filter_sys *sys = filter->sys;

    assert(input->picture.texture_count == 2);

    GLuint program = vlc_gl_shader_program_GetId(sys->program);
    filter->vt->UseProgram(program);

    const GLfloat vertexCoord[] = {
        -1, 1,
        -1, -1,
        1, 1,
        1, -1,
    };

    // <https://en.wikipedia.org/wiki/YUV>
    // in row major order
    static const GLfloat bt709_to_rgb[] = {
        1.f, 0.f, 1.28033f,
        1.f, -0.21482f, -0.38059f,
        1.f, 2.12798f, 0.f,
    };
    static const GLfloat bt601_to_rgb[] = {
        1.f,  1.f,       1.13983f,
        1.f, -0.39465f, -0.5806f,
        1.f,  2.03211f,  0.f,
    };

    // TODO use the right matrix

    filter->vt->UniformMatrix3fv(sys->loc.yuv_to_rgb, 1, GL_TRUE, bt601_to_rgb);

    const struct vlc_gl_picture *pic = &input->picture;

    // Y-plane
    filter->vt->ActiveTexture(GL_TEXTURE0);
    filter->vt->BindTexture(GL_TEXTURE_2D, pic->textures[0]);

    // UV-plane
    filter->vt->ActiveTexture(GL_TEXTURE1);
    filter->vt->BindTexture(GL_TEXTURE_2D, pic->textures[1]);

    filter->vt->Uniform1i(sys->loc.tex_y, 0);
    filter->vt->Uniform1i(sys->loc.tex_uv, 1);

    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoord), vertexCoord, GL_STATIC_DRAW);
    filter->vt->EnableVertexAttribArray(sys->loc.vertex_pos);
    filter->vt->VertexAttribPointer(sys->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0, (const void *) 0);

    filter->vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    filter->vt->ActiveTexture(GL_TEXTURE0);
    filter->vt->BindTexture(GL_TEXTURE_2D, 0);
    filter->vt->ActiveTexture(GL_TEXTURE1);
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
create_program(struct vlc_gl_filter *filter)
{
    struct vlc_gl_shader_builder *builder =
        vlc_gl_shader_builder_Create(filter->vt, NULL, NULL);
    if (!builder)
    {
        msg_Err(filter, "cannot alloc vlc_gl_shader_builder");
        return NULL;
    }

    int ret;
    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_VERTEX, "",
                                           vertex_shader);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "cannot attach vertex shader");
        vlc_gl_shader_builder_Release(builder);
        return NULL;
    }

    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_FRAGMENT, "",
                                           fragment_shader);
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
     video_format_t *fmt_in,
     video_format_t *fmt_out)
{
    if (fmt_in->i_chroma != VLC_CODEC_NV12) {
        return VLC_EGENERIC;
    }

    struct vlc_gl_filter_sys *sys = filter->sys = malloc(sizeof(*sys));
    if (!sys)
    {
        msg_Err(filter, "cannot allocate vlc_gl_filter_sys");
        return VLC_ENOMEM;
    }

    sys->program = create_program(filter);
    if (!sys->program)
    {
        msg_Err(filter, "cannot create vlc_gl_shader_program");
        return VLC_EGENERIC;
    }

    filter->vt->GenBuffers(1, &sys->vbo);

    GLuint program = vlc_gl_shader_program_GetId(sys->program);

    sys->loc.vertex_pos = filter->vt->GetAttribLocation(program, "vertex_pos");
    sys->loc.yuv_to_rgb = filter->vt->GetUniformLocation(program, "yuv_to_rgb");
    sys->loc.tex_y = filter->vt->GetUniformLocation(program, "tex_y");
    sys->loc.tex_uv = filter->vt->GetUniformLocation(program, "tex_uv");

    filter->filter = FilterInput;
    filter->close = FilterClose;

    fmt_out->i_chroma = VLC_CODEC_RGBA;
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
