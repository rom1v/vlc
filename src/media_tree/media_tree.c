/*****************************************************************************
 * media_tree.c : Media tree
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "media_tree.h"

#include <assert.h>
#include <vlc_common.h>
#include <vlc_arrays.h>
#include <vlc_atomic.h>
#include <vlc_input_item.h>
#include <vlc_threads.h>
#include "libvlc.h"

struct vlc_media_tree_listener_id
{
    const vlc_media_tree_callbacks_t *cbs;
    void *userdata;
    struct vlc_list node; /**< node of media_tree_private_t.listeners */
};

typedef struct
{
    vlc_media_tree_t public_data;

    struct vlc_list listeners; /**< list of vlc_media_tree_listener_id.node */
    vlc_mutex_t lock;
    vlc_atomic_rc_t rc;
} media_tree_private_t;

#define mt_priv(mt) container_of(mt, media_tree_private_t, public_data);

vlc_media_tree_t *vlc_media_tree_Create(void)
{
    media_tree_private_t *priv = malloc(sizeof(*priv));
    if (unlikely(!priv))
        return NULL;

    vlc_mutex_init(&priv->lock);
    vlc_atomic_rc_init(&priv->rc);
    vlc_list_init(&priv->listeners);

    vlc_media_tree_t *tree = &priv->public_data;
    input_item_node_t *root = &tree->root;
    root->p_item = NULL;
    TAB_INIT(root->i_children, root->pp_children);

    return tree;
}

static inline void AssertLocked(vlc_media_tree_t *tree)
{
    media_tree_private_t *priv = mt_priv(tree);
    vlc_assert_locked(&priv->lock);
}

static void NotifyListenerAdded(vlc_media_tree_t *tree)
{
    AssertLocked(tree);
    media_tree_private_t *priv = mt_priv(tree);

    vlc_media_tree_listener_id *listener;
    vlc_list_foreach(listener, &priv->listeners, node)
        if (listener->cbs->listener_added)
            listener->cbs->listener_added(tree, listener->userdata);
}

static void NotifyNodeAdded(vlc_media_tree_t *tree, const input_item_node_t *parent,
                            const input_item_node_t *node)
{
    AssertLocked(tree);
    media_tree_private_t *priv = mt_priv(tree);

    vlc_media_tree_listener_id *listener;
    vlc_list_foreach(listener, &priv->listeners, node)
        if (listener->cbs->node_added)
            listener->cbs->node_added(tree, parent, node, listener->userdata);
}

static void NotifyNodeRemoved(vlc_media_tree_t *tree, const input_item_node_t *parent,
                              const input_item_node_t *node)
{
    AssertLocked(tree);
    media_tree_private_t *priv = mt_priv(tree);

    vlc_media_tree_listener_id *listener;
    vlc_list_foreach(listener, &priv->listeners, node)
        if (listener->cbs->node_removed)
            listener->cbs->node_removed(tree, parent, node, listener->userdata);
}

static bool FindNodeByInput(input_item_node_t *parent, const input_item_t *input,
                            input_item_node_t **result, input_item_node_t **result_parent)
{
    for (int i = 0; i < parent->i_children; ++i)
    {
        input_item_node_t *child = parent->pp_children[i];
        if (child->p_item == input)
        {
            *result = child;
            if (result_parent)
                *result_parent = parent;
            return true;
        }

        if (FindNodeByInput(child, input, result, result_parent))
            return true;
    }

    return false;
}

static void DestroyRootNode(vlc_media_tree_t *tree)
{
    input_item_node_t *root = &tree->root;
    for (int i = 0; i < root->i_children; ++i)
        input_item_node_Delete(root->pp_children[i]);

    free(root->pp_children);
}

static void Destroy(vlc_media_tree_t *tree)
{
    media_tree_private_t *priv = mt_priv(tree);
    vlc_media_tree_listener_id *listener;
    vlc_list_foreach(listener, &priv->listeners, node)
        free(listener);
    vlc_list_init(&priv->listeners); /* reset */
    DestroyRootNode(tree);
    vlc_mutex_destroy(&priv->lock);
    free(tree);
}

