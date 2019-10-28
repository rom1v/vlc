/*****************************************************************************
 * renderer.h
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

#include "program.h"

struct vlc_gl_renderer;

typedef int vlc_gl_renderer_open_fn(struct vlc_gl_renderer *renderer,
                                    struct vlc_gl_program *program);

struct vlc_gl_renderer_ops {
    int
    (*prepare)(struct vlc_gl_renderer *renderer);

    int
    (*render)(struct vlc_gl_renderer *renderer);

    void
    (*close)(struct vlc_gl_renderer *renderer);
};

struct vlc_gl_renderer {
    struct vlc_object_t obj;
    module_t *module;

    const opengl_vtable_t *gl;

    void *sys;
    const struct vlc_gl_renderer_ops *ops;
};
