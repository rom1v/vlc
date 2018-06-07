/*****************************************************************************
 * media_tree.h : Media tree
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

#include "media_tree.h"

#include <assert.h>
#include <stdatomic.h>
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_threads.h>
#include "libvlc.h"

struct media_tree_connection_t
{
    const media_tree_callbacks_t *cbs;
    void *userdata;
    struct vlc_list siblings;
};

typedef struct
{
    media_tree_t public_data;

    struct vlc_list connections;
    vlc_mutex_t lock;
    atomic_uint refs;
} media_tree_private_t;

#define mt_priv( mt ) container_of( mt, media_tree_private_t, public_data );

media_tree_t *media_tree_Create( vlc_object_t *p_parent )
{
    media_tree_private_t *p_priv = vlc_custom_create( p_parent, sizeof( *p_priv ), "media-tree" );
    if( unlikely( !p_priv ) )
        return NULL;

    vlc_mutex_init( &p_priv->lock );
    atomic_init( &p_priv->refs, 1 );
    vlc_list_init( &p_priv->connections );

    media_tree_t *p_tree = &p_priv->public_data;
    media_node_t *p_root = &p_tree->p_root;
    p_root->p_input = NULL;
    p_root->p_parent = NULL;
    ARRAY_INIT( p_root->children );

    return p_tree;
}

static inline void AssertLocked( media_tree_t *p_tree )
{
    media_tree_private_t *p_priv = mt_priv( p_tree );
    vlc_assert_locked( &p_priv->lock );
}

static void NotifyTreeConnected( media_tree_t *p_tree )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_connection_t *p_conn;
    vlc_list_foreach( p_conn, &p_priv->connections, siblings )
        if( p_conn->cbs->pf_tree_connected )
            p_conn->cbs->pf_tree_connected( p_tree, p_conn->userdata );
}

static void NotifyNodeAdded( media_tree_t *p_tree, media_node_t *p_node )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_connection_t *p_conn;
    vlc_list_foreach( p_conn, &p_priv->connections, siblings )
        if( p_conn->cbs->pf_node_added )
            p_conn->cbs->pf_node_added( p_tree, p_node, p_conn->userdata );
}

static void NotifyNodeRemoved( media_tree_t *p_tree, media_node_t *p_node )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_connection_t *p_conn;
    vlc_list_foreach( p_conn, &p_priv->connections, siblings )
        if( p_conn->cbs->pf_node_removed )
            p_conn->cbs->pf_node_removed( p_tree, p_node, p_conn->userdata );
}

static void NotifySubtreeAdded( media_tree_t *p_tree, media_node_t *p_node )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_connection_t *p_conn;
    vlc_list_foreach( p_conn, &p_priv->connections, siblings )
        if( p_conn->cbs->pf_subtree_added )
            p_conn->cbs->pf_subtree_added( p_tree, p_node, p_conn->userdata );
}

static void NotifyInputChanged( media_tree_t *p_tree, media_node_t *p_node )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_connection_t *p_conn;
    vlc_list_foreach( p_conn, &p_priv->connections, siblings )
        if( p_conn->cbs->pf_input_updated )
            p_conn->cbs->pf_input_updated( p_tree, p_node, p_conn->userdata );
}

static media_node_t *FindNodeByInput( media_node_t *p_node, input_item_t *p_input )
{
    if( p_node->p_input == p_input )
        return p_node;

    FOREACH_ARRAY( media_node_t *p, p_node->children )
        media_node_t *p_result = FindNodeByInput( p, p_input );
        if( p_result )
            return p_result;
    FOREACH_END()

    return NULL;
}

static media_node_t *AddChild( media_tree_t *p_tree, input_item_t *p_input, media_node_t *p_parent, int i_pos );

static void AddSubtree( media_tree_t *p_tree, media_node_t *p_to, input_item_node_t *p_from )
{
    for( int i = 0; i < p_from->i_children; ++i )
    {
        input_item_node_t *p_child = p_from->pp_children[i];
        media_node_t *p_node = AddChild( p_tree, p_child->p_item, p_to, MEDIA_TREE_END );
        if( unlikely( !p_node ) )
        {
            msg_Warn( p_tree, "Cannot create node");
            continue;
        }
        AddSubtree( p_tree, p_node, p_child );
    }
}

static void input_item_subtree_added( const vlc_event_t *p_event, void *userdata )
{
    media_tree_t *p_tree = userdata;
    input_item_t *p_input = p_event->p_obj;
    input_item_node_t *p_from = p_event->u.input_item_subitem_tree_added.p_root;

    media_tree_Lock( p_tree );
    // TODO Rather than FindNodeByInput(), store the node associated in the input in a structured userdata
    media_node_t *p_subtree_root = FindNodeByInput( &p_tree->p_root, p_input );
    if( unlikely( !p_subtree_root ) )
    {
        msg_Warn( p_tree, "Did not find expected node for subtree");
        media_tree_Unlock( p_tree );
        return;
    }

    AddSubtree( p_tree, p_subtree_root, p_from );
    NotifySubtreeAdded( p_tree, p_subtree_root );
    media_tree_Unlock( p_tree );
}

static void input_item_changed( const vlc_event_t *p_event, void *userdata )
{
    media_tree_t *p_tree = userdata;
    input_item_t *p_input = p_event->p_obj;

    media_tree_Lock( p_tree );
    // TODO Rather than FindNodeByInput(), store the node associated in the input in a structured userdata
    media_node_t *p_node = FindNodeByInput( &p_tree->p_root, p_input );
    if( unlikely( !p_node ) )
    {
        msg_Warn( p_tree, "Cannot find node");
        media_tree_Unlock( p_tree );
        return;
    }

    NotifyInputChanged( p_tree, p_node );
    media_tree_Unlock( p_tree );
}

static void RegisterInputEvents( media_tree_t *p_tree, input_item_t *p_input )
{
    vlc_event_manager_t *p_em = &p_input->event_manager;
    vlc_event_attach( p_em, vlc_InputItemSubItemTreeAdded, input_item_subtree_added, p_tree );
    vlc_event_attach( p_em, vlc_InputItemDurationChanged, input_item_changed, p_tree );
    vlc_event_attach( p_em, vlc_InputItemMetaChanged, input_item_changed, p_tree );
    vlc_event_attach( p_em, vlc_InputItemNameChanged, input_item_changed, p_tree );
    vlc_event_attach( p_em, vlc_InputItemInfoChanged, input_item_changed, p_tree );
    vlc_event_attach( p_em, vlc_InputItemErrorWhenReadingChanged, input_item_changed, p_tree );
}

static void DeregisterInputEvents( media_tree_t *p_tree, input_item_t *p_input )
{
    vlc_event_manager_t *p_em = &p_input->event_manager;
    vlc_event_detach( p_em, vlc_InputItemSubItemTreeAdded, input_item_subtree_added, p_tree );
    vlc_event_detach( p_em, vlc_InputItemDurationChanged, input_item_changed, p_tree );
    vlc_event_detach( p_em, vlc_InputItemMetaChanged, input_item_changed, p_tree );
    vlc_event_detach( p_em, vlc_InputItemNameChanged, input_item_changed, p_tree );
    vlc_event_detach( p_em, vlc_InputItemInfoChanged, input_item_changed, p_tree );
    vlc_event_detach( p_em, vlc_InputItemErrorWhenReadingChanged, input_item_changed, p_tree );
}

static void DestroyNodeAndChildren( media_tree_t *, media_node_t * );

static void DestroyChildren( media_tree_t *p_tree, media_node_t *p_node )
{
    FOREACH_ARRAY( media_node_t *p_child, p_node->children )
        DestroyNodeAndChildren( p_tree, p_child );
    FOREACH_END()
}

static void DestroyNodeAndChildren( media_tree_t *p_tree, media_node_t *p_node )
{
    DestroyChildren( p_tree, p_node );
    DeregisterInputEvents( p_tree, p_node->p_input );
    input_item_Release( p_node->p_input );
    free( p_node );
}

static void Destroy( media_tree_t *p_tree )
{
    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_connection_t *p_conn;
    vlc_list_foreach( p_conn, &p_priv->connections, siblings )
        free( p_conn );
    vlc_list_init( &p_priv->connections ); /* reset */
    DestroyChildren( p_tree, &p_tree->p_root );
    vlc_mutex_destroy( &p_priv->lock );
    vlc_object_release( p_tree );
}

