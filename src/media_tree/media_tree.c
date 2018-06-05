#include <assert.h>
#include <vlc_common.h>
#include <vlc_media_tree.h>
#include <vlc_threads.h>
#include "libvlc.h"

struct media_tree_connection_t
{
    media_tree_listener_t listener;
};

TYPEDEF_ARRAY( media_tree_connection_t *, media_tree_connection_array_t )

typedef struct
{
    media_tree_t public_data;

    media_tree_connection_array_t connections;
    vlc_mutex_t lock;
    atomic_uint refs;
} media_tree_private_t;

#define mt_priv( mt ) container_of( mt, media_tree_private_t, public_data );

media_tree_t *media_tree_Create( void )
{
    media_tree_private_t *p_priv = malloc( sizeof ( *p_priv ) );
    if( unlikely( !p_priv ) )
        return NULL;

    vlc_mutex_init( &p_priv->lock );
    atomic_init( &p_priv->refs, 1 );
    ARRAY_INIT( p_priv->connections );

    media_tree_t *p_tree = &p_priv->public_data;
    media_node_t *p_root = &p_tree->p_root;
    p_root->p_input = NULL;
    p_root->p_parent = NULL;
    ARRAY_INIT( p_root->children );

    return p_tree;
}

static void DestroyNodeAndChildren( media_node_t * );

static void DestroyChildren( media_node_t *p_node )
{
    FOREACH_ARRAY( media_node_t *p_child, p_node->children )
        DestroyNodeAndChildren( p_child );
    FOREACH_END()
}

static void DestroyNodeAndChildren( media_node_t *p_node )
{
    DestroyChildren( p_node );
    input_item_Release( p_node->p_input );
    free( p_node );
}

static void Destroy( media_tree_t *p_tree )
{
    media_tree_private_t *p_priv = mt_priv( p_tree );
    FOREACH_ARRAY( media_tree_connection_t *p_conn, p_priv->connections )
        free( p_conn );
    FOREACH_END()
    ARRAY_RESET( p_priv->connections);
    DestroyChildren( &p_tree->p_root );
    vlc_mutex_destroy( &p_priv->lock );
    free( p_priv );
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

static inline void AssertLocked( media_tree_t *p_tree )
{
    media_tree_private_t *p_priv = mt_priv( p_tree );
    vlc_assert_locked( &p_priv->lock );
}

static inline void AssertBelong( media_tree_t *p_tree, media_node_t *p_node )
{
#ifndef NDEBUG
    for( media_node_t *p = p_node; p != &p_tree->p_root; p = p->p_parent )
        assert( p );
#endif
}

static media_node_t *AddChild( input_item_t *p_input, media_node_t *p_parent, int i_pos )
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

static void NotifyChildren( media_tree_t *p_tree, media_node_t *p_node, const media_tree_listener_t *p_listener )
{
    AssertLocked( p_tree );
    FOREACH_ARRAY( media_node_t *p_child, p_node->children )
        p_listener->pf_node_added( p_tree, p_child, p_listener->userdata );
        NotifyChildren( p_tree, p_child, p_listener );
    FOREACH_END()
}

void media_tree_connected_default( media_tree_t *p_tree, void *userdata )
{
    VLC_UNUSED( userdata );
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    FOREACH_ARRAY( media_tree_connection_t *p_conn, p_priv->connections )
        if( !p_conn->listener.pf_node_added)
            break; /* nothing to do for this listener */
        /* notify "node added" for every node */
        NotifyChildren( p_tree, &p_tree->p_root, &p_conn->listener );
    FOREACH_END()
}

static void NotifyTreeConnected( media_tree_t *p_tree )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    FOREACH_ARRAY( media_tree_connection_t *p_conn, p_priv->connections )
        if( p_conn->listener.pf_tree_connected )
            p_conn->listener.pf_tree_connected( p_tree, p_conn->listener.userdata );
    FOREACH_END()
}

static void NotifyNodeAdded( media_tree_t *p_tree, media_node_t *p_node )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    FOREACH_ARRAY( media_tree_connection_t *p_conn, p_priv->connections )
        if( p_conn->listener.pf_node_added )
            p_conn->listener.pf_node_added( p_tree, p_node, p_conn->listener.userdata );
    FOREACH_END()
}

static void NotifyNodeRemoved( media_tree_t *p_tree, media_node_t *p_node )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    FOREACH_ARRAY( media_tree_connection_t *p_conn, p_priv->connections )
        if( p_conn->listener.pf_node_removed )
            p_conn->listener.pf_node_removed( p_tree, p_node, p_conn->listener.userdata );
    FOREACH_END()
}

media_tree_connection_t *media_tree_Connect( media_tree_t *p_tree, const media_tree_listener_t *p_listener )
{
    media_tree_connection_t *p_conn = malloc( sizeof( *p_conn ) );
    if( !p_conn )
        return NULL;
    p_conn->listener = *p_listener;

    media_tree_private_t *p_priv = mt_priv( p_tree );
    media_tree_Lock( p_tree );
    ARRAY_APPEND( p_priv->connections, p_conn );
    NotifyTreeConnected( p_tree );
    media_tree_Unlock( p_tree );

    return p_conn;
}

static bool RemoveConnection( media_tree_t *p_tree, const media_tree_connection_t *p_connection )
{
    AssertLocked( p_tree );
    media_tree_private_t *p_priv = mt_priv( p_tree );
    for( int i = 0; i < p_priv->connections.i_size; ++i)
    {
        media_tree_connection_t *p_conn = p_priv->connections.p_elems[i];
        if( p_conn == p_connection )
        {
            ARRAY_REMOVE( p_priv->connections, i );
            return true;
        }
    }
    return false;
}

void media_tree_Disconnect( media_tree_t *p_tree, media_tree_connection_t *p_connection )
{
    media_tree_Lock( p_tree );
    bool found = RemoveConnection( p_tree, p_connection );
#ifdef NDEBUG
    VLC_UNUSED( found );
#else
    assert( found );
#endif
    media_tree_Unlock( p_tree );

    free( p_connection );
}

media_node_t *media_tree_AddByInput( media_tree_t *p_tree,
                                     input_item_t *p_input,
                                     media_node_t *p_parent,
                                     int i_pos )
{
    AssertLocked( p_tree );

    media_node_t *p_node = AddChild( p_input, p_parent, i_pos );
    if( unlikely( !p_node ) )
        return NULL;

    AssertBelong( p_tree, p_parent );

    NotifyNodeAdded( p_tree, p_node );

    return p_node;
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

media_node_t *media_tree_FindByInput( media_tree_t *p_tree, input_item_t *p_input )
{
    AssertLocked( p_tree );

    /* quick & dirty depth-first O(n) implementation, with n the number of nodes in the tree */
    return FindNodeByInput( &p_tree->p_root, p_input );
}

media_node_t *media_tree_Remove( media_tree_t *p_tree, media_node_t *p_node )
{
    AssertLocked( p_tree );
    AssertBelong( p_tree, p_node );

    media_node_t *p_parent = p_node->p_parent;
    if( p_parent )
    {
        int i_pos = FindNodeIndex( &p_parent->children, p_node );
        assert( i_pos >= 0 );
        ARRAY_REMOVE( p_parent->children, i_pos );
    }

    NotifyNodeRemoved( p_tree, p_node );

    DestroyNodeAndChildren( p_node );

    return NULL;
}
