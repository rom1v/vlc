/*****************************************************************************
 * renderer.h: OpenGL internal header
 *****************************************************************************
 * Copyright (C) 2016 VLC authors and VideoLAN
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

#ifndef VLC_GL_RENDERER_H
#define VLC_GL_RENDERER_H

#include <vlc_common.h>
#include <vlc_codec.h>
#include <vlc_opengl.h>
#include <vlc_plugin.h>
#include "gl_common.h"
#include "interop.h"

struct pl_context;
struct pl_shader;
struct pl_shader_res;

/**
 * OpenGL picture renderer
 */
struct vlc_gl_renderer
{
    /* Pointer to object gl, set by the caller */
    vlc_gl_t *gl;

    /* libplacebo context, created by the caller (optional) */
    struct pl_context *pl_ctx;

    /* Function pointers to OpenGL functions, set by the caller */
    const opengl_vtable_t *vt;

    /* True to dump shaders, set by the caller */
    bool b_dump_shaders;

    /* GLSL version, set by the caller. 100 for GLSL ES, 120 for desktop GLSL */
    unsigned glsl_version;
    /* Precision header, set by the caller. In OpenGLES, the fragment language
     * has no default precision qualifier for floating point types. */
    const char *glsl_precision_header;

    GLuint program_id;

    struct {
        GLfloat OrientationMatrix[16];
        GLfloat ProjectionMatrix[16];
        GLfloat ZoomMatrix[16];
        GLfloat ViewMatrix[16];
    } var;

    struct {
        GLint Texture[PICTURE_PLANE_MAX];
        GLint TexSize[PICTURE_PLANE_MAX]; /* for GL_TEXTURE_RECTANGLE */
        GLint ConvMatrix;
        GLint FillColor;
        GLint *pl_vars; /* for pl_sh_res */

        GLint TransformMatrix;
        GLint OrientationMatrix;
        GLint ProjectionMatrix;
        GLint ViewMatrix;
        GLint ZoomMatrix;
    } uloc;

    struct {
        GLint MultiTexCoord[3];
        GLint VertexPosition;
    } aloc;

    bool yuv_color;
    GLfloat conv_matrix[16];

    struct pl_shader *pl_sh;
    const struct pl_shader_res *pl_sh_res;

    struct vlc_gl_interop *interop;

    /**
     * Callback to fetch locations of uniform or attributes variables
     *
     * This function pointer cannot be NULL. This callback is called one time
     * after init.
     *
     * \param renderer OpenGL renderer
     * \param program linked program that will be used by this renderer
     * \return VLC_SUCCESS or a VLC error
     */
    int (*pf_fetch_locations)(struct vlc_gl_renderer *renderer, GLuint program);

    /**
     * Callback to prepare the fragment shader
     *
     * This function pointer cannot be NULL. This callback can be used to
     * specify values of uniform variables.
     *
     * \param renderer OpenGL renderer
     * \param tex_width array of tex width (one per plane)
     * \param tex_height array of tex height (one per plane)
     * \param alpha alpha value, used only for RGBA fragment shader
     */
    void (*pf_prepare_shader)(const struct vlc_gl_renderer *renderer,
                              const GLsizei *tex_width, const GLsizei *tex_height,
                              float alpha);
};

#endif /* include-guard */
