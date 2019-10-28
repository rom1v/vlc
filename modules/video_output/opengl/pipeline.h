/*****************************************************************************
 * pipeline.h
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

#ifndef VLC_GL_PIPELINE_H
#define VLC_GL_PIPELINE_H

#include <vlc_common.h>

#include "importer.h"
#include "renderer.h"

struct vlc_gl_pipeline;

struct vlc_gl_pipeline_ops {
    int
    (*prepare)(struct vlc_gl_pipeline *pipeline, picture_t *picture,
               subpicture_t *subpicture);

    int
    (*render)(struct vlc_gl_pipeline *pipeline);
};

struct vlc_gl_pipeline *
vlc_gl_pipeline_New(vlc_object_t *obj, const opengl_vtable_t *gl,
                    const video_format_t *fmt);

void
vlc_gl_pipeline_Delete(struct vlc_gl_pipeline *pipeline);

#endif
