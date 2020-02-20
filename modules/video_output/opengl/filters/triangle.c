/*****************************************************************************
 * triangle.c
 *****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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

#include "../filter.h"
#include "../gl_api.h"
#include "../gl_common.h"
#include "../gl_util.h"

struct sys {
    GLuint program_id;

#define VBO_VERTEX_POS 0
#define VBO_VERTEX_COLOR 1
    GLuint vbo[2];

    struct {
        GLint vertex_pos;
        GLint vertex_color;
    } loc;
};

static int
Draw(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;

    const opengl_vtable_t *vt = &filter->api->vt;

    vt->UseProgram(sys->program_id);

    vt->Enable(GL_BLEND);
    vt->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo[VBO_VERTEX_POS]);
    vt->EnableVertexAttribArray(sys->loc.vertex_pos);
    vt->VertexAttribPointer(sys->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0,
                            (const void *) 0);

    vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo[VBO_VERTEX_COLOR]);
    vt->EnableVertexAttribArray(sys->loc.vertex_color);
    vt->VertexAttribPointer(sys->loc.vertex_color, 3, GL_FLOAT, GL_FALSE, 0,
                            (const void *) 0);

    vt->DrawArrays(GL_TRIANGLES, 0, 3);

    vt->Disable(GL_BLEND);

    fprintf(stderr, "==== draw\n");

    return VLC_SUCCESS;
}

static void
Close(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;

    const opengl_vtable_t *vt = &filter->api->vt;
    vt->DeleteProgram(sys->program_id);
    vt->DeleteBuffers(2, sys->vbo);

    free(sys);
}

static vlc_gl_filter_open_fn Open;
static int
Open(struct vlc_gl_filter *filter, const config_chain_t *config,
     struct vlc_gl_sampler *sampler)
{
    (void) config;

    struct sys *sys = filter->sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_EGENERIC;

    static const char *const VERTEX_SHADER =
        "#version 100\n"
        "attribute vec2 vertex_pos;\n"
        "attribute vec3 vertex_color;\n"
        "varying vec3 color;\n"
        "void main() {\n"
        "  gl_Position = vec4(vertex_pos, 0.0, 1.0);\n"
        "  color = vertex_color;\n"
        "}\n";

    static const char *const FRAGMENT_SHADER =
        "#version 100\n"
        "precision highp float;\n"
        "varying vec3 color;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(color, 0.5);\n"
        "}\n";

    const opengl_vtable_t *vt = &filter->api->vt;

    GLuint program_id =
        vlc_gl_BuildProgram(VLC_OBJECT(filter), vt,
                            1, (const char **) &VERTEX_SHADER,
                            1, (const char **) &FRAGMENT_SHADER);

    if (!program_id)
        goto error;

    sys->program_id = program_id;

    sys->loc.vertex_pos = vt->GetAttribLocation(program_id, "vertex_pos");
    if (sys->loc.vertex_pos == -1)
        goto error;

    sys->loc.vertex_color = vt->GetAttribLocation(program_id, "vertex_color");
    if (sys->loc.vertex_color == -1)
        goto error;

    vt->GenBuffers(ARRAY_SIZE(sys->vbo), sys->vbo);

    static const GLfloat vertex_pos[] = {
         0,  1,
        -1, -1,
         1, -1,
    };

    vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo[VBO_VERTEX_POS]);
    vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertex_pos), vertex_pos,
                   GL_STATIC_DRAW);

    const GLfloat vertex_colors[] = {
        1, 0, 0,
        0, 1, 0,
        0, 0, 1,
    };

    vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo[VBO_VERTEX_COLOR]);
    vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertex_colors), vertex_colors,
                   GL_STATIC_DRAW);

    vt->BindBuffer(GL_ARRAY_BUFFER, 0);

    static const struct vlc_gl_filter_ops ops = {
        .draw = Draw,
        .close = Close,
    };
    filter->ops = &ops;

    return VLC_SUCCESS;

error:
    free(sys);
    return VLC_EGENERIC;
}

vlc_module_begin()
    set_shortname("triangle")
    set_description("OpenGL triangle blender")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callback(Open)
    add_shortcut("triangle")
vlc_module_end()
