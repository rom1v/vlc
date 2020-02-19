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

#include "filter.h"

struct vlc_gl_filters {
    struct vlc_list list;
};

void
vlc_gl_filters_Init(struct vlc_gl_filters *filters);

void
vlc_gl_filters_Append(struct vlc_gl_filters *filters,
                      struct vlc_gl_filter *filter);

int
vlc_gl_filters_Draw(struct vlc_gl_filters *filters);

void
vlc_gl_filters_Close(struct vlc_gl_filters *filters);

#endif
