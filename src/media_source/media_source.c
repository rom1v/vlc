/*****************************************************************************
 * media_source.c : Media source
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

#include "media_source/media_source.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stdatomic.h>
#include <vlc_media_tree.h>
#include <vlc_playlist.h>
#include <vlc_services_discovery.h>
#include "libvlc.h"
#include "playlist/playlist_internal.h"
#include "media_tree/media_tree.h"

typedef struct
{
    media_source_t public_data;

    services_discovery_t *p_sd;
    atomic_uint refs;
    media_source_provider_t *p_owner;
    struct vlc_list siblings;
    char psz_name[];
} media_source_private_t;

#define ms_priv( ms ) container_of( ms, media_source_private_t, public_data )

typedef struct
{
    media_source_provider_t public_data;

    vlc_mutex_t lock;
    struct vlc_list media_sources;
} media_source_provider_private_t;

#define msp_priv( msp ) container_of( msp, media_source_provider_private_t, public_data )

/* A new item has been added to a certain services discovery */
static void services_discovery_item_added( services_discovery_t *p_sd,
                                           input_item_t *p_parent, input_item_t *p_input,
                                           const char *psz_cat )
{
    assert( !p_parent || !psz_cat );
    VLC_UNUSED( psz_cat );

    media_source_t *p_ms = p_sd->owner.sys;
    media_tree_t *p_tree = p_ms->p_tree;

    msg_Dbg( p_sd, "adding: %s", p_input->psz_name ? p_input->psz_name : "(null)" );

    media_tree_Lock( p_tree );

    media_node_t *p_parent_node;
    if( p_parent )
        p_parent_node = media_tree_Find( p_tree, p_parent );
    else
        p_parent_node = &p_tree->p_root;

    media_tree_Add( p_tree, p_input, p_parent_node, MEDIA_TREE_END );

    media_tree_Unlock( p_tree );
}

