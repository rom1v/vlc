/*****************************************************************************
 * triangle_mask.c: triangle mask for opengl
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

    GLuint vbo;

    struct {
        GLint vertex_pos;
        GLint sampler;
    } loc;
};

static const char *vertex_shader_header =
    "#version 300 es\n";

static const char *vertex_shader_body =
    "in vec2 vertex_pos;\n"
    "out vec2 tex_coord;\n"
    "void main() {\n"
    " gl_Position = vec4(vertex_pos, 0.0, 1.0);\n"
    " tex_coord = vec2( (vertex_pos.x + 1.0) / 2.0,\n"
    "                   (vertex_pos.y + 1.0) / 2.0);\n"
    "}";

static const char *fragment_shader_header =
    "#version 300 es\n"
    "precision mediump float;\n";

static const char *fragment_shader_body =
    "in vec2 tex_coord;\n"
    "uniform sampler2D tex;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    " frag_color = texture(tex, tex_coord);\n"
    "}";

static int FilterInput(struct vlc_gl_filter *filter,
                       const struct vlc_gl_shader_sampler *sampler,
                       const struct vlc_gl_filter_input *input)
{
    (void) sampler;

    struct vlc_gl_filter_sys *sys = filter->sys;

    /* TODO: program should be a vlc_gl_program loaded by a shader API */
    GLuint program = vlc_gl_shader_program_GetId(sys->program);
    filter->vt->UseProgram(program);

    const struct vlc_gl_picture *pic = &input->picture;
    const GLfloat vertexCoord[] = {
         0,     0.75,
        -0.75, -0.75,
         0.9,  -0.2,
    };

    assert(pic->textures[0]);
    /* TODO: binded texture tracker ? */
    filter->vt->ActiveTexture(GL_TEXTURE0);
    filter->vt->BindTexture(GL_TEXTURE_2D, pic->textures[0]);
    filter->vt->Uniform1i(sys->loc.sampler, 0);

    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoord), vertexCoord, GL_STATIC_DRAW);
    filter->vt->EnableVertexAttribArray(sys->loc.vertex_pos);
    filter->vt->VertexAttribPointer(sys->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0, (const void *) 0);

    filter->vt->DrawArrays(GL_TRIANGLES, 0, 3);

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
    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_VERTEX,
                                           &vertex_shader_header, 1,
                                           &vertex_shader_body, 1);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "cannot attach vertex shader");
        vlc_gl_shader_builder_Release(builder);
        return NULL;
    }

    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_FRAGMENT,
                                           &fragment_shader_header, 1,
                                           &fragment_shader_body, 1);
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

static int Open(struct vlc_gl_filter *filter,
                const config_chain_t *config,
                video_format_t *fmt_in,
                video_format_t *fmt_out)
{
    (void) config;

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
    sys->loc.sampler = filter->vt->GetUniformLocation(program, "tex");

    filter->prepare = NULL;
    filter->filter = FilterInput;
    filter->close = FilterClose;

    fmt_in->i_chroma = VLC_CODEC_RGBA;
    fmt_out->i_chroma = VLC_CODEC_RGBA;

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_shortname("triangle mask")
    set_description("OpenGL triangle mask")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callback(Open)
    add_shortcut("triangle_mask")
vlc_module_end()
