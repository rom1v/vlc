/*****************************************************************************
 * sampler_priv.h
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

#ifndef VLC_GL_SAMPLER_PRIV_H
#define VLC_GL_SAMPLER_PRIV_H

#include <vlc_common.h>

#include "gl_api.h"
#include "sampler.h"

struct vlc_gl_interop;

/**
 * Create a new sampler
 *
 * \param interop the interop
 */
struct vlc_gl_sampler *
vlc_gl_sampler_NewFromInterop(struct vlc_gl_interop *interop);

struct vlc_gl_sampler *
vlc_gl_sampler_NewDirect(struct vlc_gl_t *gl, const struct vlc_gl_api *api,
                         const video_format_t *fmt);

/**
 * Delete a sampler
 *
 * \param sampler the sampler
 */
void
vlc_gl_sampler_Delete(struct vlc_gl_sampler *sampler);

/**
 * Update the input picture
 *
 * This changes the current input picture, available from the fragment shader.
 *
 * Warning: only call on sampler created by vlc_gl_sampler_NewFromInterop().
 *
 * \param sampler the sampler
 * \param picture the new current picture
 */
int
vlc_gl_sampler_UpdatePicture(struct vlc_gl_sampler *sampler,
                             picture_t *picture);

/**
 * Update the input texture
 *
 * Warning: only call on sampler created by vlc_gl_sampler_NewDirect().
 *
 * \param sampler the sampler
 * \param texture the new current texture
 * \param tex_width the texture width
 * \param tex_height the texture height
 */
int
vlc_gl_sampler_UpdateTexture(struct vlc_gl_sampler *sampler, GLuint texture,
                             GLsizei tex_width, GLsizei tex_height);

#endif
