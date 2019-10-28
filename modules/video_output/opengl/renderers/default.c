/*****************************************************************************
 * default.c
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

#include "../renderer.h"

struct sys {
    const struct vlc_gl_program *program;
    GLuint program_id;
    GLuint vbo;
    struct {
        GLint vertex_coords;
    } loc;
};

static const char *const VERTEX_SHADER =
    "#version 300 es\n"
    "in vec2 vertex_coords;\n"
    "out vec2 tex_coords;\n"
    "void main() {\n"
    "  gl_Position = vec4(vertex_coords, 0.0, 1.0);\n"
    "  tex_coords = vec2((vertex_coords.x + 1.0) / 2.0,\n"
    "                    (vertex_coords.y + 1.0) / 2.0);\n"
    "}";

static const char *const FRAGMENT_SHADER_HEADER =
    "#version 300 es\n"
    "precision mediump float;\n";

static const char *const FRAGMENT_SHADER_BODY =
    "in vec2 tex_coords;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = vlc_texture_raw(tex_coord);\n"
    "}";

static int
Prepare(struct vlc_gl_renderer *renderer)
{
    struct sys *sys = renderer->sys;

    int ret = vlc_gl_program_PrepareShaders(sys->program);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(renderer, "Could not prepare shaders");
        return ret;
    }

    static GLfloat VERTEX_COORDS[] = {
        -1, -1,
        -1, 1,
        1, -1,
        1, 1,
    };

    const opengl_vtable_t *gl = renderer->gl;

    gl->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    gl->BufferData(GL_ARRAY_BUFFER, sizeof(VERTEX_COORDS), VERTEX_COORDS,
                   GL_STATIC_DRAW);
    gl->EnableVertexAttribArray(sys->loc.vertex_coords);
    gl->VertexAttribPointer(sys->loc.vertex_coords, 2, GL_FLOAT, GL_FALSE, 0,
                            (const void *) 0);

    return VLC_SUCCESS;
}

static int
Render(struct vlc_gl_renderer *renderer)
{
    const opengl_vtable_t *gl = renderer->gl;

    gl->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    return VLC_SUCCESS;
}

static void
Close(struct vlc_gl_renderer *renderer)
{
    struct sys *sys = renderer->sys;
    const opengl_vtable_t *gl = renderer->gl;

    gl->DeleteBuffers(1, &sys->vbo);
    gl->DeleteProgram(sys->program_id);

    free(sys);
}

static const struct vlc_gl_renderer_ops ops = {
    .prepare = Prepare,
    .render = Render,
    .close = Close,
};

static vlc_gl_renderer_open_fn Open;
static int
Open(struct vlc_gl_renderer *renderer, struct vlc_gl_program *program)
{
    const opengl_vtable_t *gl = renderer->gl;

    int ret = vlc_gl_program_AppendShaderCode(program, VLC_GL_SHADER_VERTEX,
                                              VLC_GL_SHADER_CODE_BODY,
                                              VERTEX_SHADER);
    if (ret != VLC_SUCCESS)
        return ret;

    ret = vlc_gl_program_AppendShaderCode(program, VLC_GL_SHADER_FRAGMENT,
                                          VLC_GL_SHADER_CODE_HEADER,
                                          FRAGMENT_SHADER_HEADER);
    if (ret != VLC_SUCCESS)
        return ret;

    ret = vlc_gl_program_AppendShaderCode(program, VLC_GL_SHADER_FRAGMENT,
                                          VLC_GL_SHADER_CODE_BODY,
                                          FRAGMENT_SHADER_BODY);
    if (ret != VLC_SUCCESS)
        return ret;

    GLuint program_id = vlc_gl_program_Compile(program, gl);
    if (!program_id)
        return ret;

    struct sys *sys = renderer->sys = malloc(sizeof(*sys));
    if (!sys)
    {
        gl->DeleteProgram(program_id);
        return ret;
    }

    sys->program = program;
    sys->program_id = program_id;
    gl->GenBuffers(1, &sys->vbo);
    sys->loc.vertex_coords = gl->GetAttribLocation(program_id, "vertex_pos");

    renderer->ops = &ops;

    return VLC_SUCCESS;
}

vlc_module_begin ()
    set_description("OpenGL default renderer")
    set_capability("glrenderer", 100)
    set_callback(Open)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    add_shortcut("default")
vlc_module_end ()
