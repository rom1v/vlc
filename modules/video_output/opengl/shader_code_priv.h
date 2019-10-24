/*****************************************************************************
 * shader_code_priv.h
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

#ifndef VLC_GL_SHADER_CODE_PRIV_H
#define VLC_GL_SHADER_CODE_PRIV_H

#include "shader_code.h"

#include <vlc_vector.h>

typedef struct VLC_VECTOR(char *) vec_str;

struct vlc_gl_shader_code {
    vec_str parts[VLC_SHADER_CODE_LOCATION_COUNT_];
};

void
vlc_gl_shader_code_Init(struct vlc_gl_shader_code *code);

void
vlc_gl_shader_code_Destroy(struct vlc_gl_shader_code *code);

/**
 * Merge `other` into `code`.
 *
 * The `other` shader code is _moved_ (its content is left undefined) into
 * `code`.
 */
int
vlc_gl_shader_code_MergeIn(struct vlc_gl_shader_code *code,
                           struct vlc_gl_shader_code *other);

#endif
