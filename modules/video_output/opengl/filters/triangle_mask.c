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
    struct vlc_gl_sampler *sampler;

    GLuint vbo;

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

    vlc_gl_sampler_PrepareShader(sys->sampler);

    vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    vt->EnableVertexAttribArray(sys->loc.vertex_pos);
    vt->VertexAttribPointer(sys->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0,
                            (const void *) 0);

    vt->Clear(GL_COLOR_BUFFER_BIT);
    vt->DrawArrays(GL_TRIANGLES, 0, 3);

    fprintf(stderr, "==== draw\n");

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
     const struct vlc_gl_tex_size *size_in,
     struct vlc_gl_tex_size *size_out,
     struct vlc_gl_sampler *sampler)
{
    (void) config;
    (void) size_in;
    (void) size_out;

    fprintf(stderr, "%ux%u %ux%u\n", size_in->width, size_in->height, size_out->width, size_out->height);

    struct sys *sys = filter->sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_EGENERIC;

    sys->sampler = sampler;

    static const char *const VERTEX_SHADER =
        "#version 100\n"
        "attribute vec2 vertex_pos;\n"
        "varying vec2 tex_coords;\n"
        "void main() {\n"
        "  gl_Position = vec4(vertex_pos, 0.0, 1.0);\n"
        "  tex_coords = vec2((vertex_pos.x + 1.0) / 2.0,\n"
        "                    (vertex_pos.y + 1.0) / 2.0);\n"
        "}\n";

    static const char *const FRAGMENT_SHADER_TEMPLATE =
        "#version 100\n"
        "%s\n" /* extensions */
        "precision highp float;\n"
        "%s\n" /* vlc_texture definition */
        "varying vec2 tex_coords;\n"
        "void main() {\n"
        "  gl_FragColor = vlc_texture(tex_coords);\n"
        "}\n";

    const char *extensions = sampler->shader.extensions
                           ? sampler->shader.extensions : "";

    char *fragment_shader;
    int ret = asprintf(&fragment_shader, FRAGMENT_SHADER_TEMPLATE, extensions,
                       sampler->shader.body);
    if (ret < 0)
        goto error;

    const opengl_vtable_t *vt = &filter->api->vt;

    GLuint program_id =
        vlc_gl_BuildProgram(VLC_OBJECT(filter), vt,
                            1, (const char **) &VERTEX_SHADER,
                            1, (const char **) &fragment_shader);

    free(fragment_shader);
    if (!program_id)
        goto error;

    vlc_gl_sampler_FetchLocations(sampler, program_id);

    sys->program_id = program_id;

    sys->loc.vertex_pos = vt->GetAttribLocation(program_id, "vertex_pos");
    if (sys->loc.vertex_pos == -1)
        goto error;

    vt->GenBuffers(1, &sys->vbo);

    static const GLfloat vertex_pos[] = {
         0,     0.75,
        -0.75, -0.75,
         0.9,  -0.2,
    };

    vt->BindBuffer(GL_ARRAY_BUFFER, sys->vbo);
    vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertex_pos), vertex_pos,
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
    set_shortname("triangle_mask")
    set_description("OpenGL triangle blender")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callback(Open)
    add_shortcut("triangle_mask")
vlc_module_end()
