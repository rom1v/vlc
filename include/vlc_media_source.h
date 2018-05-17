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

#include <vlc_services_discovery.h>

/* Media source was previously named "service discovery".
 * During the transition, this is just an adapter to use the existing service
 * discovery. */

# ifdef __cplusplus
extern "C" {
# endif

typedef struct services_discovery_t media_source_t;
typedef struct services_discovery_owner_t media_source_owner_t;
typedef enum services_discovery_category_e media_source_category_e;
typedef enum services_discovery_command_e media_source_command_e;
typedef enum services_discovery_capability_e media_source_capability_e;
typedef services_discovery_descriptor_t media_source_descriptor_t;

static inline int vlc_ms_control( media_source_t *p_ms, int i_query, va_list args )
{
    return vlc_sd_control( p_ms, i_query, args );
}

static inline char **vlc_ms_GetNames( vlc_object_t *p_obj, char ***pppsz_longnames, int **pp_categories)
{
    return vlc_sd_GetNames( p_obj, pppsz_longnames, pp_categories );
}
#define vlc_ms_GetNames( obj, pln, pcat ) \
        vlc_ms_GetNames( VLC_OBJECT( obj ), pln, pcat )

static inline media_source_t *vlc_ms_Create( vlc_object_t *p_parent, const char *psz_name, const media_source_owner_t *p_owner )
{
    return vlc_sd_Create( p_parent, psz_name, p_owner );
}

#define vlc_ms_Create( ppar, pn, po ) \
        vlc_ms_Create( VLC_OBJECT( ppar ), pn, po )

static inline void vlc_ms_Destroy( media_source_t *p_ms )
{
    vlc_sd_Destroy( p_ms );
}

static inline void media_source_AddItem( media_source_t *p_ms, input_item_t *p_item )
{
    services_discovery_AddItem( p_ms, p_item );
}

static inline void media_source_AddSubItem( media_source_t *p_ms, input_item_t *p_parent, input_item_t *p_item )
{
    services_discovery_AddSubItem( p_ms, p_parent, p_item );
}

VLC_DEPRECATED
static inline void media_source_AddItemCat( media_source_t *p_ms, input_item_t *p_item, const char *psz_category )
{
    services_discovery_AddItemCat( p_ms, p_item, psz_category );
}

static inline void media_source_RemoveItem( media_source_t *p_ms, input_item_t *p_item )
{
    services_discovery_RemoveItem( p_ms, p_item );
}

static inline int vlc_ms_probe_Add( vlc_probe_t *p_probe, const char *psz_name, const char *psz_longname, int i_category)
{
    return vlc_sd_probe_Add( p_probe, psz_name, psz_longname, i_category );
}

#define VLC_MS_PROBE_SUBMODULE VLC_SD_PROBE_SUBMODULE
#define VLC_MS_PROBE_HELPER VLC_SD_PROBE_HELPER

# ifdef __cplusplus
}
# endif

#endif

