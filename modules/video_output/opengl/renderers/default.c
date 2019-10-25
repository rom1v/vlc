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

static const char *const VERTEX_SHADER =
    "#version 300 es\n"
    "in vec2 vectex_pos;\n"
    "out vec2 tex_coord;\n"
    "void main() {\n"
    "  gl_Position = vec4(vector_pos, 0.0, 1.0);\n"
    "  tex_coord = vec2((vertex_pos.x + 1.0) / 2.0,\n"
    "                   (vectex_pos.y + 1.0) / 2.0);\n"
    "}";

static const char *const FRAGMENT_SHADER_HEADER =
    "#version 300 es\n"
    "precision mediump float;\n";

static const char *const FRAGMENT_SHADER_BODY =
    "in vec2 tex_coord;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = vlc_texture_raw(tex_coord);\n"
    "}";

static int
Draw(struct vlc_gl_renderer *renderer)
{
    const opengl_vtable_t *gl = renderer->gl;

    //gl->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    //gl->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoord), vertexCoord, GL_STATIC_DRAW);
    //gl->EnableVertexAttribArray(sys->loc.vertex_pos);
    //gl->VertexAttribPointer(sys->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0, (const void *) 0);
}

static void
Close(struct vlc_gl_renderer *renderer)
{

}

static int
FetchLocations(GLuint program, void *userdata)
{
    struct vlc_gl_renderer *renderer = userdata;
}

static int
PrepareShaders(void *userdata)
{
    struct vlc_gl_renderer *renderer = userdata;

}

static const struct vlc_gl_renderer_ops ops = {
    .draw = Draw,
    .close = Close,
};

static const struct vlc_gl_program_cbs program_cbs = {
    .on_program_compiled = FetchLocations,
    .prepare_shaders = PrepareShaders,
};

static vlc_gl_renderer_open_fn Open;
static int
Open(struct vlc_gl_renderer *renderer, struct vlc_gl_program *program)
{
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

    ret = vlc_gl_program_RegisterCallbacks(program, &program_cbs, renderer);
    if (ret != VLC_SUCCESS)
        return ret;

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
