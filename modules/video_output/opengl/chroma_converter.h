/*****************************************************************************
 * chroma_converter.h: VLC GL chroma converter
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

#ifndef VLC_GL_CHROMA_CONVERTER_H
#define VLC_GL_CHROMA_CONVERTER_H

#include "converter.h" // for opengl_vtable_t

struct vlc_gl_chroma_converter;

static const char *const FRAGMENT_COORDS_NORMAL =
    "vec2 vlc_picture_coords(vec2 coords) {\n"
    "  return vec2(coords.x, coords.y);\n"
    "}\n";

static const char *const FRAGMENT_COORDS_VFLIPPED =
    "vec2 vlc_picture_coords(vec2 coords) {\n"
    "  return vec2(coords.x, 1.0 - coords.y);\n"
    "}\n";

struct vlc_gl_chroma_converter_ops {
    void (*close)(struct vlc_gl_chroma_converter *converter);
};

struct vlc_gl_chroma_converter {
    vlc_object_t obj;
    const opengl_vtable_t *vt;

    void *sys;
    const struct vlc_gl_chroma_converter_ops *ops;
};

#endif