static void services_discovery_item_removed( services_discovery_t *p_sd, input_item_t *p_input )
{
    media_source_t *p_ms = p_sd->owner.sys;
    media_tree_t *p_tree = p_ms->p_tree;

    msg_Dbg( p_sd, "removing: %s", p_input->psz_name ? p_input->psz_name : "(null)" );

    media_tree_Lock( p_tree );

    media_node_t *p_node = media_tree_Find( p_tree, p_input );
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

static const struct services_discovery_callbacks media_source_provider_sd_cbs = {
    .item_added = services_discovery_item_added,
    .item_removed = services_discovery_item_removed,
};

static inline void AssertLocked( media_source_provider_t *p_msp )
{
    media_source_provider_private_t *p_priv = msp_priv( p_msp );
    vlc_assert_locked( &p_priv->lock );
}

static media_source_t *MediaSourceCreate( media_source_provider_t *p_msp, const char *psz_name )
{
    media_source_private_t *p_priv = malloc( sizeof( *p_priv ) + strlen( psz_name ) + 1 );
    if( unlikely( !p_priv ) )
        return NULL;

    atomic_init( &p_priv->refs, 1 );

    media_source_t *p_ms = &p_priv->public_data;

    /* vlc_sd_Create() may call services_discovery_item_added(), which will read its
     * p_tree, so it must be initialized first */
    p_ms->p_tree = media_tree_Create( VLC_OBJECT( p_msp ) );
    if( unlikely( !p_ms->p_tree ) )
    {
        free( p_ms );
        return NULL;
    }

    strcpy( p_priv->psz_name, psz_name );

    struct services_discovery_owner_t owner = {
        .cbs = &media_source_provider_sd_cbs,
        .sys = p_ms,
    };

    p_priv->p_sd = vlc_sd_Create( p_msp, psz_name, &owner );
    if( unlikely( !p_priv->p_sd ) )
    {
        media_tree_Release( p_ms->p_tree );
        free( p_ms );
        return NULL;
    }

    /* p_sd->description is set during vlc_sd_Create() */
    p_ms->psz_description = p_priv->p_sd->description;

    p_priv->p_owner = p_msp;

    return p_ms;
}

static void Remove( media_source_provider_t *, media_source_t * );

static void MediaSourceDestroy( media_source_t *p_ms )
{
    media_source_private_t *p_priv = ms_priv( p_ms );
    Remove( p_priv->p_owner, p_ms );
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

static media_source_t *FindByName( media_source_provider_private_t *p_priv, const char *psz_name )
{
    vlc_assert_locked( &p_priv->lock );
    media_source_private_t *p_entry;
    vlc_list_foreach( p_entry, &p_priv->media_sources, siblings )
        if( !strcmp( psz_name, p_entry->psz_name ) )
            return &p_entry->public_data;
    return NULL;
}

media_source_provider_t *media_source_provider_Get( libvlc_int_t *libvlc )
{
    return libvlc_priv( libvlc )->p_media_source_provider;
}

media_source_provider_t *media_source_provider_Create( vlc_object_t *p_parent )
{
    media_source_provider_private_t *p_priv = vlc_custom_create( p_parent, sizeof( *p_priv ), "media-source-provider" );
    if( unlikely( !p_priv ) )
        return NULL;

    vlc_mutex_init( &p_priv->lock );
    vlc_list_init( &p_priv->media_sources );
    return &p_priv->public_data;
}

void media_source_provider_Destroy( media_source_provider_t *p_msp )
{
    media_source_provider_private_t *p_priv = msp_priv( p_msp );

    vlc_mutex_destroy( &p_priv->lock );
    vlc_object_release( p_msp );
}

static media_source_t *AddServiceDiscovery( media_source_provider_t *p_msp, const char *psz_name )
{
    AssertLocked( p_msp );

    media_source_t *p_ms = MediaSourceCreate( p_msp, psz_name );
    if( unlikely( !p_ms ) )
        return NULL;

    media_source_provider_private_t *p_priv = msp_priv( p_msp );

    vlc_list_append( &ms_priv( p_ms )->siblings, &p_priv->media_sources );
    return p_ms;
}

media_source_t *media_source_provider_GetMediaSource( media_source_provider_t *p_msp, const char *psz_name )
{
    media_source_provider_private_t *p_priv = msp_priv( p_msp );

    vlc_mutex_lock( &p_priv->lock );

    media_source_t *p_ms = FindByName( p_priv, psz_name );
    if( !p_ms )
    {
        p_ms = AddServiceDiscovery( p_msp, psz_name );
        if( unlikely( !p_ms ) )
        {
            vlc_mutex_unlock( &p_priv->lock );
            return NULL;
        }
    }

    vlc_mutex_unlock( &p_priv->lock );

    return p_ms;
}

static void Remove( media_source_provider_t *p_msp, media_source_t *p_ms )
{
    media_source_provider_private_t *p_priv = msp_priv( p_msp );

    vlc_mutex_lock( &p_priv->lock );
    vlc_list_remove( &ms_priv( p_ms )->siblings );
    vlc_mutex_unlock( &p_priv->lock );
}

bool media_source_provider_IsServicesDiscoveryLoaded( media_source_provider_t *p_msp, const char *psz_name )
{
    media_source_provider_private_t *p_priv = msp_priv( p_msp );

    vlc_mutex_lock( &p_priv->lock );
    media_source_t *p_ms = FindByName( p_priv, psz_name );
    vlc_mutex_unlock( &p_priv->lock );

    return p_ms != NULL;
}

int media_source_provider_vaControl( media_source_provider_t *p_msp, const char *psz_name, int i_query, va_list args )
{
    media_source_provider_private_t *p_priv = msp_priv( p_msp );

    vlc_mutex_lock( &p_priv->lock );

    media_source_t *p_ms = FindByName( p_priv, psz_name );
    assert( p_ms );

    // XXX must we keep the lock? (playlist_ServicesDiscoveryControl did)
    media_source_private_t *p = ms_priv( p_ms );
    int ret = vlc_sd_control( p->p_sd, i_query, args );

    vlc_mutex_unlock( &p_priv->lock );

    return ret;
}
