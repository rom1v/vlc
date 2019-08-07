/*****************************************************************************
 * filter.h: OpenGL filter API
 *****************************************************************************
 * Copyright (C) 2019 VideoLabs
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

#ifndef VLC_OPENGL_FILTER_H
#define VLC_OPENGL_FILTER_H

#include <GL/gl.h>

#include <vlc_common.h>
#include <vlc_es.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TODO: forward declaration opengl_vtable_t */
struct opengl_vtable_t;
typedef struct opengl_vtable_t opengl_vtable_t;

struct opengl_tex_converter_t;

/**
 *
 */
struct vlc_gl_region
{
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
};


/**
 *
 */
struct vlc_gl_filter_input
{
    struct vlc_gl_region picture;

    size_t region_count;
    struct vlc_gl_region *regions;

    /**
     * Common rendering variable, shouldn't be here
     */
    struct {
        GLfloat OrientationMatrix[16];
        GLfloat ProjectionMatrix[16];
        GLfloat ZoomMatrix[16];
        GLfloat ViewMatrix[16];
    } var;
};


/**
 * Opengl filter public API, which is equivalent to a rendering pass.
 *
 * Currently, it only allows a rendering operation on the current framebuffer.
 *
 * \warning{ this API is currently unstable }
 */
struct vlc_gl_filter
{
    vlc_object_t obj;
    /**
     * Filter module private data
     */
    void *sys;

    const config_chain_t *config;

    const video_format_t *fmt;

    /**
     * Renderer opengl vtable
     */
    const opengl_vtable_t *vt;
    vlc_gl_t *gl;

    /**
     *
     */
    int (*filter)(struct vlc_gl_filter *filter,
                  const struct vlc_gl_filter_input *input);

    /**
     *
     */
    void (*close)(struct vlc_gl_filter *filter);

    struct
    {
        bool blend;
    } info;
};

struct vlc_gl_program
{
    GLuint id;
    struct opengl_tex_converter_t *tc;

    /* XXX: shouldn't be here */
    struct {
        GLfloat OrientationMatrix[16];
        GLfloat ProjectionMatrix[16];
        GLfloat ZoomMatrix[16];
        GLfloat ViewMatrix[16];
    } var;

    /* XXX: shouldn't be here */
    struct { /* UniformLocation */
        GLint OrientationMatrix;
        GLint ProjectionMatrix;
        GLint ViewMatrix;
        GLint ZoomMatrix;
    } uloc;

    /* XXX: shouldn't be here */
    struct { /* AttribLocation */
        GLint MultiTexCoord[3];
        GLint VertexPosition;
    } aloc;
};

#ifdef __cplusplus
} // extern "c"
#endif

#endif
