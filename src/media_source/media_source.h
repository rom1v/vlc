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

#ifndef MEDIA_SOURCE_H
#define MEDIA_SOURCE_H

#include <vlc_media_source.h>

vlc_media_source_provider_t *vlc_media_source_provider_Create(vlc_object_t *parent);
#define vlc_media_source_provider_Create(obj) vlc_media_source_provider_Create(VLC_OBJECT(obj))

void vlc_media_source_provider_Destroy(vlc_media_source_provider_t *);

// TODO remove below (it's for temporary compatibility)

/** Check whether a given services discovery is loaded */
bool vlc_media_source_provider_IsServicesDiscoveryLoaded(vlc_media_source_provider_t *, const char *name) VLC_DEPRECATED;

/** Query a services discovery */
int vlc_media_source_provider_vaControl(vlc_media_source_provider_t *, const char *name, int query, va_list args);

static inline int vlc_media_source_provider_Control(vlc_media_source_provider_t *msp, const char *name, int query, ...)
{
    va_list args;
    va_start(args, query);
    int ret = vlc_media_source_provider_vaControl(msp, name, query, args);
    va_end(args);
    return ret;
}

#endif
