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

static int
BuildProgram(struct vlc_gl_filter *filter)
{
    static const char *const VERTEX_SHADER =
        "#version 100\n"
        "attribute vec2 vertex_pos;\n"
        "attribute vec3 vertex_color;\n"
        "varying vec3 color;\n"
        "void main() {\n"
        "  gl_Position = vec4(vertex_pos, 0.0, 1.0)\n"
        "  color = vertex_color;\n"
        "}\n";

    static const char *const FRAGMENT_SHADER=
        "#version 100\n"
        "precision highp float;\n"
        "varying vec3 color;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(Color, 0.5)\n"
        "}\n";

    const opengl_vtable_t *vt = &filter->api->vt;

    GLuint program_id =
        vlc_gl_BuildProgram(VLC_OBJECT(filter), vt,
                            1, (const char **) &VERTEX_SHADER,
                            1, (const char **) &FRAGMENT_SHADER);

    return VLC_SUCCESS;
}

static vlc_gl_filter_open_fn Open;
static int
Open(struct vlc_gl_filter *filter, const config_chain_t *config)
{
    (void) config;

    int ret = BuildProgram(filter);
    if (ret != VLC_SUCCESS)
        return ret;

    fprintf(stderr, "======== TRIANGLE LOADED ======\n");
    return VLC_SUCCESS;
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