void vlc_media_tree_Hold(vlc_media_tree_t *tree)
{
    media_tree_private_t *priv = mt_priv(tree);
    vlc_atomic_rc_inc(&priv->rc);
}

void vlc_media_tree_Release(vlc_media_tree_t *tree)
{
    media_tree_private_t *priv = mt_priv(tree);
    if (vlc_atomic_rc_dec(&priv->rc))
        Destroy(tree);
}

void vlc_media_tree_Lock(vlc_media_tree_t *tree)
{
    media_tree_private_t *priv = mt_priv(tree);
    vlc_mutex_lock(&priv->lock);
}

void vlc_media_tree_Unlock(vlc_media_tree_t *tree)
{
    media_tree_private_t *priv = mt_priv(tree);
    vlc_mutex_unlock(&priv->lock);
}

static input_item_node_t *AddChild(input_item_node_t *parent, input_item_t *input)
{
    input_item_node_t *node = input_item_node_Create(input);
    if (unlikely(!node))
        return NULL;

    input_item_node_AppendNode(parent, node);

    return node;
}

static void NotifyChildren(vlc_media_tree_t *tree, const input_item_node_t *node,
                           vlc_media_tree_listener_id *listener)
{
    AssertLocked(tree);
    for (int i = 0; i < node->i_children; ++i)
    {
        input_item_node_t *child = node->pp_children[i];
        listener->cbs->node_added(tree, node, child, listener->userdata);
        NotifyChildren(tree, child, listener);
    }
}

void vlc_media_tree_listener_added_default(vlc_media_tree_t *tree, void *userdata)
{
    VLC_UNUSED(userdata);
    AssertLocked(tree);
    media_tree_private_t *priv = mt_priv(tree);
    vlc_media_tree_listener_id *listener;
    vlc_list_foreach(listener, &priv->listeners, node)
        /* notify "node added" for every node */
        if (listener->cbs->node_added)
            NotifyChildren(tree, &tree->root, listener);
}

vlc_media_tree_listener_id *vlc_media_tree_AddListener(vlc_media_tree_t *tree,
                                                       const vlc_media_tree_callbacks_t *cbs,
                                                       void *userdata)
{
    vlc_media_tree_listener_id *listener = malloc(sizeof(*listener));
    if (unlikely(!listener))
        return NULL;
    listener->cbs = cbs;
    listener->userdata = userdata;

    media_tree_private_t *priv = mt_priv(tree);
    vlc_media_tree_Lock(tree);
    vlc_list_append(&listener->node, &priv->listeners);
    NotifyListenerAdded(tree);
    vlc_media_tree_Unlock(tree);
    return listener;
}

void vlc_media_tree_RemoveListener(vlc_media_tree_t *tree, vlc_media_tree_listener_id *listener)
{
    vlc_media_tree_Lock(tree);
    vlc_list_remove(&listener->node);
    vlc_media_tree_Unlock(tree);

    free(listener);
}

input_item_node_t *vlc_media_tree_Add(vlc_media_tree_t *tree, input_item_node_t *parent, input_item_t *input)
{
    AssertLocked(tree);

    input_item_node_t *node = AddChild(parent, input);
    if (unlikely(!node))
        return NULL;

    NotifyNodeAdded(tree, parent, node);

    return node;
}

bool vlc_media_tree_Find(vlc_media_tree_t *tree, const input_item_t *input,
                         input_item_node_t **result, input_item_node_t **result_parent)
{
    AssertLocked(tree);

    /* quick & dirty depth-first O(n) implementation, with n the number of nodes in the tree */
    return FindNodeByInput(&tree->root, input, result, result_parent);
}

bool vlc_media_tree_Remove(vlc_media_tree_t *tree, input_item_t *input)
{
    AssertLocked(tree);

    input_item_node_t *node;
    input_item_node_t *parent;
    if (!FindNodeByInput(&tree->root, input, &node, &parent))
        return false;

    input_item_node_RemoveNode(parent, node);
    NotifyNodeRemoved(tree, parent, node);
    input_item_node_Delete(node);
    return true;
}
