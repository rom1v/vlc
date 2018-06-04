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
#include <vlc_playlist.h>
#include <vlc_services_discovery.h>
#include "playlist_internal.h"

struct playlist_sd_entry_t {
    playlist_t *p_playlist;
    playlist_item_t *p_root;
    media_source_t *p_ms;
    const char *psz_name;
};

static void media_tree_node_added( media_tree_t *p_tree,
                                   media_node_t *p_parent,
                                   media_node_t *p_node,
                                   void *userdata )
{
    assert( p_parent );
    playlist_sd_entry_t *p = userdata;

    playlist_Lock( p->p_playlist );

    playlist_item_t *p_parent_item;
    if( p_parent != &p_tree->p_root )
        p_parent_item = playlist_ItemGetByInput( p->p_playlist, p_parent->p_input );
    else
        p_parent_item = p->p_root;
    assert( p_parent_item );
    playlist_NodeAddInput( p->p_playlist, p_node->p_input, p_parent_item, PLAYLIST_END );

    playlist_Unlock( p->p_playlist );
}

static void media_tree_node_removed( media_tree_t *p_tree,
                                     media_node_t *p_node,
                                     void *userdata )
{
    VLC_UNUSED( p_tree );
    playlist_sd_entry_t *p = userdata;

    playlist_Lock( p->p_playlist );

    playlist_item_t *p_item = playlist_ItemGetByInput( p->p_playlist, p_node->p_input );
    if( unlikely( !p_item ) )
    {
        msg_Err( p->p_playlist, "removing item not added" ); /* SD plugin bug */
        playlist_Unlock( p->p_playlist );
        return;
    }

#ifndef NDEBUG
    /* Check that the item belonged to the SD */
    for( playlist_item_t *p_i = p_item->p_parent; p_i != p->p_root; p_i = p_i->p_parent )
        assert( p_i );
#endif

    playlist_item_t *p_parent = p_item->p_parent;
    /* if the item was added under a category and the category node
       becomes empty, delete that node as well */
    if( p_parent != p->p_root && p_parent->i_children == 1 )
        p_item = p_parent;

    playlist_NodeDeleteExplicit( p->p_playlist, p_item, PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT );

    playlist_Unlock( p->p_playlist );
}

int playlist_ServicesDiscoveryAdd( playlist_t *p_playlist, const char *psz_name )
{
    playlist_sd_entry_t *p = malloc( sizeof( *p ) );
    if( unlikely( !p ) )
        return VLC_ENOMEM;

    p->p_playlist = p_playlist;

    p->psz_name = strdup( psz_name );
    if( unlikely( !p->psz_name ) )
    {
        free( p );
        return VLC_ENOMEM;
    }

    media_browser_t *p_media_browser = pl_priv( p_playlist )->p_media_browser;
    media_source_t *p_ms = media_browser_Add( p_media_browser, psz_name );
    if( !p_ms )
    {
        free( p );
        return VLC_ENOMEM;
    }

    playlist_Lock( p_playlist );
    // XXX circular dependency to retrieve description, so we use psz_name instead
    p->p_root = playlist_NodeCreate( p_playlist, psz_name, &p_playlist->root,
                                     PLAYLIST_END, PLAYLIST_RO_FLAG );
    playlist_Unlock( p_playlist );

    media_tree_callbacks_t callbacks = {
        .userdata = p,
        .pf_tree_attached = media_tree_attached_default,
        .pf_node_added = media_tree_node_added,
        .pf_node_removed = media_tree_node_removed,
    };
    media_tree_Lock( p_ms->p_tree );
    media_tree_Attach( p_ms->p_tree, &callbacks );
    media_tree_Unlock( p_ms->p_tree );

    /* use the same big playlist lock for this temporary stuff */
    playlist_private_t *p_priv = pl_priv( p_playlist );
    playlist_Lock( p_playlist );
    ARRAY_APPEND( p_priv->sd_entries, p );
    playlist_Unlock( p_playlist );

    return VLC_SUCCESS;
}

static playlist_sd_entry_t *RemoveEntry( playlist_t *p_playlist, const char *psz_name )
{
    playlist_AssertLocked( p_playlist );
    playlist_private_t *p_priv = pl_priv( p_playlist );

    playlist_sd_entry_t *p_matching = NULL;
    for( int i = 0; i < p_priv->sd_entries.i_size; ++i) {
        playlist_sd_entry_t *p = p_priv->sd_entries.p_elems[i];
        if( !strcmp( p->psz_name, psz_name ) )
        {
            p_matching = p;
            ARRAY_REMOVE( p_priv->sd_entries, i );
            break;
        }
    }

    return p_matching;
}

int playlist_ServicesDiscoveryRemove( playlist_t *p_playlist, const char *psz_name )
{
    playlist_private_t *p_priv = pl_priv( p_playlist );

    playlist_Lock( p_playlist );

    playlist_sd_entry_t *p = RemoveEntry( p_playlist, psz_name );
    assert( p );

    playlist_NodeDeleteExplicit( p_playlist, p->p_root,
                                 PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT );

    playlist_Unlock( p_playlist );

    media_browser_Remove( p_priv->p_media_browser, p->p_ms );
    media_source_Release( p->p_ms );

    free( ( void * )p->psz_name );
    free( p );

    return VLC_SUCCESS;
}

bool playlist_IsServicesDiscoveryLoaded( playlist_t *p_playlist,
                                         const char *psz_name )
{
    playlist_private_t *p_priv = pl_priv( p_playlist );
    return media_browser_IsServicesDiscoveryLoaded( p_priv->p_media_browser, psz_name );
}

int playlist_ServicesDiscoveryControl( playlist_t *p_playlist, const char *psz_name, int i_control, ... )
{
    playlist_private_t *p_priv = pl_priv( p_playlist );
    va_list args;
    va_start( args, i_control );
    int ret = media_browser_vaControl( p_priv->p_media_browser, psz_name, i_control, args );
    va_end( args );
    return ret;
}

void playlist_ServicesDiscoveryKillAll( playlist_t *p_playlist )
{
    playlist_private_t *p_priv = pl_priv( p_playlist );
    playlist_Lock( p_playlist );
    FOREACH_ARRAY( playlist_sd_entry_t *p, p_priv->sd_entries )
        media_source_Release( p->p_ms );
        playlist_NodeDeleteExplicit( p_playlist, p->p_root,
                                     PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT );
        free( ( void * )p->psz_name );
        free( p );
    FOREACH_END()
    ARRAY_RESET( p_priv->sd_entries );
    playlist_Unlock( p_playlist );
}
