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

#include <stdatomic.h>
#include <vlc_common.h>
#include <vlc_arrays.h>
#include <vlc_input_item.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_TREE_END (-1)

typedef struct media_node_t media_node_t;
typedef struct media_tree_t media_tree_t;
TYPEDEF_ARRAY( media_node_t *, media_node_array_t )

struct media_node_t
{
    input_item_t *p_input;
    media_node_t *p_parent;
    media_node_array_t children;
};

struct media_tree_t {
    struct vlc_common_members obj;
    media_node_t p_root;
};

typedef struct media_tree_callbacks_t
{
    void *userdata;
    void ( *pf_tree_attached )( media_tree_t *, void *userdata );
    void ( *pf_node_added )( media_tree_t *, media_node_t *, void *userdata );
    void ( *pf_node_removed )( media_tree_t *, media_node_t *, void *userdata );
} media_tree_callbacks_t;

/* default pf_tree_attached callback calling pf_node_added for every node */
VLC_API void media_tree_attached_default( media_tree_t *, void *userdata );

VLC_API media_tree_t *media_tree_Create( vlc_object_t *p_parent );

VLC_API void media_tree_Hold( media_tree_t * );
VLC_API void media_tree_Release( media_tree_t * );

VLC_API void media_tree_Lock( media_tree_t * );
VLC_API void media_tree_Unlock( media_tree_t * );

VLC_API void media_tree_Attach( media_tree_t *, const media_tree_callbacks_t * );
VLC_API void media_tree_Detach( media_tree_t * );

VLC_API media_node_t *media_tree_Add( media_tree_t *, input_item_t *, media_node_t *p_parent, int i_pos );
VLC_API media_node_t *media_tree_Find( media_tree_t *, input_item_t * );
VLC_API media_node_t *media_tree_Remove( media_tree_t *, media_node_t * );

#ifdef __cplusplus
}
#endif

#endif
