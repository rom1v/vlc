/*****************************************************************************
 * importer.h
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

#ifndef VLC_GL_IMPORTER_H
#define VLC_GL_IMPORTER_H

#include <vlc_common.h>
#include <vlc_opengl.h>
#include <vlc_picture.h>
#include <vlc_picture_pool.h>

#include "gl_common.h"

struct vlc_gl_importer;

struct vlc_gl_importer_ops {
    /**
     * Callback to allocate data for bound textures
     *
     * This function pointer can be NULL. Software converters should call
     * glTexImage2D() to allocate textures data (it will be deallocated by the
     * caller when calling glDeleteTextures()). Won't be called if
     * handle_texs_gen is true.
     *
     * \param tc OpenGL tex converter
     * \param textures array of textures to bind (one per plane)
     * \param tex_width array of tex width (one per plane)
     * \param tex_height array of tex height (one per plane)
     * \return VLC_SUCCESS or a VLC error
     */
    int
    (*allocate_textures)(const struct vlc_gl_importer *importer,
                         GLuint textures[], const GLsizei tex_width[],
                         const GLsizei tex_height[]);

    /**
     * Callback to update a picture
     *
     * This function pointer cannot be NULL. The implementation should upload
     * every planes of the picture.
     *
     * \param tc OpenGL tex converter
     * \param textures array of textures to bind (one per plane)
     * \param tex_width array of tex width (one per plane)
     * \param tex_height array of tex height (one per plane)
     * \param pic picture to update
     * \param plane_offset offsets of each picture planes to read data from
     * (one per plane, can be NULL)
     * \return VLC_SUCCESS or a VLC error
     */
    int
    (*update_textures)(const struct vlc_gl_importer *importer,
                       GLuint textures[], const GLsizei tex_width[],
                       const GLsizei tex_height[], picture_t *pic,
                       const size_t plane_offsets[]);

    /**
     * Callback to allocate a picture pool
     *
     * This function pointer *can* be NULL. If NULL, A generic pool with
     * pictures allocated from the video_format_t will be used.
     *
     * \param tc OpenGL tex converter
     * \param requested_count number of pictures to allocate
     * \return the picture pool or NULL in case of error
     */
     picture_pool_t *
     (*get_pool)(const struct vlc_gl_importer *importer,
                 unsigned requested_count);
};

struct vlc_gl_importer {
    vlc_gl_t *gl;
    const opengl_vtable_t *vt;
    GLenum tex_target;

    /* True if the current API is OpenGL ES, set by the caller */
    bool is_gles;

    /* Available gl extensions (from GL_EXTENSIONS) */
    const char *glexts;

    const video_format_t *fmt;

    /* Software format (useful if fmt only exposes opaque chroma) */
    video_format_t sw_fmt;

    /* Pointer to decoder video context, set by the caller (can be NULL) */
    vlc_video_context *vctx;

    /* Set to true if textures are generated from pf_update() */
    bool handle_texs_gen;

    /* Initialized by the importer */
    struct vlc_gl_tex_cfg {
        /*
         * Texture scale factor, cannot be 0.
         * In 4:2:0, 1/1 for the Y texture and 1/2 for the UV texture(s)
         */
        vlc_rational_t w;
        vlc_rational_t h;

        GLint internal;
        GLenum format;
        GLenum type;
    } texs[PICTURE_PLANE_MAX];
    unsigned tex_count;

    void *priv;
    const struct vlc_gl_importer_ops *ops;
};

#endif
