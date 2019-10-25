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

#ifndef VLC_GL_PROGRAM_PRIV_H
#define VLC_GL_PROGRAM_PRIV_H

#include "program.h"

#include <vlc_vector.h>

struct vlc_gl_program_cbs_reg {
    const struct vlc_gl_program_cbs *cbs;
    void *userdata;
};

typedef struct VLC_VECTOR(char *) vec_str;
typedef struct VLC_VECTOR(struct vlc_gl_program_cbs_reg)
    vec_program_cbs_reg;

struct vlc_gl_program {
    vec_str code[VLC_GL_SHADER_TYPE_COUNT_][VLC_GL_SHADER_CODE_LOCATION_COUNT_];
    vec_program_cbs_reg cbs_reg;
};

void
vlc_gl_program_Init(struct vlc_gl_program *program);

// copy constructor
int
vlc_gl_program_InitFrom(struct vlc_gl_program *program,
                        struct vlc_gl_program *other);

void
vlc_gl_program_Destroy(struct vlc_gl_program *program);

/**
 * Merge `other` into `code`.
 *
 * The `other` program code and callbacks are _moved_ into `program` (the
 * content of `other` is left undefined).
 */
int
vlc_gl_program_MergeIn(struct vlc_gl_program *program,
                       struct vlc_gl_program *other);

#endif
