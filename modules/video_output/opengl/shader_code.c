/*****************************************************************************
 * shader_code.c
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "shader_code_priv.h"

#include <assert.h>

void
vlc_gl_shader_code_Init(struct vlc_gl_shader_code *code)
{
    for (int loc = 0; loc < VLC_SHADER_CODE_LOCATION_COUNT_; ++loc)
        vlc_vector_init(&code->parts[loc]);
}

void
vlc_gl_shader_code_Destroy(struct vlc_gl_shader_code *code)
{

    for (int loc = 0; loc < VLC_SHADER_CODE_LOCATION_COUNT_; ++loc)
    {
        vec_str *vec = &code->parts[loc];
        for (size_t i = 0; i < vec->size; ++i)
            free(vec->data[i]);
        vlc_vector_destroy(vec);
    }
}

int
vlc_gl_shader_code_AppendVa(struct vlc_gl_shader_code *code,
                            enum vlc_shader_code_location location,
                            const char *fmt, va_list ap)
{
    char *str;
    int len = vasprintf(&str, fmt, ap);
    if (len == -1)
        return VLC_ENOMEM;

    bool ok = vlc_vector_push(&code->parts[location], str);
    if (!ok)
    {
        free(str);
        return VLC_ENOMEM;
    }

    return VLC_SUCCESS;
}

int
vlc_gl_shader_code_MergeIn(struct vlc_gl_shader_code *code,
                           struct vlc_gl_shader_code *other)
{
    /* reserve space separately to keep the state consistent on error */
    for (int loc = 0; loc < VLC_SHADER_CODE_LOCATION_COUNT_; ++loc)
    {
        size_t count = code->parts[loc].size + other->parts[loc].size;
        bool ok = vlc_vector_reserve(&code->parts[loc], count);
        if (!ok)
            return VLC_ENOMEM;
    }

    for (int loc = 0; loc < VLC_SHADER_CODE_LOCATION_COUNT_; ++loc)
    {
        bool ok = vlc_vector_push_all(&code->parts[loc], other->parts[loc].data,
                                      other->parts[loc].size);
        assert(ok); /* we called vlc_vector_reserve() beforehand */
        /* the code have been moved out of the "other" structure */
        vlc_vector_clear(&other->parts[loc]);
    }

    vlc_gl_shader_code_Destroy(other);

    return VLC_SUCCESS;
}
