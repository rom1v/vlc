/*****************************************************************************
 * sub_renderer.h
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

#ifndef VLC_RENDERER_H
#define VLC_RENDERER_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_opengl.h>

#include "gl_common.h"

struct vlc_gl_renderer;

/**
 * Create a new renderer
 *
 * \param gl the GL context
 * \param vt the OpenGL functions vtable
 * \param supports_npot indicate if the implementation supports non-power-of-2
 *                      texture size
 */
struct vlc_gl_renderer *
vlc_gl_renderer_New(vlc_gl_t *gl, const opengl_vtable_t *vt,
                    bool supports_npot);

/**
 * Delete a renderer
 *
 * \param sr the renderer
 */
void
vlc_gl_renderer_Delete(struct vlc_gl_renderer *r);

/**
 * Prepare the fragment shader
 *
 * Concretely, it allocates OpenGL textures if necessary and uploads the
 * picture.
 *
 * \param sr the renderer
 * \param picture the picture to render
 */
int
vlc_gl_renderer_Prepare(struct vlc_gl_sub_renderer *r, picture_t *picture);

/**
 * Draw the prepared picture
 *
 * \param sr the renderer
 */
int
vlc_gl_renderer_Draw(struct vlc_gl_renderer *r);

int
vlc_gl_renderer_SetViewpoint(struct vlc_gl_renderer *r,
                             const vlc_viewpoint_t *vp);

void
vlc_gl_renderer_SetWindowAspectRatio(struct vlc_gl_renderer *r, float sar);

void
vlc_gl_renderer_SetViewport(struct vlc_gl_renderer *r, int x, int y,
                            unsigned width, unsigned height);

#endif
