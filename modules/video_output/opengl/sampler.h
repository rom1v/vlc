/*****************************************************************************
 * sampler.h
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

#ifndef VLC_GL_SAMPLER_H
#define VLC_GL_SAMPLER_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_opengl.h>
#include <vlc_picture.h>

#include "gl_common.h"
#include "../placebo_utils.h"

struct vlc_gl_sampler {
    const opengl_vtable_t *vt;

    struct {
        GLfloat OrientationMatrix[4*4];
        GLfloat TexCoordsMap[PICTURE_PLANE_MAX][3*3];
    } var;
    struct {
        GLint Texture[PICTURE_PLANE_MAX];
        GLint TexSize[PICTURE_PLANE_MAX]; /* for GL_TEXTURE_RECTANGLE */
        GLint ConvMatrix;
        GLint FillColor;
        GLint *pl_vars; /* for pl_sh_res */

        GLint TransformMatrix;
        GLint OrientationMatrix;
        GLint TexCoordsMap[PICTURE_PLANE_MAX];
    } uloc;

    bool yuv_color;
    GLfloat conv_matrix[4*4];

    /* libplacebo context */
    struct pl_context *pl_ctx;
    struct pl_shader *pl_sh;
    const struct pl_shader_res *pl_sh_res;

    GLsizei tex_width[PICTURE_PLANE_MAX];
    GLsizei tex_height[PICTURE_PLANE_MAX];

    GLuint textures[PICTURE_PLANE_MAX];

    struct {
        unsigned int i_x_offset;
        unsigned int i_y_offset;
        unsigned int i_visible_width;
        unsigned int i_visible_height;
    } last_source;

    struct vlc_gl_interop *interop;
};

struct vlc_gl_sampler *
vlc_gl_sampler_New(struct vlc_gl_interop *interop, const video_format_t *fmt);

void
vlc_gl_sampler_Delete(struct vlc_gl_sampler *sampler);

#endif
