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
#include <stdatomic.h>
#include <vlc_arrays.h>
#include <vlc_common.h>
#include <vlc_media_tree.h>
#include <vlc_playlist.h>
#include "libvlc.h"
#include "playlist/playlist_internal.h"

typedef struct
{
    media_source_t public_data;

    services_discovery_t *p_sd; /**< Loaded services discovery module */
    atomic_uint refs;
    char psz_name[];
} media_source_private_t;

#define ms_priv( ms ) container_of( ms, media_source_private_t, public_data );

TYPEDEF_ARRAY( media_source_t *, media_source_array_t )

typedef struct
{
    media_browser_t public_data;

    vlc_mutex_t lock;
    media_source_array_t media_sources;
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
    media_source_t *p_ms = p_sd->owner.sys;
    media_tree_t *p_tree = p_ms->p_tree;

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
    media_source_t *p_ms = p_sd->owner.sys;
    media_tree_t *p_tree = p_ms->p_tree;

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
    for( media_node_t *p = p_node->p_parent; p != &p_ms->p_tree->p_root; p = p->p_parent )
        assert( p );
#endif

    media_tree_Remove( p_tree, p_node );

    media_tree_Unlock( p_tree );
}

static media_source_t *MediaSourceCreate( media_browser_t *p_mb, const char *psz_name )
{
    media_source_private_t *p_priv = malloc( sizeof( *p_priv ) + strlen( psz_name ) + 1 );
    if( unlikely( !p_priv ) )
        return NULL;

    atomic_init( &p_priv->refs, 1 );

    media_source_t *p_ms = &p_priv->public_data;

    /* vlc_sd_Create() may call services_discovery_item_added(), which will read its
     * p_tree, so it must be initialized first */
    p_ms->p_tree = media_tree_Create();
    if( unlikely( !p_ms->p_tree ) )
    {
        free( p_ms );
        return NULL;
    }

    strcpy( p_priv->psz_name, psz_name );

    struct services_discovery_owner_t owner = {
        .sys = p_ms,
        .item_added = services_discovery_item_added,
        .item_removed = services_discovery_item_removed,
    };

    p_priv->p_sd = vlc_sd_Create( p_mb, psz_name, &owner );
    if( unlikely( !p_priv->p_sd ) )
    {
        media_tree_Release( p_ms->p_tree );
        free( p_ms );
        return NULL;
    }

    return p_ms;
}

static void MediaSourceDestroy( media_source_t *p_ms )
{
    media_source_private_t *p_priv = ms_priv( p_ms );
    vlc_sd_Destroy( p_priv->p_sd );
    media_tree_Release( p_ms->p_tree );
    free( p_priv );
}

void media_source_Hold( media_source_t *p_ms )
{
    media_source_private_t *p_priv = ms_priv( p_ms );
    atomic_fetch_add( &p_priv->refs, 1 );
}

void media_source_Release( media_source_t *p_ms )
{
    media_source_private_t *p_priv = ms_priv( p_ms );
    if( atomic_fetch_sub( &p_priv->refs, 1 ) == 1 )
        MediaSourceDestroy( p_ms );
}

static int FindIndexByName( media_browser_private_t *p_priv, const char *psz_name )
{
    for( int i = 0; i < p_priv->media_sources.i_size; ++i )
    {
        media_source_t *p_ms = p_priv->media_sources.p_elems[i];
        media_source_private_t *p = ms_priv( p_ms );
        if( !strcmp( psz_name, p->psz_name ) )
            return i;
    }
    return -1;
}

static int FindIndex( media_browser_private_t *p_priv, const media_source_t *p_ms )
{
    for( int i = 0; i < p_priv->media_sources.i_size; ++i )
    {
        media_source_t *p_cur = p_priv->media_sources.p_elems[i];
        if( p_cur == p_ms )
            return i;
    }
    return -1;
}

static media_source_t *FindByName( media_browser_private_t *p_priv, const char *psz_name )
{
    int i = FindIndexByName( p_priv, psz_name );
    return i == -1 ? NULL : p_priv->media_sources.p_elems[i];
}

static bool Remove( media_browser_private_t *p_priv, const media_source_t *p_ms )
{
    int i = FindIndex( p_priv, p_ms );
    if( i == -1 )
        return false;

    ARRAY_REMOVE( p_priv->media_sources, i );
    return true;
}

media_browser_t *media_browser_Create( vlc_object_t *p_parent )
{
    media_browser_private_t *p_priv = vlc_custom_create( p_parent, sizeof( *p_priv ), "media-browser" );
    if( unlikely( !p_priv ) )
        return NULL;

    vlc_mutex_init( &p_priv->lock );
    ARRAY_INIT( p_priv->media_sources );
    return &p_priv->public_data;
}

void media_browser_Destroy( media_browser_t *p_mb )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    /* Destroy all entries */
    FOREACH_ARRAY( media_source_t *p_ms, p_priv->media_sources )
        media_source_Release( p_ms );
    FOREACH_END()

    vlc_mutex_destroy( &p_priv->lock );
    vlc_object_release( p_mb );
}

media_source_t *media_browser_Add( media_browser_t *p_mb, const char *psz_name )
{
    media_source_t *p_ms = MediaSourceCreate( p_mb, psz_name );
    if( unlikely( !p_ms ) )
        return NULL;

    /* once appended to p_priv->media_sources, it may be released by a concurrent media_browser_Remove()
     * before this function returns */
    media_source_Hold( p_ms );

    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );
    ARRAY_APPEND( p_priv->media_sources, p_ms );
    vlc_mutex_unlock( &p_priv->lock );

    return p_ms;
}

void media_browser_Remove( media_browser_t *p_mb, media_source_t *p_ms )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );
    bool found = Remove( p_priv, p_ms );
#ifdef NDEBUG
    VLC_UNUSED( found );
#else
    assert( found );
#endif
    vlc_mutex_unlock( &p_priv->lock );

    media_source_Release( p_ms );
}

bool media_browser_IsServicesDiscoveryLoaded( media_browser_t *p_mb, const char *psz_name )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );
    int i = FindIndexByName( p_priv, psz_name );
    vlc_mutex_unlock( &p_priv->lock );

    return i != -1;
}

int media_browser_vaControl( media_browser_t *p_mb, const char *psz_name, int i_query, va_list args )
{
    media_browser_private_t *p_priv = mb_priv( p_mb );

    vlc_mutex_lock( &p_priv->lock );

    media_source_t *p_ms = FindByName( p_priv, psz_name );
    assert( p_ms );

    // XXX must we keep the lock? (playlist_ServicesDiscoveryControl did)
    media_source_private_t *p = ms_priv( p_ms );
    int ret = vlc_sd_control( p->p_sd, i_query, args );

    vlc_mutex_unlock( &p_priv->lock );

    return ret;
}
