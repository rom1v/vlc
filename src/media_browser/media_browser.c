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

#include <assert.h>
#include <vlc_arrays.h>
#include <vlc_common.h>
#include <vlc_media_tree.h>
#include <vlc_playlist.h>
#include "libvlc.h"
#include "playlist/playlist_internal.h"

typedef struct
{
    media_tree_t *p_tree;
    services_discovery_t *p_sd; /**< Loaded services discovery module */
    char psz_name[];
} sd_entry_t;

TYPEDEF_ARRAY( sd_entry_t *, sd_entry_array_t )

typedef struct
{
    media_browser_t public_data;

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
    VLC_UNUSED( psz_cat );

    /* cf playlist/services_discovery.c:playlist_sd_item_added */
    sd_entry_t *p_entry = p_sd->owner.sys;
    media_tree_t *p_tree = p_entry->p_tree;

    msg_Dbg( p_sd, "adding: %s", p_input->psz_name ? p_input->psz_name : "(null)" );

    media_tree_Lock( p_tree );

    media_node_t *p_parent_node;
    if( p_parent )
        p_parent_node = media_tree_FindByInput( p_tree, p_parent );
    else
        p_parent_node = &p_tree->p_root;

    media_tree_AddByInput( p_tree, p_input, p_parent_node, MEDIA_TREE_END );

    media_tree_Unlock( p_tree );
}

static void services_discovery_item_removed( services_discovery_t *p_sd, input_item_t *p_input )
{
    /* cf playlist/services_discovery.c:playlist_sd_item_removed */
    sd_entry_t *p_entry = p_sd->owner.sys;
    media_tree_t *p_tree = p_entry->p_tree;

    msg_Dbg( p_sd, "removing: %s", p_input->psz_name ? p_input->psz_name : "(null)" );

    media_tree_Lock( p_tree );

    media_node_t *p_node = media_tree_FindByInput( p_tree, p_input );
    if( unlikely( !p_node ) )
    {
        msg_Err( p_sd, "removing item not added"); /* SD plugin bug */
        media_tree_Unlock( p_tree );
        return;
    }

#ifndef NDEBUG
    /* Check that the item belonged to the SD */
    for( media_node_t *p = p_node->p_parent; p != &p_entry->p_tree->p_root; p = p->p_parent )
        assert( p );
#endif

    media_tree_Remove( p_tree, p_node );

    media_tree_Unlock( p_tree );
}

static sd_entry_t *CreateEntry( media_browser_t *p_mb, const char *psz_name )
{
    sd_entry_t *p_entry = malloc( sizeof( *p_entry ) + strlen( psz_name ) + 1 );
    if( unlikely( !p_entry ) )
        return NULL;

    /* vlc_sd_Create() may call services_discovery_item_added(), which will read its
     * p_tree, so it must be initialized first */
    p_entry->p_tree = media_tree_Create();
    if( unlikely( !p_entry->p_tree ) )
    {
        free( p_entry );
        return NULL;
    }

    strcpy( p_entry->psz_name, psz_name );

    struct services_discovery_owner_t owner = {
        .sys = p_entry,
        .item_added = services_discovery_item_added,
        .item_removed = services_discovery_item_removed,
    };

    p_entry->p_sd = vlc_sd_Create( p_mb, psz_name, &owner );
    if( unlikely( !p_entry->p_sd ) )
    {
        media_tree_Release( p_entry->p_tree );
        free( p_entry );
        return NULL;
    }

    return p_entry;
}

static void DestroyEntry( sd_entry_t *p_entry )
{
    media_tree_Release( p_entry->p_tree );
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

static int FindEntryIndexByTree( media_browser_private_t *p_priv, const media_tree_t *p_tree )
{
    for( int i = 0; i < p_priv->items.i_size; ++i )
    {
        sd_entry_t *p_entry = p_priv->items.p_elems[i];
        if( p_entry->p_tree == p_tree )
            return i;
    }
    return -1;
}

static sd_entry_t *FindEntryByName( media_browser_private_t *p_priv, const char *psz_name )
{
    int i = FindEntryIndexByName( p_priv, psz_name );
    return i == -1 ? NULL : p_priv->items.p_elems[i];
}

static sd_entry_t *RemoveEntryByTree( media_browser_private_t *p_priv, const media_tree_t *p_tree )
{
    int i = FindEntryIndexByTree( p_priv, p_tree );
    return i == -1 ? NULL : p_priv->items.p_elems[i];
}

media_browser_t *media_browser_Create( vlc_object_t *p_parent )
{
    media_browser_private_t *p_priv = vlc_custom_create( p_parent, sizeof( *p_priv ), "media-browser" );
    if( unlikely( !p_priv ) )
        return NULL;

    vlc_mutex_init( &p_priv->lock );
    ARRAY_INIT( p_priv->items );
    return &p_priv->public_data;
}

void media_browser_Destroy( media_browser_t *p_mb )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    /* Destroy all entries */
    FOREACH_ARRAY( sd_entry_t *p_entry, p_priv->items )
        DestroyEntry( p_entry );
    FOREACH_END()

    vlc_mutex_destroy( &p_priv->lock );
    vlc_object_release( p_mb );
}

media_tree_t *media_browser_Add( media_browser_t *p_mb, const char *psz_name )
{
    sd_entry_t *p_entry = CreateEntry( p_mb, psz_name );
    if( unlikely( !p_entry ) )
        return NULL;

    media_browser_private_t *p_priv = mb_priv( p_mb );

    /* once appended to p_priv->items, it may be released by a concurrent media_browser_Remove()
     * before this function returns */
    media_tree_Hold( p_entry->p_tree );

    vlc_mutex_lock( &p_priv->lock );
    ARRAY_APPEND( p_priv->items, p_entry );
    vlc_mutex_unlock( &p_priv->lock );

    return p_entry->p_tree;
}

void media_browser_Remove( media_browser_t *p_mb, media_tree_t *p_tree )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );
    sd_entry_t *p_entry = RemoveEntryByTree( p_priv, p_tree );
    vlc_mutex_unlock( &p_priv->lock );

    assert( p_entry );

    DestroyEntry( p_entry );
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
