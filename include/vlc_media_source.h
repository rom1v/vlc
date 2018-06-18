/*****************************************************************************
 * vlc_media_source.h : Media source
 *****************************************************************************
 * Copyright (C) 2018 VLC authors and VideoLAN
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

#ifndef VLC_MEDIA_SOURCE_H
#define VLC_MEDIA_SOURCE_H

#include <vlc_common.h>

typedef struct media_tree_t media_tree_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct media_source_t
{
    media_tree_t *p_tree;
    const char *psz_description;
} media_source_t;

VLC_API void media_source_Hold( media_source_t * );
VLC_API void media_source_Release( media_source_t * );

typedef struct media_source_provider_t
{
    struct vlc_common_members obj;
    /* all other fields are private */
} media_source_provider_t;

VLC_API media_source_provider_t *media_source_provider_Get( libvlc_int_t * );

VLC_API media_source_t *media_source_provider_GetMediaSource( media_source_provider_t *, const char *psz_name );

#ifdef __cplusplus
}
#endif

#endif

