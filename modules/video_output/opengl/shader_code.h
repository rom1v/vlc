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

enum vlc_shader_code_location {
    VLC_SHADER_CODE_HEADER,
    VLC_SHADER_CODE_BODY,
    VLC_SHADER_CODE_LOCATION_COUNT_,
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

#endif
