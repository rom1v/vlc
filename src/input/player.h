/*****************************************************************************
 * player.h: Player internal interface
 *****************************************************************************
 * Copyright © 2018 VLC authors and VideoLAN
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

#include <vlc_player.h>

enum vlc_player_media_ended_action {
    VLC_PLAYER_MEDIA_ENDED_CONTINUE,
    VLC_PLAYER_MEDIA_ENDED_PAUSE,
    VLC_PLAYER_MEDIA_ENDED_STOP,
    VLC_PLAYER_MEDIA_ENDED_EXIT,
};

/**
 * Assert that the player mutex is locked.
 *
 * This is exposed in this internal header because the playlist and its
 * associated player share the lock to avoid lock-order inversion issues.
 */
void
vlc_player_assert_locked(vlc_player_t *player);

void
vlc_player_SetMediaEndedAction(vlc_player_t *player,
                               enum vlc_player_media_ended_action action);
