/*****************************************************************************
 * filters.h
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

#ifndef VLC_GL_FILTERS_H
#define VLC_GL_FILTERS_H

#include <vlc_common.h>
#include <vlc_list.h>
#include <vlc_opengl.h>

#include "filter.h"
#include "gl_api.h"
#include "sampler.h"

struct vlc_gl_filters {
    struct vlc_gl_t *gl;
    const struct vlc_gl_api *api;

    struct vlc_list list; /**< list of vlc_gl_filter.node */
};

/**
 * Initialize the filters list
 *
 * \param filters the filters list
 * \param gl the OpenGL context
 * \param api the OpenGL api
 */
void
vlc_gl_filters_Init(struct vlc_gl_filters *filters, struct vlc_gl_t *gl,
                    const struct vlc_gl_api *api);

/**
 * Create and append a filter loaded from a module to the filters list
 *
 * The created filter is owned by the filters list.
 *
 * \param filters the filters list
 * \param name the module name
 * \param config the module configuration
 * \param sampler the OpenGL sampler to use from the filter
 * \return a weak reference to the filter (NULL on error)
 */
struct vlc_gl_filter *
vlc_gl_filters_Append(struct vlc_gl_filters *filters, const char *name,
                      const config_chain_t *config,
                      struct vlc_gl_sampler *sampler);

/**
 * Draw by executing all the filters
 *
 * \param filters the filters list
 */
int
vlc_gl_filters_Draw(struct vlc_gl_filters *filters);

/**
 * Close all the filters and destroy the filters list
 *
 * \param filters the filters list
 */
void
vlc_gl_filters_Destroy(struct vlc_gl_filters *filters);

#endif
