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
struct vlc_gl_shader_sampler;
typedef struct opengl_vtable_t opengl_vtable_t;

struct opengl_tex_converter_t;

struct vlc_gl_picture {
    GLuint textures[PICTURE_PLANE_MAX];
    unsigned texture_count;
};

struct vlc_gl_filter_input {
    /* Input video frame. */
    struct vlc_gl_picture   picture;
    /* Current viewpoint in the renderer. */
    vlc_viewpoint_t         viewpoint;

    vlc_tick_t   picture_date;
    vlc_tick_t   rendering_date;
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

    /**
     * Renderer opengl vtable
     */
    const opengl_vtable_t *vt;
    vlc_gl_t *gl;

    /**
     * Called once after the module Open() function, with a shader sampler
     * (matching the filter requested input format) is initialized by the core.
     *
     * Typically, a module must compile its OpenGL program from this function.
     */
    int (*prepare)(struct vlc_gl_filter *filter,
                   const struct vlc_gl_shader_sampler *sampler);

    /**
     *
     */
    int (*filter)(struct vlc_gl_filter *filter,
                  const struct vlc_gl_shader_sampler *sampler,
                  const struct vlc_gl_filter_input *input);

    /**
     * Called when previous filter output has been resized. The filter
     * implementation should override `*width` and `*height` if the new
     * output size of the filter should be different from the input.
     *
     * @param filter the running filter instance
     * @param width the new input width and output width
     * @param height the new input height and output width
     * @return VLC_SUCCESS if resize is accepted, VLC_EGENERIC if the filter
     *         cannot adapt to this new size
     */
    /* int (*resize)(struct vlc_gl_filter *filter,
                  unsigned *width, unsigned *height);*/

    /**
     * Called when previous filter *output* has been resized. The filter
     * implementation should override fmt_out if it should be different from
     * the previous output.
     *
     * @param filter the running filter instance
     * @param width the new input width and output width
     * @param height the new input height and output width
     * @return VLC_SUCCESS if change is accepted, VLC_EGENERIC if the filter
     *         cannot adapt to this new format
     */
    int (*input_change)(struct vlc_gl_filter *filter,
            video_format_t *fmt_in,
            video_format_t *fmt_out
            );

    /**
     * Called when previous filter *input* has been resized. The filter
     * implementation should override fmt_in if it should be different from
     * the previous output.
     *
     * @param filter the running filter instance
     * @param fmt_in The current expected fmt_in
     * @param fmt_out The changed fmt_out
     * @return VLC_SUCCESS if change is accepted, VLC_EGENERIC if the filter
     *         cannot adapt to this new format
     */
    int (*output_change)(struct vlc_gl_filter *filter,
            video_format_t *fmt_in,
            video_format_t *fmt_out
            );

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
