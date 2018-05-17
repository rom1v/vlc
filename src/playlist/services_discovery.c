/*****************************************************************************
 * services_discovery.c : Manage playlist services_discovery modules
 *****************************************************************************
 * Copyright (C) 1999-2004 VLC authors and VideoLAN
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <assert.h>

#include <vlc_common.h>
#include <vlc_media_browser.h>
#include <vlc_playlist.h>
#include <vlc_services_discovery.h>
#include "playlist_internal.h"

int playlist_ServicesDiscoveryAdd(playlist_t *playlist, const char *chain)
{
    playlist_private_t *p_priv = pl_priv(playlist);
    return media_browser_Add( p_priv->p_mb, chain );
}

int playlist_ServicesDiscoveryRemove(playlist_t *playlist, const char *name)
{
    playlist_private_t *p_priv = pl_priv(playlist);
    return media_browser_Remove( p_priv->p_mb, name );
}

bool playlist_IsServicesDiscoveryLoaded( playlist_t * playlist,
                                         const char *psz_name )
{
    playlist_private_t *p_priv = pl_priv(playlist);
    return media_browser_IsMediaSourceLoaded( p_priv->p_mb, psz_name );
}

int playlist_ServicesDiscoveryControl( playlist_t *playlist, const char *psz_name, int i_control, ... )
{
    playlist_private_t *p_priv = pl_priv( playlist );
    va_list args;
    va_start( args, i_control );
    int ret = media_browser_vaControl( p_priv->p_mb, psz_name, i_control, args );
    va_end( args );
    return ret;
}
