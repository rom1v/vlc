/*****************************************************************************
 * shader_code.h
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

#ifndef VLC_GL_SHADER_CODE_H
#define VLC_GL_SHADER_CODE_H

#include <vlc_common.h>
#include <vlc_vector.h>
#include "gl_common.h"

enum vlc_shader_code_location {
    VLC_SHADER_CODE_HEADER,
    VLC_SHADER_CODE_BODY,
    VLC_SHADER_CODE_LOCATION_COUNT_,
};

struct vlc_gl_shader_cbs {

    /**
     * This function will be called once, after the whole program is compiled
     * and linked.
     *
     * Its purpose is typically to retrieve uniforms and attributes locations.
     */
    int
    (*on_program_compiled)(GLuint program, void *userdata);

    /**
     * This function will be called before drawing.
     *
     * Its purpose is to load attributes and uniforms.
     */
    int
    (*prepare_shader)(void *userdata);
};

struct vlc_gl_shader_code;

int
vlc_gl_shader_code_AppendVa(struct vlc_gl_shader_code *code,
                            enum vlc_shader_code_location location,
                            const char *fmt, va_list ap);

static inline int
vlc_gl_shader_code_Append(struct vlc_gl_shader_code *code,
                          enum vlc_shader_code_location location,
                          const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int ret = vlc_gl_shader_code_AppendVa(code, location, fmt, va);
    va_end(va);
    return ret;
}

int
vlc_gl_shader_code_RegisterCallbacks(struct vlc_gl_shader_code *code,
                                     const struct vlc_gl_shader_cbs *cbs,
                                     void *userdata);

#endif
