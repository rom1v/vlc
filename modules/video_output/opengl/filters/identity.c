/*****************************************************************************
 * identity.c: an identity filter for opengl
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
    "out vec4 frag_color;\n"
    "void main() {\n"
    " frag_color = vlc_texture(tex_coord);\n"
    "}";

static int FilterInput(struct vlc_gl_filter *filter,
                       const struct vlc_gl_shader_sampler *sampler,
                       const struct vlc_gl_filter_input *input)
{
    (void) sampler;

    struct vlc_gl_filter_sys *sys = filter->sys;

    GLuint program = vlc_gl_shader_program_GetId(sys->program);
    filter->vt->UseProgram(program);

    const GLfloat vertexCoord[] = {
        -1, 1,
        -1, -1,
        1, 1,
        1, -1,
    };

    int ret = vlc_gl_shader_sampler_Load(sampler, &input->picture);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "Cannot load shader sampler data");
        return ret;
    }

    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoord), vertexCoord, GL_STATIC_DRAW);
    filter->vt->EnableVertexAttribArray(sys->loc.vertex_pos);
    filter->vt->VertexAttribPointer(sys->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0, (const void *) 0);

    filter->vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    vlc_gl_shader_sampler_Unload(sampler, &input->picture);

    return VLC_SUCCESS;
}

static void FilterClose(struct vlc_gl_filter *filter)
{
    struct vlc_gl_filter_sys *sys = filter->sys;
    if (sys->program)
    {
        vlc_gl_shader_program_Release(sys->program);
        filter->vt->DeleteBuffers(1, &sys->vbo);
    }

    free(sys);
}

static struct vlc_gl_shader_program *
create_program(struct vlc_gl_filter *filter,
               const struct vlc_gl_shader_sampler *sampler)
{
    (void) sampler;

    struct vlc_gl_shader_builder *builder =
        vlc_gl_shader_builder_Create(filter->vt, NULL, sampler);
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

static int
FilterPrepare(struct vlc_gl_filter *filter,
              const struct vlc_gl_shader_sampler *sampler)
{
    struct vlc_gl_filter_sys *sys = filter->sys;
    sys->program = create_program(filter, sampler);
    if (!sys->program)
    {
        msg_Err(filter, "cannot create vlc_gl_shader_program");
        return VLC_EGENERIC;
    }

    filter->vt->GenBuffers(1, &sys->vbo);

    GLuint program = vlc_gl_shader_program_GetId(sys->program);

    sys->loc.vertex_pos = filter->vt->GetAttribLocation(program, "vertex_pos");

    int ret = vlc_gl_shader_sampler_Prepare(sampler, sys->program);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "Cannot prepare shader sampler");
        return ret;
    }

    return VLC_SUCCESS;
}

static int Open(struct vlc_gl_filter *filter,
                const config_chain_t *config,
                video_format_t *fmt_in,
                video_format_t *fmt_out)
{
    (void) config;
    (void) fmt_in;
    (void) fmt_out;

    struct vlc_gl_filter_sys *sys = filter->sys = malloc(sizeof(*sys));
    if (!sys)
    {
        msg_Err(filter, "cannot allocate vlc_gl_filter_sys");
        return VLC_ENOMEM;
    }
    sys->program = NULL;

    filter->prepare = FilterPrepare;
    filter->filter = FilterInput;
    filter->close = FilterClose;

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_shortname("identity")
    set_description("OpenGL identity filter")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callback(Open)
    add_shortcut("identity")
vlc_module_end()
