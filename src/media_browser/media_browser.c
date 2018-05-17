/*****************************************************************************
 * media_browser.c : Browser for services discovery
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

#include <media_browser/media_browser.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_arrays.h>
#include <vlc_common.h>
#include <vlc_playlist.h>
#include "libvlc.h"
#include "playlist/playlist_internal.h"

typedef struct
{
    playlist_item_t *p_node;
    services_discovery_t *p_sd; /**< Loaded services discovery module */
    char psz_name[];
} sd_entry_t;

TYPEDEF_ARRAY( sd_entry_t *, sd_entry_array_t )

typedef struct
{
    media_browser_t public_data; /* public data */

    /* For now, the media tree is still managed by the playlist */
    playlist_t *p_playlist;

    vlc_mutex_t lock;
    sd_entry_array_t items;
} media_browser_private_t;

#define mb_priv( mb ) container_of( mb, media_browser_private_t, public_data )

/* A new item has been added to a certain services discovery */
static void services_discovery_item_added( services_discovery_t *p_sd,
                                           input_item_t *p_parent, input_item_t *p_input,
                                           const char *psz_cat )
{
    assert( !p_parent || !psz_cat );

    /* cf playlist/services_discovery.c:playlist_sd_item_added */
    sd_entry_t *p_entry = p_sd->owner.sys;
    media_browser_t *p_mb = ( media_browser_t * )p_sd->obj.parent;
    media_browser_private_t *p_priv = mb_priv( p_mb );
    playlist_t *p_playlist = p_priv->p_playlist;
    playlist_item_t *p_node;

    msg_Dbg( p_sd, "adding: %s", p_input->psz_name ? p_input->psz_name : "(null)" );

    const char *psz_longname = p_sd->description ? p_sd->description : "?";

    playlist_Lock( p_playlist );
    if( !p_entry->p_node )
        p_entry->p_node = playlist_NodeCreate( p_playlist, psz_longname, &p_playlist->root,
                                               PLAYLIST_END, PLAYLIST_RO_FLAG );

    if( p_parent )
        p_node = playlist_ItemGetByInput( p_playlist, p_parent );
    else
    {
        if( !psz_cat )
            p_node = p_entry->p_node;
        else
        {
           /* Parent is NULL (root) and category is specified.
             * This is clearly a hack. TODO: remove this. */
            p_node = playlist_ChildSearchName( p_entry->p_node, psz_cat);
            if ( !p_node )
                p_node = playlist_NodeCreate( p_playlist, psz_cat, p_entry->p_node,
                                              PLAYLIST_END, PLAYLIST_RO_FLAG );
        }
    }

    playlist_NodeAddInput( p_playlist, p_input, p_node, PLAYLIST_END );
    playlist_Unlock( p_playlist );
}

static void services_discovery_item_removed( services_discovery_t *p_sd, input_item_t *p_input )
{
    /* cf playlist/services_discovery.c:playlist_sd_item_removed */
    sd_entry_t *p_entry = p_sd->owner.sys;
    media_browser_t *p_mb = ( media_browser_t * )p_sd->obj.parent;
    media_browser_private_t *p_priv = mb_priv( p_mb );
    playlist_t *p_playlist = p_priv->p_playlist;

    msg_Dbg( p_sd, "removing: %s", p_input->psz_name ? p_input->psz_name : "(null)" );

    playlist_Lock( p_playlist );
    playlist_item_t *p_item = playlist_ItemGetByInput( p_playlist, p_input );
    if( unlikely( !p_item ) )
    {
        msg_Err( p_sd, "removing item not added"); /* MS plugin bug */
        playlist_Unlock( p_playlist );
        return;
    }

#ifndef NDEBUG
    /* Check that the item belonged to the MS */
    for( playlist_item_t *p = p_item->p_parent; p != p_entry->p_node; p = p->p_parent )
        assert( p );
#endif

    playlist_item_t *p_node = p_item->p_parent;
    /* if the item was added under a category and the category node
       becomes empty, delete that node as well */
    if ( p_node != p_entry->p_node && p_node->i_children == 1)
        p_item = p_node;

    playlist_NodeDeleteExplicit( p_playlist, p_item,
                                 PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT );
    playlist_Unlock( p_playlist );
}

static sd_entry_t *CreateEntry( media_browser_t *p_mb, const char *psz_name )
{
    sd_entry_t *p_entry = malloc( sizeof( *p_entry ) + strlen( psz_name ) + 1 );
    if( unlikely( !p_entry ) )
        return NULL;

    struct services_discovery_owner_t owner = {
        .sys = p_entry,
        .item_added = services_discovery_item_added,
        .item_removed = services_discovery_item_removed,
    };

    /* vlc_sd_Create() may call services_discovery_item_added(), which will read its
     * p_node, so it must be initialized first */
    p_entry->p_node = NULL;
    strcpy( p_entry->psz_name, psz_name );

    p_entry->p_sd = vlc_sd_Create( p_mb, psz_name, &owner );
    if( unlikely( !p_entry->p_sd ) )
    {
        free( p_entry );
        return NULL;
    }

    return p_entry;
}

