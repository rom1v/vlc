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
#include <vlc_media_tree.h>
#include <vlc_playlist.h>
#include <vlc_services_discovery.h>
#include "playlist_internal.h"
#include "media_source/media_source.h"

typedef struct {
    playlist_t *playlist;
    playlist_item_t *root;
    vlc_media_source_t *ms;
    vlc_media_tree_listener_id *listener;
    const char *name;
    struct vlc_list siblings;
} playlist_sd_entry_t;

static void media_tree_node_added(vlc_media_tree_t *tree,
                                  const input_item_node_t *parent,
                                  const input_item_node_t *node,
                                  void *userdata)
{
    assert(parent);
    playlist_sd_entry_t *p = userdata;

    playlist_Lock(p->playlist);

    playlist_item_t *parent_item;
    if (parent != &tree->root)
        parent_item = playlist_ItemGetByInput(p->playlist, parent->p_item);
    else
        parent_item = p->root;

    /* this is a hack, but this whole media_tree-to-playlist adapter is temporary */
    if (parent_item->i_children == -1)
        parent_item->i_children = 0; /* it's a node! */

    playlist_NodeAddInput(p->playlist, node->p_item, parent_item, PLAYLIST_END);

    playlist_Unlock(p->playlist);
}

static void media_tree_node_removed(vlc_media_tree_t *tree,
                                    const input_item_node_t *node_parent,
                                    const input_item_node_t *node,
                                    void *userdata)
{
    VLC_UNUSED(tree);
    VLC_UNUSED(node_parent);
    playlist_sd_entry_t *p = userdata;

    playlist_Lock(p->playlist);

    playlist_item_t *item = playlist_ItemGetByInput(p->playlist, node->p_item);
    if (unlikely(!item))
    {
        msg_Err(p->playlist, "removing item not added"); /* SD plugin bug */
        playlist_Unlock(p->playlist);
        return;
    }

#ifndef NDEBUG
    /* Check that the item belonged to the SD */
    for (playlist_item_t *pi = item->p_parent; pi != p->root; pi = pi->p_parent)
        assert(pi);
#endif

    playlist_item_t *parent = item->p_parent;
    /* if the item was added under a category and the category node
       becomes empty, delete that node as well */
    if (parent != p->root && parent->i_children == 1)
        item = parent;

    playlist_NodeDeleteExplicit(p->playlist, item, PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT);

    playlist_Unlock(p->playlist);
}

static const vlc_media_tree_callbacks_t media_tree_callbacks = {
    .listener_added = vlc_media_tree_listener_added_default,
    .subtree_added = NULL, /* already managed by the playlist */
    .node_added = media_tree_node_added,
    .node_removed = media_tree_node_removed,
};

int playlist_ServicesDiscoveryAdd(playlist_t *playlist, const char *name)
{
    playlist_sd_entry_t *p = malloc(sizeof(*p));
    if (unlikely(!p))
        return VLC_ENOMEM;

    p->playlist = playlist;

    p->name = strdup(name);
    if (unlikely(!p->name))
    {
        free(p);
        return VLC_ENOMEM;
    }

    vlc_media_source_provider_t *msp = pl_priv(playlist)->media_source_provider;
    vlc_media_source_t *ms = vlc_media_source_provider_GetMediaSource(msp, name);
    if (!ms)
    {
        free(p);
        return VLC_ENOMEM;
    }
    p->ms = ms;

    const char *description = ms->description ? ms->description : "?";

    playlist_Lock(playlist);
    p->root = playlist_NodeCreate(playlist, description, &playlist->root,
                                  PLAYLIST_END, PLAYLIST_RO_FLAG);
    playlist_Unlock(playlist);

    p->listener = vlc_media_tree_AddListener(ms->tree, &media_tree_callbacks, p);
    if (unlikely(!p->listener))
    {
        vlc_media_source_Release(p->ms);
        playlist_NodeDelete(playlist, p->root);
        free(p);
        return VLC_ENOMEM;
    }

    /* use the same big playlist lock for this temporary stuff */
    playlist_private_t *priv = pl_priv(playlist);
    playlist_Lock(playlist);
    vlc_list_append(&p->siblings, &priv->sd_entries);
    playlist_Unlock(playlist);

    return VLC_SUCCESS;
}

static playlist_sd_entry_t *RemoveEntry(playlist_t *playlist, const char *name)
{
    playlist_AssertLocked(playlist);
    playlist_private_t *priv = pl_priv(playlist);

    playlist_sd_entry_t *p_matching = NULL;
    playlist_sd_entry_t *p_entry;
    vlc_list_foreach(p_entry, &priv->sd_entries, siblings)
    {
        if (!strcmp(name, p_entry->name))
        {
            p_matching = p_entry;
            vlc_list_remove(&p_entry->siblings);
            break;
        }
    }

    return p_matching;
}

int playlist_ServicesDiscoveryRemove(playlist_t *playlist, const char *name)
{
    playlist_Lock(playlist);

    playlist_sd_entry_t *p = RemoveEntry(playlist, name);
    assert(p);

    playlist_NodeDeleteExplicit(playlist, p->root,
                                 PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT);

    playlist_Unlock(playlist);

    vlc_media_tree_RemoveListener(p->ms->tree, p->listener);
    vlc_media_source_Release(p->ms);

    free((void *) p->name);
    free(p);

    return VLC_SUCCESS;
}

bool playlist_IsServicesDiscoveryLoaded(playlist_t *playlist, const char *name)
{
    playlist_private_t *priv = pl_priv(playlist);
    return vlc_media_source_provider_IsServicesDiscoveryLoaded(priv->media_source_provider, name);
}

int playlist_ServicesDiscoveryControl(playlist_t *playlist, const char *name, int control, ...)
{
    playlist_private_t *priv = pl_priv(playlist);
    va_list args;
    va_start(args, control);
    int ret = vlc_media_source_provider_vaControl(priv->media_source_provider, name, control, args);
    va_end(args);
    return ret;
}

void playlist_ServicesDiscoveryKillAll(playlist_t *playlist)
{
    playlist_private_t *priv = pl_priv(playlist);

    playlist_Lock(playlist);

    playlist_sd_entry_t *p_entry;
    vlc_list_foreach(p_entry, &priv->sd_entries, siblings)
    {
        vlc_media_tree_RemoveListener(p_entry->ms->tree, p_entry->listener);
        vlc_media_source_Release(p_entry->ms);
        playlist_NodeDeleteExplicit(playlist, p_entry->root,
                                    PLAYLIST_DELETE_FORCE | PLAYLIST_DELETE_STOP_IF_CURRENT);
        free((void *) p_entry->name);
        free(p_entry);
    }
    vlc_list_init(&priv->sd_entries); /* reset */
    playlist_Unlock(playlist);
}
