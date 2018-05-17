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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "media_source.h"

#include <assert.h>
#include <vlc_atomic.h>
#include <vlc_playlist.h>
#include <vlc_services_discovery.h>
#include "libvlc.h"
#include "media_tree.h"

typedef struct
{
    vlc_media_source_t public_data;

    services_discovery_t *sd;
    vlc_atomic_rc_t rc;
    vlc_media_source_provider_t *owner;
    struct vlc_list node;
    char name[];
} media_source_private_t;

#define ms_priv(ms) container_of(ms, media_source_private_t, public_data)

struct vlc_media_source_provider_t
{
    struct vlc_common_members obj;
    vlc_mutex_t lock;
    struct vlc_list media_sources;
};

/* A new item has been added to a certain services discovery */
static void
services_discovery_item_added(services_discovery_t *sd,
                              input_item_t *parent, input_item_t *media,
                              const char *cat)
{
    assert(!parent || !cat);
    VLC_UNUSED(cat);

    vlc_media_source_t *ms = sd->owner.sys;
    vlc_media_tree_t *tree = ms->tree;

    msg_Dbg(sd, "adding: %s", media->psz_name ? media->psz_name : "(null)");

    vlc_media_tree_Lock(tree);

    input_item_node_t *parent_node;
    if (parent)
        vlc_media_tree_Find(tree, parent, &parent_node, NULL);
    else
        parent_node = &tree->root;

    bool added = vlc_media_tree_Add(tree, parent_node, media) != NULL;
    if (unlikely(!added))
        msg_Err(sd, "could not allocate media tree node");

    vlc_media_tree_Unlock(tree);
}

static void
services_discovery_item_removed(services_discovery_t *sd, input_item_t *media)
{
    vlc_media_source_t *ms = sd->owner.sys;
    vlc_media_tree_t *tree = ms->tree;

    msg_Dbg(sd, "removing: %s", media->psz_name ? media->psz_name : "(null)");

    vlc_media_tree_Lock(tree);
    bool removed = vlc_media_tree_Remove(tree, media);
    vlc_media_tree_Unlock(tree);

    if (unlikely(!removed))
    {
        msg_Err(sd, "removing item not added"); /* SD plugin bug */
        return;
    }
}

static const struct services_discovery_callbacks sd_cbs = {
    .item_added = services_discovery_item_added,
    .item_removed = services_discovery_item_removed,
};

static vlc_media_source_t *
MediaSourceNew(vlc_media_source_provider_t *provider, const char *name)
{
    media_source_private_t *priv = malloc(sizeof(*priv) + strlen(name) + 1);
    if (unlikely(!priv))
        return NULL;

    vlc_atomic_rc_init(&priv->rc);

    vlc_media_source_t *ms = &priv->public_data;

    /* vlc_sd_Create() may call services_discovery_item_added(), which will read its
     * tree, so it must be initialized first */
    ms->tree = vlc_media_tree_Create();
    if (unlikely(!ms->tree))
    {
        free(priv);
        return NULL;
    }

    strcpy(priv->name, name);

    struct services_discovery_owner_t owner = {
        .cbs = &sd_cbs,
        .sys = ms,
    };

    priv->sd = vlc_sd_Create(provider, name, &owner);
    if (unlikely(!priv->sd))
    {
        vlc_media_tree_Release(ms->tree);
        free(priv);
        return NULL;
    }

    /* sd->description is set during vlc_sd_Create() */
    ms->description = priv->sd->description;

    priv->owner = provider;

    return ms;
}

static void
Remove(vlc_media_source_provider_t *provider, vlc_media_source_t *ms)
{
    vlc_mutex_lock(&provider->lock);
    vlc_list_remove(&ms_priv(ms)->node);
    vlc_mutex_unlock(&provider->lock);
}

static void
MediaSourceDestroy(vlc_media_source_t *ms)
{
    media_source_private_t *priv = ms_priv(ms);
    Remove(priv->owner, ms);
    vlc_sd_Destroy(priv->sd);
    vlc_media_tree_Release(ms->tree);
    free(priv);
}

void
vlc_media_source_Hold(vlc_media_source_t *ms)
{
    media_source_private_t *priv = ms_priv(ms);
    vlc_atomic_rc_inc(&priv->rc);
}

void
vlc_media_source_Release(vlc_media_source_t *ms)
{
    media_source_private_t *priv = ms_priv(ms);
    if (vlc_atomic_rc_dec(&priv->rc))
        MediaSourceDestroy(ms);
}

static vlc_media_source_t *
FindByName(vlc_media_source_provider_t *provider, const char *name)
{
    vlc_mutex_assert(&provider->lock);
    media_source_private_t *entry;
    vlc_list_foreach(entry, &provider->media_sources, node)
        if (!strcmp(name, entry->name))
            return &entry->public_data;
    return NULL;
}

vlc_media_source_provider_t *
vlc_media_source_provider_Get(libvlc_int_t *libvlc)
{
    return libvlc_priv(libvlc)->media_source_provider;
}

#undef vlc_media_source_provider_Create
vlc_media_source_provider_t *
vlc_media_source_provider_Create(vlc_object_t *parent)
{
    vlc_media_source_provider_t *provider =
            vlc_custom_create(parent, sizeof(*provider),
                              "media-source-provider");
    if (unlikely(!provider))
        return NULL;

    vlc_mutex_init(&provider->lock);
    vlc_list_init(&provider->media_sources);
    return provider;
}

void
vlc_media_source_provider_Destroy(vlc_media_source_provider_t *provider)
{
    vlc_mutex_destroy(&provider->lock);
    vlc_object_release(provider);
}

static vlc_media_source_t *
AddServiceDiscovery(vlc_media_source_provider_t *provider, const char *name)
{
    vlc_mutex_assert(&provider->lock);

    vlc_media_source_t *ms = MediaSourceNew(provider, name);
    if (unlikely(!ms))
        return NULL;

    vlc_list_append(&ms_priv(ms)->node, &provider->media_sources);
    return ms;
}

vlc_media_source_t *
vlc_media_source_provider_GetMediaSource(vlc_media_source_provider_t *provider,
                                         const char *name)
{
    vlc_mutex_lock(&provider->lock);
    vlc_media_source_t *ms = FindByName(provider, name);
    if (!ms)
        ms = AddServiceDiscovery(provider, name);
    vlc_mutex_unlock(&provider->lock);

    return ms;
}