void media_tree_Hold( media_tree_t *p_tree )
{
    media_tree_private_t *p_priv = mt_priv( p_tree );
    atomic_fetch_add( &p_priv->refs, 1 );
}

void media_tree_Release( media_tree_t *p_tree )
{
    media_tree_private_t *p_priv = mt_priv( p_tree );
    if( atomic_fetch_sub( &p_priv->refs, 1 ) == 1 )
        Destroy( p_tree );
}

void media_tree_Lock( media_tree_t *p_tree )
{
    media_tree_private_t *p_priv = mt_priv( p_tree );
    vlc_mutex_lock( &p_priv->lock );
}

void media_tree_Unlock( media_tree_t *p_tree )
{
    media_tree_private_t *p_priv = mt_priv( p_tree );
    vlc_mutex_unlock( &p_priv->lock );
}

static inline void AssertBelong( media_tree_t *p_tree, media_node_t *p_node )
{
#ifndef NDEBUG
    for( media_node_t *p = p_node; p != &p_tree->p_root; p = p->p_parent )
        assert( p );
#endif
}

static media_node_t *AddChild( media_tree_t *p_tree, input_item_t *p_input, media_node_t *p_parent, int i_pos )
{
    media_node_t *p_node = malloc( sizeof( *p_node ) );
    if( unlikely( !p_node ) )
        return NULL;

    if( i_pos == -1 )
        i_pos = p_parent->children.i_size;

    p_node->p_input = p_input;
    p_node->p_parent = p_parent;
    ARRAY_INIT( p_node->children );
    ARRAY_INSERT( p_parent->children, p_node, i_pos );

    input_item_Hold( p_input );
    RegisterInputEvents( p_tree, p_input );

    return p_node;
}

