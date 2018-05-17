/*****************************************************************************
 * vlc_media_source.h : Media source
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

#ifndef VLC_MEDIA_SOURCE_H
#define VLC_MEDIA_SOURCE_H

#include <vlc_common.h>
#include <vlc_input_item.h>

typedef struct vlc_media_tree_t vlc_media_tree_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup media_source Media source
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
    void
    (*on_children_reset)(vlc_media_tree_t *tree, input_item_node_t *node,
                         void *userdata);

    /**
     * Called when a media notifies that a subtree has been added, with lock
     * held.
     *
     * Use vlc_media_tree_subtree_added_default implementation to call
     * node_added() for every new node.
     */
    void
    (*on_children_added)(vlc_media_tree_t *tree, input_item_node_t *node,
                         input_item_node_t *const children[], size_t count,
                         void *userdata);

    void
    (*on_children_removed)(vlc_media_tree_t *tree, input_item_node_t *node,
                           input_item_node_t *const children[], size_t count,
                           void *userdata);
} vlc_media_tree_callbacks_t;

/**
 * Listener for media tree events.
 */
typedef struct vlc_media_tree_listener_id vlc_media_tree_listener_id;

/**
 * Default implementation for listener_added(), which calls node_added() for
 * every existing node.
 **/
//VLC_API void
//vlc_media_tree_listener_added_default(vlc_media_tree_t *, void *userdata);

/**
 * Default implementation for subtree_added(), which calls node_added()
 * for every new node.
 **/
//VLC_API void
//vlc_media_tree_subtree_added_default(vlc_media_tree_t *,
//                                     const input_item_node_t *, void *userdata);

/**
 * Add a listener. The lock must NOT be held.
 *
 * \param tree                 the media tree, unlocked
 * \param cbs                  the callbacks (must be valid until the listener
 *                             is removed)
 * \param userdata             userdata provided as a parameter in callbacks
 * \param notify_current_state true to notify the current state immediately via
 *                             callbacks
 */
VLC_API vlc_media_tree_listener_id *
vlc_media_tree_AddListener(vlc_media_tree_t *tree,
                           const vlc_media_tree_callbacks_t *cbs,
                           void *userdata, bool notify_current_state);

/**
 * Remove a listener. The lock must NOT be held.
 *
 * \param tree     the media tree, unlocked
 * \param listener the listener identifier returned by
 *                 vlc_media_tree_AddListener()
 */
VLC_API void
vlc_media_tree_RemoveListener(vlc_media_tree_t *tree,
                              vlc_media_tree_listener_id *listener);

/**
 * Lock the media tree (non-recursive).
 */
VLC_API void
vlc_media_tree_Lock(vlc_media_tree_t *);

/**
 * Unlock the media tree.
 */
VLC_API void
vlc_media_tree_Unlock(vlc_media_tree_t *);

/**
 * Find the node containing the requested input item (and its parent).
 *
 * \param tree the media tree, locked
 * \param result point to the matching node if the function returns true [OUT]
 * \param result_parent if not NULL, point to the matching node parent
 *                      if the function returns true [OUT]
 *
 * \retval true if item was found
 * \retval false if item was not found
 */
VLC_API bool
vlc_media_tree_Find(vlc_media_tree_t *tree, const input_item_t *media,
                    input_item_node_t **result,
                    input_item_node_t **result_parent);

/**
 * Preparse a media, and expand it in the media tree on subitems added.
 *
 * \param media_tree the media tree (not necessarily locked)
 * \param libvlc the libvlc instance
 * \param media the media to preparse
 */
VLC_API void
vlc_media_tree_Preparse(vlc_media_tree_t *media_tree, libvlc_int_t *libvlc,
                        input_item_t *media);

/**
 * Media source.
 *
 * A media source is associated to a "service discovery". It stores the
 * detected media in a media tree.
 */
typedef struct vlc_media_source_t
{
    vlc_media_tree_t *tree;
    const char *description;
} vlc_media_source_t;

/**
 * Increase the media source reference count.
 */
VLC_API void
vlc_media_source_Hold(vlc_media_source_t *);

/**
 * Decrease the media source reference count.
 *
 * Destroy the media source and close the associated "service discovery" if it
 * reaches 0.
 */
VLC_API void
vlc_media_source_Release(vlc_media_source_t *);

/**
 * Media source provider (opaque pointer), used to get media sources.
 */
typedef struct vlc_media_source_provider_t vlc_media_source_provider_t;

/**
 * Return the media source provider associated to the libvlc instance.
 */
VLC_API vlc_media_source_provider_t *
vlc_media_source_provider_Get(libvlc_int_t *);

/**
 * Return the media source identified by psz_name.
 *
 * The resulting media source must be released by vlc_media_source_Release().
 */
VLC_API vlc_media_source_t *
vlc_media_source_provider_GetMediaSource(vlc_media_source_provider_t *,
                                         const char *name);

/** @} */

#ifdef __cplusplus
}
#endif

#endif