static void DestroyEntry( sd_entry_t *p_entry )
{
    vlc_sd_Destroy( p_entry->p_sd );
    free( p_entry );
}

static int FindEntryIndexByName( media_browser_private_t *p_priv, const char *psz_name )
{
    for( int i = 0; i < p_priv->items.i_size; ++i )
    {
        sd_entry_t *p_entry = p_priv->items.p_elems[i];
        if( !strcmp( psz_name, p_entry->psz_name ) )
            return i;
    }
    return -1;
}

static sd_entry_t *FindEntryByName( media_browser_private_t *p_priv, const char *psz_name )
{
    int i = FindEntryIndexByName( p_priv, psz_name );
    return i == -1 ? NULL : p_priv->items.p_elems[i];
}

static sd_entry_t *RemoveEntryByName( media_browser_private_t *p_priv, const char *psz_name )
{
    int i = FindEntryIndexByName( p_priv, psz_name );
    if( i == -1 )
        return NULL;

    sd_entry_t *p_entry = p_priv->items.p_elems[i];
    ARRAY_REMOVE( p_priv->items, i );
    return p_entry;
}

media_browser_t *media_browser_Create( vlc_object_t *p_parent, playlist_t *p_playlist )
{
    media_browser_private_t *p_priv = vlc_custom_create( p_parent, sizeof( *p_priv ), "media-source-manager" );
    if( unlikely( !p_priv ) )
        return NULL;

    p_priv->p_playlist = p_playlist;
    vlc_mutex_init( &p_priv->lock );
    ARRAY_INIT( p_priv->items );
    return &p_priv->public_data;
}

void media_browser_Destroy( media_browser_t *p_mb )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    /* Remove all services discovery from the playlist */
    playlist_t *p_playlist = p_priv->p_playlist;
    FOREACH_ARRAY( sd_entry_t *p_entry, p_priv->items )
        if ( p_entry->p_node )
        {
            playlist_Lock( p_playlist );
            playlist_NodeDeleteExplicit( p_playlist, p_entry->p_node,
                                         PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT );
            playlist_Unlock( p_playlist );
        }
        DestroyEntry( p_entry );
    FOREACH_END()

    vlc_mutex_destroy( &p_priv->lock );
    vlc_object_release( p_mb );
}

int media_browser_Add( media_browser_t *p_mb, const char *psz_name )
{
    sd_entry_t *p_entry = CreateEntry( p_mb, psz_name );
    if( unlikely( !p_entry ) )
        return VLC_ENOMEM;

    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );
    ARRAY_APPEND( p_priv->items, p_entry );
    vlc_mutex_unlock( &p_priv->lock );

    /* Backward compatibility with Qt UI: create the node even if the SD
     * has not discovered any item. */
    playlist_t *p_playlist = p_priv->p_playlist;
    if( !p_entry->p_node && p_entry->p_sd->description )
    {
        playlist_Lock( p_playlist );
        p_entry->p_node = playlist_NodeCreate( p_playlist, p_entry->p_sd->description,
                                               &p_playlist->root, PLAYLIST_END, PLAYLIST_RO_FLAG );
        playlist_Unlock( p_playlist );
    }

    return VLC_SUCCESS;
}

int media_browser_Remove( media_browser_t *p_mb, const char *psz_name )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );
    sd_entry_t *p_entry = RemoveEntryByName( p_priv, psz_name );
    vlc_mutex_unlock( &p_priv->lock );

    if( unlikely( !p_entry ) )
    {
        msg_Warn( p_mb, "Media source %s is not loaded", psz_name );
        return VLC_EGENERIC;
    }

    /* Remove the playlist node if it exists */
    playlist_t *p_playlist = p_priv->p_playlist;
    if ( p_entry->p_node )
    {
        playlist_Lock( p_playlist );
        playlist_NodeDeleteExplicit( p_playlist, p_entry->p_node,
                                     PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT );
        playlist_Unlock( p_playlist );
    }

    DestroyEntry( p_entry );
    return VLC_SUCCESS;
}

bool media_browser_IsServicesDiscoveryLoaded( media_browser_t *p_mb, const char *psz_name )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );
    int i = FindEntryIndexByName( p_priv, psz_name );
    vlc_mutex_unlock( &p_priv->lock );

    return i != -1;
}

int media_browser_vaControl( media_browser_t *p_mb, const char *psz_name, int i_query, va_list args )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );

    sd_entry_t *p_entry = FindEntryByName( p_priv, psz_name );
    assert( p_entry );

    // XXX must we keep the lock? (playlist_ServicesDiscoveryControl did)
    int ret = vlc_sd_control( p_entry->p_sd, i_query, args );

    vlc_mutex_unlock( &p_priv->lock );

    return ret;
}
