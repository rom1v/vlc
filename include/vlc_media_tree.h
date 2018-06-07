/*****************************************************************************
 * vlc_media_tree.h : Media tree
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

#ifndef VLC_MEDIA_TREE_H
#define VLC_MEDIA_TREE_H

#include <vlc_common.h>
#include <vlc_arrays.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_TREE_END (-1)

typedef struct media_node_t media_node_t;
typedef struct media_tree_t media_tree_t;
TYPEDEF_ARRAY( media_node_t *, media_node_array_t )

/**
 * Node of media tree.
 */
struct media_node_t
{
    input_item_t *p_input;
    media_node_t *p_parent;
    media_node_array_t children;
};

/**
 * Media tree.
 *
 * Nodes must be traversed with locked held (media_tree_Lock()).
 */
struct media_tree_t {
    struct vlc_common_members obj;
    media_node_t p_root;
};

/**
 * Opaque type to identify a "listener" connection.
 */
typedef struct media_tree_connection_t media_tree_connection_t;

/**
 * Callbacks to listen to media tree events.
 */
typedef struct media_tree_callbacks_t
{
    /**
     * Called on media_tree_Connect(), with lock held.
     *
     * Use media_tree_connect_default implementation to call pf_node_added()
     * for every node.
     */
    void ( *pf_tree_connected )( media_tree_t *, void *userdata );

    /**
     * Called when an input item notifies that a subtree has been added.
     *
     * Use media_tree_subtree_added_default implementation to call
     * pf_node_added() for every new node.
     */
    void ( *pf_subtree_added )( media_tree_t *, const media_node_t *, void *userdata );

    /**
     * Called when a new node is added to the media tree, with lock held.
     */
    void ( *pf_node_added )( media_tree_t *, const media_node_t *, void *userdata );

    /**
     * Called when a node is removed from the media tree, with lock held.
     */
    void ( *pf_node_removed )( media_tree_t *, const media_node_t *, void *userdata );

    /**
     * Called when an input item is updated.
     */
    void ( *pf_input_updated )( media_tree_t *, const media_node_t *, void *userdata );
} media_tree_callbacks_t;

/**
 * Default implementation for pf_tree_connected(), which calls pf_node_added()
 * for every existing node.
 **/
VLC_API void media_tree_connected_default( media_tree_t *, void *userdata );

/**
 * Default implementation for pf_subtree_added(), which calls pf_node_added()
 * for every new node.
 **/
VLC_API void media_tree_subtree_added_default( media_tree_t *, const media_node_t *, void *userdata );

/**
 * Increase the media tree reference count.
 */
VLC_API void media_tree_Hold( media_tree_t * );

/**
 * Decrease the media tree reference count.
 *
 * Destroy the media tree if it reaches 0.
 */
VLC_API void media_tree_Release( media_tree_t * );

/**
 * Connect callbacks. The lock must NOT be held.
 *
 * \return a connection to be used in media_tree_Disconnect()
 */
VLC_API media_tree_connection_t *media_tree_Connect( media_tree_t *, const media_tree_callbacks_t *, void *userdata );

/**
 * Disconnect callbacks. The lock must NOT be held.
 */
VLC_API void media_tree_Disconnect( media_tree_t *, media_tree_connection_t * );

/**
 * Lock the media tree (non-recursive).
 */
VLC_API void media_tree_Lock( media_tree_t * );

/**
 * Unlock the media tree.
 */
VLC_API void media_tree_Unlock( media_tree_t * );

/**
 * Find the media node containing the requested input item.
 *
 * \return the matching media node, or NULL if not found
 */
VLC_API media_node_t *media_tree_Find( media_tree_t *, input_item_t * );

#ifdef __cplusplus
}
#endif

#endif
