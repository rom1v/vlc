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

/**
 * The purpose of a sampler is to provide pixel values of a VLC input picture,
 * stored in any format.
 *
 * Concretely, a GLSL function:
 *
 *     vec4 vlc_texture(vec2 coords)
 *
 * returns the RGBA values for the given coordinates.
 *
 * Contrary to the standard GLSL function:
 *
 *     vec4 texture2D(sampler2D sampler, vec2 coords)
 *
 * it does not take a sampler2D as parameter. The role of the sampler is to
 * abstract the input picture from the renderer, so the input picture is
 * implicitly available.
 */
struct vlc_gl_sampler {
    /* Input format */
    const video_format_t *fmt;

    /* Software format (useful if fmt only exposes opaque chroma) */
    const video_format_t *sw_fmt;

    struct {
        /* GLSL declaration for extensions.
         *
         * If not-NULL, it must be injected immediately after the "version"
         * line.
         */
        char *extensions;

        /* Piece of code necessary to declare and implement the GLSL function
         * vlc_texture(vec2 coords).
         *
         * It must be injected in the fragment shader before calling this
         * function.
         *
         * It may not be NULL.
         */
        char *body;
    } shader;
};

/**
 * Fetch locations of uniform or attributes variables
 *
 * \param sampler the sampler
 * \param program linked program that will be used by this sampler
 */
void
vlc_gl_sampler_FetchLocations(struct vlc_gl_sampler *sampler, GLuint program);

/**
 * Prepare the fragment shader
 *
 * Typically, this specifies values of uniform variables.
 *
 * \param sampler the sampler
 */
void
vlc_gl_sampler_PrepareShader(const struct vlc_gl_sampler *sampler);

#endif
