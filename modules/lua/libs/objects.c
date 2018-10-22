/*****************************************************************************
 * objects.c: Generic lua<->vlc object wrapper
 *****************************************************************************
 * Copyright (C) 2007-2008 the VideoLAN team
 *
 * Authors: Antoine Cellerier <dionoea at videolan tod org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifndef  _GNU_SOURCE
#   define  _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_vout.h>
#include <vlc_playlist.h>
#include <vlc_player.h>

#include "../vlc.h"
#include "../libs.h"
#include "objects.h"
#include "input.h"

/*****************************************************************************
 * Generic vlc_object_t wrapper creation
 *****************************************************************************/
static int vlclua_object_release( lua_State *L )
{
    vlc_object_t **p_obj = (vlc_object_t **)luaL_checkudata( L, 1, "vlc_object" );
    lua_pop( L, 1 );
    vlc_object_release( *p_obj );
    return 0;
}

static int vlclua_object_find( lua_State *L )
{
    lua_pushnil( L );
    return 1;
}

static int vlclua_get_libvlc( lua_State *L )
{
    libvlc_int_t *p_libvlc = vlc_object_instance(vlclua_get_this( L ));
    vlc_object_hold( p_libvlc );
    vlclua_push_vlc_object( L, p_libvlc );
    return 1;
}

static int vlclua_get_playlist( lua_State *L )
{
    vlc_playlist_t *playlist = vlclua_get_playlist_internal(L);
    if (playlist)
        lua_pushlightuserdata(L, playlist);
    else
        lua_pushnil(L);
    return 1;
}

static int vlclua_get_player( lua_State *L )
{
    vlc_player_t *player = vlclua_get_player_internal(L);
    if (player)
        lua_pushlightuserdata(L, player);
    else
        lua_pushnil(L);
    return 1;
}

#undef vlclua_push_vlc_object
int vlclua_push_vlc_object( lua_State *L, vlc_object_t *p_obj )
{
    vlc_object_t **udata = (vlc_object_t **)
        lua_newuserdata( L, sizeof( vlc_object_t * ) );
    *udata = p_obj;

    if( luaL_newmetatable( L, "vlc_object" ) )
    {
        /* Hide the metatable */
        lua_pushliteral( L, "none of your business" );
        lua_setfield( L, -2, "__metatable" );
        /* Set the garbage collector if needed */
        lua_pushcfunction( L, vlclua_object_release );
        lua_setfield( L, -2, "__gc" );
    }
    lua_setmetatable( L, -2 );
    return 1;
}

static int vlclua_get_vout( lua_State *L )
{
    vout_thread_t *vout = vlclua_get_vout_internal(L);
    if (vout)
        vlclua_push_vlc_object(L, (vlc_object_t *) vout);
    else
        lua_pushnil(L);
    return 1;
}

static int vlclua_get_aout( lua_State *L )
{
    audio_output_t *aout = vlclua_get_aout_internal(L);
    if (aout)
        vlclua_push_vlc_object(L, (vlc_object_t *) aout);
    else
        lua_pushnil(L);
    return 1;
}

/*****************************************************************************
 *
 *****************************************************************************/
static const luaL_Reg vlclua_object_reg[] = {
    { "playlist", vlclua_get_playlist },
    { "player", vlclua_get_player },
    { "libvlc", vlclua_get_libvlc },
    { "find", vlclua_object_find },
    { "vout", vlclua_get_vout},
    { "aout", vlclua_get_aout},
    { NULL, NULL }
};

void luaopen_object( lua_State *L )
{
    lua_newtable( L );
    luaL_register( L, NULL, vlclua_object_reg );
    lua_setfield( L, -2, "object" );
}
