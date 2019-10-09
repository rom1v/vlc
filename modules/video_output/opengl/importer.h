/*****************************************************************************
 * importer.h: VLC GL importer
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

#ifndef VLC_OPENGL_IMPORTER_H
#define VLC_OPENGL_IMPORTER_H

#include <GL/gl.h>

#include <vlc_common.h>
#include "converter.h" /* for opengl_vtable_t */

struct vlc_gl_importer;

struct vlc_gl_importer_ops {
    int (*import)(struct vlc_gl_importer *importer, GLuint textures[],
                  const GLsizei tex_width[], const GLsizei text_height[],
                  unsigned tex_count, picture_t *pic,
                  const size_t plane_offsets[]);

    void (*close)(struct vlc_gl_importer *importer);
};

struct vlc_gl_importer {
    struct vlc_object_t obj;

    const opengl_vtable_t *vt;
    GLenum tex_target;

    /* Initialized by the importer */
    struct vlc_gl_tex_cfg {
        /*
         * Texture scale factor, cannot be 0.
         * In 4:2:0, 1/1 for the Y texture and 1/2 for the UV texture(s)
         */
        vlc_rational_t w;
        vlc_rational_t h;

        GLint internal;
        GLenum format;
        GLenum type;
    } cfg[PICTURE_PLANE_MAX];

    void *sys;
    const struct vlc_gl_importer_ops *ops;
};

#endif
