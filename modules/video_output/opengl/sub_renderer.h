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

#ifndef VLC_SUB_RENDERER_H
#define VLC_SUB_RENDERER_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>

struct vlc_sub_renderer;

struct vlc_sub_renderer *
vlc_sub_renderer_New(void);

void
vlc_sub_renderer_Delete(struct vlc_sub_renderer *sr);

int
vlc_sub_renderer_Prepare(struct vlc_sub_renderer *sr, subpicture_t *subpicture);

int
vlc_sub_renderer_Draw(struct vlc_sub_renderer *sr);

#endif

