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
#include <vlc_input_item.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup media_tree Media tree
 * \ingroup input
 * @{
 */

/**
 * Media tree.
 *
 * Nodes must be traversed with locked held (vlc_media_tree_Lock()).
 */
typedef struct vlc_media_tree_t {
    input_item_node_t root;
} vlc_media_tree_t;

/**
 * Callbacks to receive media tree events.
 */
typedef struct vlc_media_tree_callbacks_t
{
    /**
     * Called when this listener is added by vlc_media_tree_AddListener(), with
     * lock held.
     *
     * This allows to get the tree initial state.
     *
     * Use vlc_media_tree_listener_added_default implementation to call
     * node_added() for every node.
     */
    void (*listener_added)(vlc_media_tree_t *, void *userdata);

    /**
     * Called when an input item notifies that a subtree has been added.
     *
     * Use vlc_media_tree_subtree_added_default implementation to call
     * node_added() for every new node.
     */
    void (*subtree_added)(vlc_media_tree_t *, const input_item_node_t *, void *userdata);

    /**
     * Called after a new node has been added to the media tree, with lock held.
     */
    void (*node_added)(vlc_media_tree_t *, const input_item_node_t *parent,
                       const input_item_node_t *, void *userdata);

    /**
     * Called after a node has been removed from the media tree, with lock held.
     */
    void (*node_removed)(vlc_media_tree_t *, const input_item_node_t *parent,
                         const input_item_node_t *, void *userdata);
} vlc_media_tree_callbacks_t;

/**
 * Listener for media tree events.
 */
typedef struct vlc_media_tree_listener_id vlc_media_tree_listener_id;

/**
 * Default implementation for listener_added(), which calls node_added() for
 * every existing node.
 **/
VLC_API void vlc_media_tree_listener_added_default(vlc_media_tree_t *, void *userdata);

/**
 * Default implementation for subtree_added(), which calls node_added()
 * for every new node.
 **/
VLC_API void vlc_media_tree_subtree_added_default(vlc_media_tree_t *, const input_item_node_t *, void *userdata);

/**
 * Add listener. The lock must NOT be held.
 */
VLC_API vlc_media_tree_listener_id *vlc_media_tree_AddListener(vlc_media_tree_t *,
                                                               const vlc_media_tree_callbacks_t *,
                                                               void *userdata);

/**
 * Remove listener. The lock must NOT be held.
 */
VLC_API void vlc_media_tree_RemoveListener(vlc_media_tree_t *, vlc_media_tree_listener_id *);

/**
 * Lock the media tree (non-recursive).
 */
VLC_API void vlc_media_tree_Lock(vlc_media_tree_t *);

/**
 * Unlock the media tree.
 */
VLC_API void vlc_media_tree_Unlock(vlc_media_tree_t *);

/**
 * Find the node containing the requested input item (and its parent).
 *
 * \param result point to the matching node if the function returns true [OUT]
 * \param result_parent if not NULL, point to the matching node parent
 *                      if the function returns true [OUT]
 *
 * \retval true if item was found
 * \retval false if item was not found
 */
VLC_API bool vlc_media_tree_Find(vlc_media_tree_t *, const input_item_t *,
                                 input_item_node_t **result, input_item_node_t **result_parent);

VLC_API void vlc_media_tree_Preparse(vlc_media_tree_t *, libvlc_int_t *libvlc,
                                     input_item_t *);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
