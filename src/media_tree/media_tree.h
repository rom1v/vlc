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

#ifndef _MEDIA_TREE_H
#define _MEDIA_TREE_H

#include <vlc_media_tree.h>

media_tree_t *media_tree_Create( vlc_object_t *p_parent );
media_node_t *media_tree_Add( media_tree_t *, input_item_t *, media_node_t *p_parent, int i_pos );
void media_tree_Remove( media_tree_t *, media_node_t * );

#endif