static int FindNodeIndex( media_node_array_t *p_array, media_node_t *p_node )
{
    for( int i = 0; i < p_array->i_size; ++i )
    {
        media_node_t *p_cur = p_array->p_elems[i];
        if( p_cur == p_node )
            return i;
    }

    return -1;
}

static void NotifyChildren( media_tree_t *p_tree, const media_node_t *p_node, const media_tree_connection_t *p_conn )
{
    AssertLocked( p_tree );
    FOREACH_ARRAY( media_node_t *p_child, p_node->children )
        p_conn->cbs->pf_node_added( p_tree, p_child, p_conn->userdata );
        NotifyChildren( p_tree, p_child, p_conn );
    FOREACH_END()
}

void media_tree_subtree_added_default( media_tree_t *p_tree, const media_node_t *p_node, void *userdata )
{
    VLC_UNUSED( userdata );
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_connection_t *p_conn;
    vlc_list_foreach( p_conn, &p_priv->connections, siblings )
    {
        if( !p_conn->cbs->pf_node_added)
            break; /* nothing to do for this listener */
        /* notify "node added" for every node */
        NotifyChildren( p_tree, p_node, p_conn );
    }
}

void media_tree_connected_default( media_tree_t *p_tree, void *userdata )
{
    media_tree_subtree_added_default( p_tree, &p_tree->p_root, userdata );
}

media_tree_connection_t *media_tree_Connect( media_tree_t *p_tree, const media_tree_callbacks_t *p_callbacks, void *userdata )
{
    media_tree_connection_t *p_conn = malloc( sizeof( *p_conn ) );
    if( !p_conn )
        return NULL;
    p_conn->cbs = p_callbacks;
    p_conn->userdata = userdata;

    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_Lock( p_tree );
    vlc_list_append( &p_conn->siblings, &p_priv->connections );
    NotifyTreeConnected( p_tree );
    media_tree_Unlock( p_tree );

    return p_conn;
}

void media_tree_Disconnect( media_tree_t *p_tree, media_tree_connection_t *p_connection )
{
    media_tree_Lock( p_tree );
    vlc_list_remove( &p_connection->siblings );
    media_tree_Unlock( p_tree );

    free( p_connection );
}

media_node_t *media_tree_Add( media_tree_t *p_tree,
                              input_item_t *p_input,
                              media_node_t *p_parent,
                              int i_pos )
{
    AssertLocked( p_tree );

    media_node_t *p_node = AddChild( p_tree, p_input, p_parent, i_pos );
    if( unlikely( !p_node ) )
        return NULL;

    AssertBelong( p_tree, p_parent );

    NotifyNodeAdded( p_tree, p_node );

    return p_node;
}

media_node_t *media_tree_Find( media_tree_t *p_tree, input_item_t *p_input )
{
    AssertLocked( p_tree );

    /* quick & dirty depth-first O(n) implementation, with n the number of nodes in the tree */
    return FindNodeByInput( &p_tree->p_root, p_input );
}

void media_tree_Remove( media_tree_t *p_tree, media_node_t *p_node )
{
    AssertLocked( p_tree );
    AssertBelong( p_tree, p_node );

    media_node_t *p_parent = p_node->p_parent;
    assert( p_parent );
    int i_pos = FindNodeIndex( &p_parent->children, p_node );
    assert( i_pos >= 0 );
    ARRAY_REMOVE( p_parent->children, i_pos );

    NotifyNodeRemoved( p_tree, p_node );

    DestroyNodeAndChildren( p_tree, p_node );
}
