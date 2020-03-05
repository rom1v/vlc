/*****************************************************************************
 * triangle_clock.c
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

#include <math.h>

#include "../filter.h"
#include "../gl_api.h"
#include "../gl_common.h"
#include "../gl_util.h"

struct sys {
    GLuint program_id;

    GLuint vbo;

    struct {
        GLint vertex_pos;
        GLint vertex_color;
        GLint rotation_matrix;
    } loc;
};

static int
Draw(struct vlc_gl_filter *filter, const struct vlc_gl_input_meta *meta)
{
    struct sys *sys = filter->sys;

    const opengl_vtable_t *vt = &filter->api->vt;

    vt->UseProgram(sys->program_id);

    vt->Enable(GL_BLEND);
    vt->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /*
     * The VBO data contains, for each vertex, 2 floats for the vertex position
     * followed by 3 floats for the associated color:
     *
     *  |     vertex 0      |     vertex 1      | ...
     *  | x | y | R | G | B | x | y | R | G | B | x | ...
     *   \-----/ \---------/
     * vertex_pos vertex_color
     */

    const GLsizei stride = 5 * sizeof(float);

    vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);

    vt->EnableVertexAttribArray(sys->loc.vertex_pos);
    vt->VertexAttribPointer(sys->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, stride,
                            (const void *) 0);

    intptr_t offset = 2 * sizeof(float);
    vt->EnableVertexAttribArray(sys->loc.vertex_color);
    vt->VertexAttribPointer(sys->loc.vertex_color, 3, GL_FLOAT, GL_FALSE,
                            stride, (const void *) offset);


    float time_sec = secf_from_vlc_tick(meta->pts);
    /* Full cycle in 60 seconds */
    float theta = time_sec * 2 * 3.141592f / 60;
    float cos_theta = cos(theta);
    float sin_theta = sin(theta);

    /* Defined in column-major order */
    const float rotation_matrix[16] = {
        cos_theta,   sin_theta,  0,          0,
        -sin_theta,  cos_theta,  0,          0,
        0,           0,          0,          0,
        0,           0,          0,          1,
    };

    vt->UniformMatrix4fv(sys->loc.rotation_matrix, 1, GL_FALSE,
                         rotation_matrix);

    vt->DrawArrays(GL_TRIANGLES, 0, 3);

    vt->Disable(GL_BLEND);

    return VLC_SUCCESS;
}

static void
Close(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;

    const opengl_vtable_t *vt = &filter->api->vt;
    vt->DeleteProgram(sys->program_id);
    vt->DeleteBuffers(1, &sys->vbo);

    free(sys);
}

static vlc_gl_filter_open_fn Open;
static int
Open(struct vlc_gl_filter *filter, const config_chain_t *config,
     struct vlc_gl_tex_size *size_out)
{
    (void) config;
    (void) size_out;

    struct sys *sys = filter->sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_EGENERIC;

#ifdef USE_OPENGL_ES2
# define SHADER_VERSION "#version 100\n"
# define FRAGMENT_SHADER_PRECISION "precision highp float;\n"
#else
# define SHADER_VERSION "#version 120\n"
# define FRAGMENT_SHADER_PRECISION
#endif

    static const char *const VERTEX_SHADER =
        SHADER_VERSION
        "attribute vec2 vertex_pos;\n"
        "attribute vec3 vertex_color;\n"
        "uniform mat4 rotation_matrix;\n"
        "varying vec3 color;\n"
        "void main() {\n"
        "  gl_Position = rotation_matrix * vec4(vertex_pos, 0.0, 1.0);\n"
        "  color = vertex_color;\n"
        "}\n";

    static const char *const FRAGMENT_SHADER =
        SHADER_VERSION
        FRAGMENT_SHADER_PRECISION
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

    sys->loc.rotation_matrix = vt->GetUniformLocation(program_id,
                                                      "rotation_matrix");
    if (sys->loc.rotation_matrix == -1)
        goto error;

    vt->GenBuffers(1, &sys->vbo);

    static const GLfloat data[] = {
      /* x   y      R  G  B */
         0,  1,     1, 0, 0,
        -1, -1,     0, 1, 0,
         1, -1,     0, 0, 1,
    };

    vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    vt->BufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
    vt->BindBuffer(GL_ARRAY_BUFFER, 0);

    filter->config.blend = true;
    filter->config.msaa_level = 4;

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
    set_shortname("triangle clock")
    set_description("OpenGL triangle blender with rotation")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callback(Open)
    add_shortcut("triangle_clock")
vlc_module_end()
