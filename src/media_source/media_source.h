/*****************************************************************************
 * media_source.h : Media source
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

#ifndef _MEDIA_SOURCE_H
#define _MEDIA_SOURCE_H

#include <vlc_media_source.h>

media_source_provider_t *media_source_provider_Create( vlc_object_t *p_parent );
void media_source_provider_Destroy( media_source_provider_t * );

// TODO remove below (it's for temporary compatibility)

/** Check whether a given services discovery is loaded */
bool media_source_provider_IsServicesDiscoveryLoaded( media_source_provider_t *, const char *psz_name ) VLC_DEPRECATED;

/** Query a services discovery */
int media_source_provider_vaControl( media_source_provider_t *, const char *psz_name, int i_query, va_list args );

static inline int media_source_provider_Control( media_source_provider_t *p_msp, const char *psz_name, int i_query, ... )
{
    va_list args;
    va_start( args, i_query );
    int ret = media_source_provider_vaControl( p_msp, psz_name, i_query, args );
    va_end( args );
    return ret;
}

#endif
