/*****************************************************************************
 * sub_renderer.c
 *****************************************************************************
 * Copyright (C) 2020 Videolabs
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

#include "sub_renderer.h"

typedef struct {
    GLuint   texture;
    GLsizei  width;
    GLsizei  height;

    float    alpha;

    float    top;
    float    left;
    float    bottom;
    float    right;

    float    tex_width;
    float    tex_height;
} gl_region_t;

struct vlc_sub_renderer
{
    int region_count;
    gl_region_t *regions;

    GLuint program_id;
    struct {

    } loc;

    GLuint *subpicture_buffer_object;
    int subpicture_buffer_object_count;
}

struct vlc_sub_renderer *
vlc_sub_renderer_New(void)
{
    struct vlc_sub_renderer *sr = malloc(sizeof(*sr));
    if (!sr)
        return NULL;

    sr->region_count = 0;
    sr->regions = NULL;
    return NULL;
}

void
vlc_sub_renderer_Delete(struct vlc_sub_renderer *sr)
{
    free(sr);
}

int
vlc_sub_renderer_Prepare(struct vlc_sub_renderer *sr, subpicture_t *subpicture)
{

}

int
vlc_sub_renderer_Draw(struct vlc_sub_renderer *sr)
{

}
