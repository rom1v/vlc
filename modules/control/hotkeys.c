/*****************************************************************************
 * hotkeys.c: Hotkey handling for vlc
 *****************************************************************************
 * Copyright (C) 2005-2009 the VideoLAN team
 *
 * Authors: Sigmund Augdal Helberg <dnumgis@videolan.org>
 *          Jean-Paul Saman <jpsaman #_at_# m2x.nl>
 *          Victorien Le Couviour--Tuffet <victorien.lecouviour.tuffet@gmail.com>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define VLC_MODULE_LICENSE VLC_LICENSE_GPL_2_PLUS
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_input_item.h>
#include <vlc_mouse.h>
#include <vlc_viewpoint.h>
#include <vlc_player.h>
#include <vlc_playlist.h>
#include <vlc_actions.h>
#include "math.h"

struct intf_sys_t
{
    vlc_playlist_t *playlist;
    vlc_player_listener_id *player_listener;
    struct
    {
        bool btn_pressed;
        int x, y;
    } vrnav;
};

static void handle_action(intf_thread_t *, vlc_action_id_t);

/*****************************
 * interface action handling *
 *****************************/

#define INTF_ACTION_HANDLER(name) \
static inline void \
action_handler_Intf##name(intf_thread_t *intf, vlc_action_id_t action_id)

INTF_ACTION_HANDLER()
{
    char const *varname;
    switch (action_id)
    {
        case ACTIONID_QUIT:
            return libvlc_Quit(intf->obj.libvlc);
        case ACTIONID_INTF_TOGGLE_FSC:
        case ACTIONID_INTF_HIDE:
            varname = "intf-toggle-fscontrol";
            break;
        case ACTIONID_INTF_BOSS:
            varname = "intf-boss";
            break;
        case ACTIONID_INTF_POPUP_MENU:
            varname = "intf-popupmenu";
            break;
        default:
            vlc_assert_unreachable();
    }
    var_TriggerCallback(intf->obj.libvlc, varname);
}

INTF_ACTION_HANDLER(ActionCombo)
{
    vlc_player_t *player = vlc_playlist_GetPlayer(intf->p_sys->playlist);
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    bool vrnav = var_GetBool(vout, "viewpoint-changeable");
    vlc_object_release(vout);
    switch (action_id)
    {
        case ACTIONID_COMBO_VOL_FOV_DOWN:
            action_id = !vrnav
                ? ACTIONID_VOL_DOWN
                : ACTIONID_VIEWPOINT_FOV_OUT;
            break;
        case ACTIONID_COMBO_VOL_FOV_UP:
            action_id = !vrnav
                ? ACTIONID_VOL_UP
                : ACTIONID_VIEWPOINT_FOV_IN;
            break;
        default:
            vlc_assert_unreachable();
    }
    handle_action(intf, action_id);
}

/****************************
 * playlist action handling *
 ****************************/

#define PLAYLIST_ACTION_HANDLER(name) \
static inline void \
action_handler_Playlist##name(vlc_playlist_t *playlist, \
                              vlc_action_id_t action_id)

PLAYLIST_ACTION_HANDLER(Interact)
{
    switch (action_id)
    {
        case ACTIONID_PLAY_CLEAR:
            vlc_playlist_Clear(playlist);
            break;
        case ACTIONID_PREV:
            vlc_playlist_Prev(playlist);
            break;
        case ACTIONID_NEXT:
            vlc_playlist_Next(playlist);
            break;
        default:
            vlc_assert_unreachable();
    }
}

PLAYLIST_ACTION_HANDLER(Playback)
{
    switch (action_id)
    {
        case ACTIONID_LOOP:
        {
            enum vlc_playlist_playback_repeat repeat_mode =
                vlc_playlist_GetPlaybackRepeat(playlist);
            switch (repeat_mode)
            {
                case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
                    repeat_mode = VLC_PLAYLIST_PLAYBACK_REPEAT_ALL;
                    break;
                case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
                    repeat_mode = VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT;
                    break;
                case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
                    repeat_mode = VLC_PLAYLIST_PLAYBACK_REPEAT_NONE;
                    break;
            }
            vlc_playlist_SetPlaybackRepeat(playlist, repeat_mode);
            break;
        }
        case ACTIONID_RANDOM:
        {
            enum vlc_playlist_playback_order order_mode =
                vlc_playlist_GetPlaybackOrder(playlist);
            order_mode =
                order_mode == VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL
                    ? VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM
                    : VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL;
            vlc_playlist_SetPlaybackOrder(playlist, order_mode);
            break;
        }
        default:
            vlc_assert_unreachable();
    }
}

static inline void
playlist_bookmark_Set(intf_thread_t *intf,
                      vlc_playlist_t *playlist, char *name, int id)
{
    var_Create(intf, name, VLC_VAR_STRING | VLC_VAR_DOINHERIT);
    vlc_player_t *player = vlc_playlist_GetPlayer(playlist);
    input_item_t *item = vlc_player_GetCurrentMedia(player);
    if (item)
    {
        char *uri = input_item_GetURI(item);
        config_PutPsz(name, uri);
        msg_Info(intf, "setting playlist bookmark %i to %s", id, uri);
        free(uri);
    }
}

static inline void
playlist_bookmark_Play(intf_thread_t *intf,
                       vlc_playlist_t *playlist, char *name)
{
    char *bookmark_uri = var_CreateGetString(intf, name);
    size_t count = vlc_playlist_Count(playlist);
    size_t i;
    for (i = 0; i < count; ++i)
    {
        vlc_playlist_item_t *plitem = vlc_playlist_Get(playlist, i);
        input_item_t *item = vlc_playlist_item_GetMedia(plitem);
        char *item_uri = input_item_GetURI(item);
        if (!strcmp(bookmark_uri, item_uri))
            break;
        free(item_uri);
    }
    if (i != count)
        vlc_playlist_PlayAt(playlist, i);
    free(bookmark_uri);
}

INTF_ACTION_HANDLER(PlaylistBookmark)
{
    bool set = action_id >= ACTIONID_SET_BOOKMARK1 &&
               action_id <= ACTIONID_SET_BOOKMARK10;
    int id = set ? ACTIONID_SET_BOOKMARK1 : ACTIONID_PLAY_BOOKMARK1;
    id -= action_id - 1;
    char *bookmark_name;
    if (asprintf(&bookmark_name, "bookmark%i", id) == -1)
        return;
    vlc_playlist_t *playlist = intf->p_sys->playlist;
    if (set)
        playlist_bookmark_Set(intf, playlist, bookmark_name, id);
    else
        playlist_bookmark_Play(intf, playlist, bookmark_name);
    free(bookmark_name);
}

/**************************
 * player action handling *
 **************************/

#define PLAYER_ACTION_HANDLER(name) \
static inline void action_handler_Player##name(vlc_player_t *player, \
                                               vlc_action_id_t action_id)

PLAYER_ACTION_HANDLER(State)
{
    switch (action_id)
    {
        case ACTIONID_PLAY_PAUSE:
        {
            enum vlc_player_state state = vlc_player_GetState(player);
            if (state == VLC_PLAYER_STATE_PAUSED)
                vlc_player_Resume(player);
            else
                vlc_player_Pause(player);
            break;
        }
        case ACTIONID_PLAY:
            vlc_player_Start(player);
            break;
        case ACTIONID_PAUSE:
            vlc_player_Pause(player);
            break;
        case ACTIONID_STOP:
            vlc_player_Stop(player);
            break;
        case ACTIONID_FRAME_NEXT:
            vlc_player_NextVideoFrame(player);
            break;
        default:
            vlc_assert_unreachable();
    }
}

INTF_ACTION_HANDLER(PlayerSeek)
{
    vlc_player_t *player = vlc_playlist_GetPlayer(intf->p_sys->playlist);
    if (!vlc_player_CanSeek(player))
        return;
    char const *varname;
    int sign = +1;
    switch (action_id)
    {
        case ACTIONID_JUMP_BACKWARD_EXTRASHORT:
            sign = -1;
            /* fall through */
        case ACTIONID_JUMP_FORWARD_EXTRASHORT:
            varname = "extrashort-jump-size";
            break;
        case ACTIONID_JUMP_BACKWARD_SHORT:
            sign = -1;
            /* fall through */
        case ACTIONID_JUMP_FORWARD_SHORT:
            varname = "short-jump-size";
            break;
        case ACTIONID_JUMP_BACKWARD_MEDIUM:
            sign = -1;
            /* fall through */
        case ACTIONID_JUMP_FORWARD_MEDIUM:
            varname = "medium-jump-size";
            break;
        case ACTIONID_JUMP_BACKWARD_LONG:
            sign = -1;
            /* fall through */
        case ACTIONID_JUMP_FORWARD_LONG:
            varname = "long-jump-size";
            break;
        default:
            vlc_assert_unreachable();
    }
    int jmpsz = var_InheritInteger(intf->obj.libvlc, varname);
    if (jmpsz >= 0)
        vlc_player_JumpTime(player, vlc_tick_from_sec(jmpsz * sign));
}

PLAYER_ACTION_HANDLER(Position)
{
    VLC_UNUSED(action_id);
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    if (vout_OSDEpg(vout, vlc_player_GetCurrentMedia(player)))
        vlc_player_DisplayPosition(player);
    vlc_object_release(vout);
}

PLAYER_ACTION_HANDLER(NavigateMedia)
{
    switch (action_id)
    {
        case ACTIONID_PROGRAM_SID_PREV:
            vlc_player_SelectPrevProgram(player);
            break;
        case ACTIONID_PROGRAM_SID_NEXT:
            vlc_player_SelectNextProgram(player);
            break;
        case ACTIONID_TITLE_PREV:
            vlc_player_SelectPrevTitle(player);
            break;
        case ACTIONID_TITLE_NEXT:
            vlc_player_SelectNextTitle(player);
            break;
        case ACTIONID_CHAPTER_PREV:
            vlc_player_SelectPrevChapter(player);
            break;
        case ACTIONID_CHAPTER_NEXT:
            vlc_player_SelectNextChapter(player);
            break;
        case ACTIONID_DISC_MENU:
            vlc_player_Navigate(player, VLC_PLAYER_NAV_MENU);
            break;
        default:
            vlc_assert_unreachable();
    }
}

PLAYER_ACTION_HANDLER(Track)
{
    switch (action_id)
    {
        case ACTIONID_AUDIO_TRACK:
            vlc_player_SelectNextTrack(player, AUDIO_ES);
            break;
        case ACTIONID_SUBTITLE_REVERSE_TRACK:
            vlc_player_SelectPrevTrack(player, SPU_ES);
            break;
        case ACTIONID_SUBTITLE_TRACK:
            vlc_player_SelectNextTrack(player, SPU_ES);
            break;
        default:
            vlc_assert_unreachable();
    }
}

PLAYER_ACTION_HANDLER(Delay)
{
    enum { AUDIODELAY, SUBDELAY } type;
    int delta = 50;
    switch (action_id)
    {
        case ACTIONID_AUDIODELAY_DOWN:
            delta = -50;
            /* fall-through */
        case ACTIONID_AUDIODELAY_UP:
            type = AUDIODELAY;
            break;
        case ACTIONID_SUBDELAY_DOWN:
            delta = -50;
            /* fall-through */
        case ACTIONID_SUBDELAY_UP:
            type = SUBDELAY;
            break;
        default:
            vlc_assert_unreachable();
    }
    enum vlc_player_whence whence = VLC_PLAYER_WHENCE_RELATIVE;
    delta = VLC_TICK_FROM_MS(delta);
    if (type == AUDIODELAY)
        vlc_player_SetAudioDelay(player, delta, whence);
    else
        vlc_player_SetSubtitleDelay(player, delta, whence);
}

static inline float
AdjustRateFine(float rate, int const dir)
{
    float rate_min = (float)INPUT_RATE_DEFAULT / INPUT_RATE_MAX;
    float rate_max = (float)INPUT_RATE_DEFAULT / INPUT_RATE_MIN;
    int sign = rate < 0 ? -1 : 1;
    rate = floor(fabs(rate) / 0.1 + dir + 0.05) * 0.1;
    if (rate < rate_min)
       rate = rate_min;
    else if (rate > rate_max)
        rate = rate_max;
    return rate * sign;
}

PLAYER_ACTION_HANDLER(Rate)
{
    switch (action_id)
    {
        case ACTIONID_RATE_SLOWER:
            vlc_player_DecrementRate(player);
            break;
        case ACTIONID_RATE_FASTER:
            vlc_player_IncrementRate(player);
            break;
        default:
        {
            float rate;
            switch (action_id)
            {
                case ACTIONID_RATE_NORMAL:
                    rate = 1.f;
                    break;
                case ACTIONID_RATE_SLOWER_FINE:
                case ACTIONID_RATE_FASTER_FINE:
                {
                    int const dir = action_id == ACTIONID_RATE_SLOWER_FINE ?
                        -1 : +1;
                    rate = vlc_player_GetRate(player);
                    rate = AdjustRateFine(rate, dir);
                    break;
                }
                default:
                    vlc_assert_unreachable();
            }
            vlc_player_ChangeRate(player, rate);
            break;
        }
    }
}

PLAYER_ACTION_HANDLER(ToggleSubtitle)
{
    VLC_UNUSED(action_id);
    vlc_player_ToggleSubtitle(player);
}

PLAYER_ACTION_HANDLER(SyncSubtitle)
{
    switch (action_id)
    {
        case ACTIONID_SUBSYNC_MARKAUDIO:
            vlc_player_SetSubtitleSync(
                    player, VLC_PLAYER_SUBTITLE_SYNC_MARK_AUDIO);
            break;
        case ACTIONID_SUBSYNC_MARKSUB:
            vlc_player_SetSubtitleSync(
                    player, VLC_PLAYER_SUBTITLE_SYNC_MARK_SUBTITLE);
            break;
        case ACTIONID_SUBSYNC_APPLY:
            // FIXME is that still the case?
            /* Warning! Can yield a pause in the playback.
             * For example, the following succession of actions will yield a 5 second delay :
             * - Pressing Shift-H (ACTIONID_SUBSYNC_MARKAUDIO)
             * - wait 5 second
             * - Press Shift-J (ACTIONID_SUBSYNC_MARKSUB)
             * - Press Shift-K (ACTIONID_SUBSYNC_APPLY)
             * --> 5 seconds pause
             * This is due to var_SetTime() (and ultimately UpdatePtsDelay())
             * which causes the video to pause for an equivalent duration
             * (This problem is also present in the "Track synchronization" window) */
            vlc_player_SetSubtitleSync(player, VLC_PLAYER_SUBTITLE_SYNC_APPLY);
            break;
        case ACTIONID_SUBSYNC_RESET:
            vlc_player_SetSubtitleSync(player, VLC_PLAYER_SUBTITLE_SYNC_RESET);
            break;
        default:
            vlc_assert_unreachable();
    }
}

PLAYER_ACTION_HANDLER(Navigate)
{
    enum vlc_player_nav nav;
    switch (action_id)
    {
#define PLAYER_NAV_FROM_ACTION(navval) \
        case ACTIONID_NAV_##navval: \
            nav = VLC_PLAYER_NAV_##navval; \
            break;
        PLAYER_NAV_FROM_ACTION(ACTIVATE)
        PLAYER_NAV_FROM_ACTION(UP)
        PLAYER_NAV_FROM_ACTION(DOWN)
        PLAYER_NAV_FROM_ACTION(LEFT)
        PLAYER_NAV_FROM_ACTION(RIGHT)
#undef PLAYER_NAV_FROM_ACTION
        default:
            vlc_assert_unreachable();
    }
    vlc_player_Navigate(player, nav);
}

PLAYER_ACTION_HANDLER(Viewpoint)
{
    vlc_viewpoint_t viewpoint;
    switch (action_id)
    {
        case ACTIONID_VIEWPOINT_FOV_IN:
            viewpoint.fov = -1.f;
            break;
        case ACTIONID_VIEWPOINT_FOV_OUT:
            viewpoint.fov = +1.f;
            break;
        case ACTIONID_VIEWPOINT_ROLL_CLOCK:
            viewpoint.roll = -1.f;
            break;
        case ACTIONID_VIEWPOINT_ROLL_ANTICLOCK:
            viewpoint.roll = +1.f;
            break;
        default:
            vlc_assert_unreachable();
    }
    vlc_player_UpdateViewpoint(player, &viewpoint,
                               VLC_PLAYER_WHENCE_RELATIVE);
}

PLAYER_ACTION_HANDLER(Record)
{
    VLC_UNUSED(action_id);
    vlc_player_ToggleRecording(player);
}

static int
AudioDeviceCycle(audio_output_t *aout, char **p_name)
{
    int ret = -1;

    char *device = aout_DeviceGet(aout);
    if (!device)
        goto end;
    char **ids, **names;
    int n = aout_DevicesList(aout, &ids, &names);
    if (n == -1)
        goto end;

    int index;
    for (index = 0; index < n; ++index)
        if (!strcmp(ids[index], device))
        {
            index = (index + 1) % n;
            break;
        }
    ret = aout_DeviceSet(aout, ids[index]);
    if (p_name != NULL)
        *p_name = strdup(names[index]);

    free(device);
    for (int i = 0; i < n; ++i)
    {
        free(ids[i]);
        free(names[i]);
    }
    free(ids);
    free(names);
end:
    return ret;
}

PLAYER_ACTION_HANDLER(Aout)
{
    switch (action_id)
    {
        case ACTIONID_VOL_DOWN:
            vlc_player_aout_DecrementVolume(player, 1, NULL);
            break;
        case ACTIONID_VOL_UP:
            vlc_player_aout_IncrementVolume(player, 1, NULL);
            break;
        case ACTIONID_VOL_MUTE:
            vlc_player_aout_ToggleMute(player);
            break;
        case ACTIONID_AUDIODEVICE_CYCLE:
        {
            audio_output_t *aout = vlc_player_aout_Hold(player);
            if (!aout)
                break;
            char *devname;
            if (!AudioDeviceCycle(aout, &devname))
            {
                vlc_player_vout_OSDMessage(player, _("Audio device: %s"), devname);
                free(devname);
            }
            vlc_object_release(aout);
            break;
        }
        default:
            vlc_assert_unreachable();
    }
}

PLAYER_ACTION_HANDLER(Vouts)
{
    switch (action_id)
    {
        case ACTIONID_TOGGLE_FULLSCREEN:
            vlc_player_vout_ToggleFullscreen(player);
            break;
        case ACTIONID_LEAVE_FULLSCREEN:
            vlc_player_vout_SetFullscreen(player, false);
            break;
        case ACTIONID_SNAPSHOT:
            vlc_player_vout_Snapshot(player);
            break;
        case ACTIONID_WALLPAPER:
            vlc_player_vout_ToggleWallpaperMode(player);
            break;
        default:
            vlc_assert_unreachable();
    }
}

/************************
 * vout action handling *
 ************************/

#define VOUT_ACTION_HANDLER(name) \
static inline void \
action_handler_Vout##name(vout_thread_t *vout, vlc_action_id_t action_id)

static inline void
vout_CycleVariable(vout_thread_t *vout,
                   char const *varname, int vartype, bool next)
{
    vlc_value_t val;
    var_Get(vout, varname, &val);
    size_t num_choices;
    vlc_value_t *choices;
    var_Change(vout, varname, VLC_VAR_GETCHOICES,
               &num_choices, &choices, NULL);

    vlc_value_t *choice = choices;
    for (size_t curidx = 0; curidx < num_choices; ++curidx, ++choice)
        if ((vartype == VLC_VAR_FLOAT &&
             choice->f_float == val.f_float) ||
            (vartype == VLC_VAR_STRING &&
             !strcmp(choice->psz_string, val.psz_string)))
        {
            curidx += next ? +1 : -1;
            if (next && curidx == num_choices)
                curidx = 0;
            else if (!next && curidx == (size_t)-1)
                curidx = num_choices - 1;
            choice = choices + curidx;
            break;
        }
    if (choice == choices + num_choices)
        choice = choices;
    if (vartype == VLC_VAR_FLOAT)
        var_SetFloat(vout, varname, choice->f_float);
    else if (vartype == VLC_VAR_STRING)
        var_SetString(vout, varname, choice->psz_string);

    if (vartype == VLC_VAR_STRING)
    {
        free(val.psz_string);
        for (size_t i = 0; i < num_choices; ++i)
            free(choices[i].psz_string);
    }
    free(choices);
}

#define vout_CycleVariable(vout, varname, vartype, next) \
    do \
    { \
        static_assert(vartype == VLC_VAR_FLOAT || \
                      vartype == VLC_VAR_STRING, \
                      "vartype must be either VLC_VAR_FLOAT or VLC_VAR_STRING"); \
        vout_CycleVariable(vout, varname, vartype, next); \
    } while (0)

VOUT_ACTION_HANDLER(AspectRatio)
{
    VLC_UNUSED(action_id);
    vout_CycleVariable(vout, "aspect-ratio", VLC_VAR_STRING, true);
}

VOUT_ACTION_HANDLER(Crop)
{
    if (action_id == ACTIONID_CROP)
    {
        vout_CycleVariable(vout, "crop", VLC_VAR_STRING, true);
        return;
    }
    char const *varname;
    int delta;
    switch (action_id)
    {
#define CASE_CROP(crop, var) \
        case ACTIONID_CROP_##crop: \
        case ACTIONID_UNCROP_##crop: \
            varname = "crop-"#var; \
            delta = action_id == ACTIONID_CROP_##crop? +1 : -1; \
            break;
                CASE_CROP(TOP, top)
                CASE_CROP(BOTTOM, bottom)
                CASE_CROP(LEFT, left)
                CASE_CROP(RIGHT, right)
#undef CASE_CROP
        default:
            vlc_assert_unreachable();
    }
    int crop = var_GetInteger(vout, varname);
    var_SetInteger(vout, varname, crop + delta);
}

VOUT_ACTION_HANDLER(Zoom)
{
    char const *varname = "zoom";
    switch (action_id)
    {
        case ACTIONID_TOGGLE_AUTOSCALE:
            if (var_GetFloat(vout, varname) != 1.f)
                var_SetFloat(vout, varname, 1.f);
            else
                var_ToggleBool(vout, "autoscale");
            break;
        case ACTIONID_SCALE_DOWN:
        case ACTIONID_SCALE_UP:
        {
            float zoom = var_GetFloat(vout, varname);
            float delta = action_id == ACTIONID_SCALE_DOWN ?
                -.1f : +.1f;
            if ((zoom >= .3f || delta > 0) && (zoom <= 10.f || delta < 0))
                var_SetFloat(vout, varname, zoom + delta);
            break;
        }
        case ACTIONID_ZOOM:
        case ACTIONID_UNZOOM:
            vout_CycleVariable(vout, varname, VLC_VAR_FLOAT,
                               action_id == ACTIONID_ZOOM);
            break;
        default:
        {
            static float const zoom_modes[] = { .25f, .5f, 1.f, 2.f };
            var_SetFloat(vout, varname,
                         zoom_modes[action_id - ACTIONID_ZOOM_QUARTER]);
            break;
        }
    }
}

VOUT_ACTION_HANDLER(Deinterlace)
{
    if (action_id == ACTIONID_DEINTERLACE)
        var_SetInteger(vout, "deinterlace",
                       var_GetInteger(vout, "deinterlace") ? 0 : 1);
    else if (action_id == ACTIONID_DEINTERLACE_MODE)
        vout_CycleVariable(vout, "deinterlace-mode", VLC_VAR_STRING, true);
}

VOUT_ACTION_HANDLER(SubtitleDisplay)
{
    switch (action_id)
    {
        case ACTIONID_SUBPOS_DOWN:
            var_DecInteger(vout, "sub-margin");
            break;
        case ACTIONID_SUBPOS_UP:
            var_IncInteger(vout, "sub-margin");
            break;
        default:
        {
            // FIXME all vouts
            char const *varname = "sub-text-scale";
            int scale;
            if (action_id == ACTIONID_SUBTITLE_TEXT_SCALE_NORMAL)
                scale = 100;
            else
            {
                scale = var_GetInteger(vout, varname);
                unsigned delta = (scale > 100 ? scale - 100 : 100 - scale) / 25;
                delta = delta <= 1 ? 10 : 25;
                if (action_id == ACTIONID_SUBTITLE_TEXT_SCALE_DOWN)
                    delta = -delta;
                scale += delta;
                scale -= scale % delta;
                scale = VLC_CLIP(scale, 25, 500);
            }
            var_SetInteger(vout, varname, scale);
            break;
        }
    }
}

/****************
 * action table *
 ****************/

struct vlc_action
{
    enum
    {
        NULL_ACTION = -1,
        INTF_ACTION,
        PLAYLIST_ACTION,
        PLAYER_ACTION,
        VOUT_ACTION,
    } type;
    struct
    {
        vlc_action_id_t first;
        vlc_action_id_t last;
    } range;
    union
    {
        void *fptr;
        void (*pf_intf)(intf_thread_t *, vlc_action_id_t);
        void (*pf_playlist)(vlc_playlist_t *, vlc_action_id_t);
        void (*pf_player)(vlc_player_t *, vlc_action_id_t);
        void (*pf_vout)(vout_thread_t *vout, vlc_action_id_t);
    } handler;
    bool pl_need_lock;
};

static struct vlc_action const actions[] =
{
#define VLC_ACTION(typeval, first, last, name, lock) \
    { \
        .type = typeval, \
        .range = { ACTIONID_##first, ACTIONID_##last }, \
        .handler.fptr = action_handler_##name, \
        .pl_need_lock = lock \
    },
#define VLC_ACTION_INTF(first, last, name, lock) \
    VLC_ACTION(INTF_ACTION, first, last, Intf ## name, lock)
#define VLC_ACTION_PLAYLIST(first, last, name) \
    VLC_ACTION(PLAYLIST_ACTION, first, last, Playlist ## name, true)
#define VLC_ACTION_PLAYER(first, last, name, lock) \
    VLC_ACTION(PLAYER_ACTION, first, last, Player ## name, lock)
#define VLC_ACTION_VOUT(first, last, name) \
    VLC_ACTION(VOUT_ACTION, first, last, Vout ## name, false)

    /* interface actions */
    VLC_ACTION_INTF(QUIT, INTF_POPUP_MENU, , false)
    VLC_ACTION_INTF(COMBO_VOL_FOV_DOWN, COMBO_VOL_FOV_UP, ActionCombo, false)
    /* playlist actions */
    VLC_ACTION_PLAYLIST(PLAY_CLEAR, NEXT, Interact)
    VLC_ACTION_PLAYLIST(LOOP, RANDOM, Playback)
    VLC_ACTION_INTF(SET_BOOKMARK1, PLAY_BOOKMARK10, PlaylistBookmark, true)
    /* player actions */
    VLC_ACTION_PLAYER(PLAY_PAUSE, FRAME_NEXT, State, true)
    VLC_ACTION_INTF(JUMP_BACKWARD_EXTRASHORT, JUMP_FORWARD_LONG, PlayerSeek, true)
    VLC_ACTION_PLAYER(POSITION, POSITION, Position, true)
    VLC_ACTION_PLAYER(PROGRAM_SID_PREV, DISC_MENU, NavigateMedia, true)
    VLC_ACTION_PLAYER(AUDIO_TRACK, SUBTITLE_TRACK, Track, true)
    VLC_ACTION_PLAYER(AUDIODELAY_DOWN, SUBDELAY_UP, Delay, true)
    VLC_ACTION_PLAYER(RATE_NORMAL, RATE_FASTER_FINE, Rate, true)
    VLC_ACTION_PLAYER(SUBTITLE_TOGGLE, SUBTITLE_TOGGLE, ToggleSubtitle, true)
    VLC_ACTION_PLAYER(SUBSYNC_MARKAUDIO, SUBSYNC_RESET, SyncSubtitle, true)
    VLC_ACTION_PLAYER(NAV_ACTIVATE, NAV_RIGHT, Navigate, true)
    VLC_ACTION_PLAYER(VIEWPOINT_FOV_IN, VIEWPOINT_ROLL_ANTICLOCK, Viewpoint, true)
    VLC_ACTION_PLAYER(RECORD, RECORD, Record, true)
    VLC_ACTION_PLAYER(VOL_DOWN, AUDIODEVICE_CYCLE, Aout, false)
    VLC_ACTION_PLAYER(TOGGLE_FULLSCREEN, WALLPAPER, Vouts, false)
    /* vout actions */
    VLC_ACTION_VOUT(ASPECT_RATIO, ASPECT_RATIO, AspectRatio)
    VLC_ACTION_VOUT(CROP, UNCROP_RIGHT, Crop)
    VLC_ACTION_VOUT(TOGGLE_AUTOSCALE, ZOOM_DOUBLE, Zoom)
    VLC_ACTION_VOUT(DEINTERLACE, DEINTERLACE_MODE, Deinterlace)
    VLC_ACTION_VOUT(SUBPOS_DOWN, SUBTITLE_TEXT_SCALE_UP, SubtitleDisplay)
    /* null action */
    { .type = NULL_ACTION }

#undef VLC_ACTION_VOUT
#undef VLC_ACTION_PLAYER
#undef VLC_ACTION_PLAYLIST
#undef VLC_ACTION_INTF
#undef VLC_ACTION
};

static void
handle_action(intf_thread_t *intf, vlc_action_id_t action_id)
{
    size_t action_idx;
    for (action_idx = 0; actions[action_idx].type != NULL_ACTION; ++action_idx)
        if (actions[action_idx].range.first <= action_id &&
            actions[action_idx].range.last >= action_id)
            break;
    if (actions[action_idx].type == NULL_ACTION)
        return msg_Warn(intf, "no handler for action %d", action_id);
    struct vlc_action const *action = actions + action_idx;

    vlc_playlist_t *playlist = intf->p_sys->playlist;
    if (action->pl_need_lock)
        vlc_playlist_Lock(playlist);

    switch (action->type)
    {
        case INTF_ACTION:
            action->handler.pf_intf(intf, action_id);
            break;
        case PLAYLIST_ACTION:
            action->handler.pf_playlist(playlist, action_id);
            break;
        case PLAYER_ACTION:
        case VOUT_ACTION:
        {
            vlc_player_t *player = vlc_playlist_GetPlayer(playlist);
            if (action->type == PLAYER_ACTION)
                action->handler.pf_player(player, action_id);
            else
            {
                vout_thread_t *vout = vlc_player_vout_Hold(player);
                action->handler.pf_vout(vout, action_id);
                vlc_object_release(vout);
            }
            break;
        }
        default:
            vlc_assert_unreachable();
    }

    if (action->pl_need_lock)
        vlc_playlist_Unlock(playlist);
}

/******************
 * vout callbacks *
 ******************/

static int
MouseButtonCallback(vlc_object_t *obj, char const *var,
                    vlc_value_t oldval, vlc_value_t newval, void *data)
{
    VLC_UNUSED(var);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;
    vout_thread_t *vout = (vout_thread_t *)obj;

    if ((newval.i_int & (1 << MOUSE_BUTTON_LEFT)) &&
        var_GetBool(vout, "viewpoint-changeable"))
    {
        if (!sys->vrnav.btn_pressed)
        {
            sys->vrnav.btn_pressed = true;
            var_GetCoords(vout, "mouse-moved", &sys->vrnav.x, &sys->vrnav.y);
        }
    }
    else
        sys->vrnav.btn_pressed = false;

    unsigned pressed = newval.i_int & ~oldval.i_int;
    if (pressed & (1 << MOUSE_BUTTON_LEFT))
        var_SetBool(intf->obj.libvlc, "intf-popupmenu", false);
    if (pressed & (1 << MOUSE_BUTTON_CENTER))
        var_TriggerCallback(intf->obj.libvlc, "intf-toggle-fscontrol");
#ifndef _WIN32
    if (pressed & (1 << MOUSE_BUTTON_RIGHT))
#else
    if ((oldval.i_int & (1 << MOUSE_BUTTON_RIGHT))
     && !(newval.i_int & (1 << MOUSE_BUTTON_RIGHT)))
#endif
        var_SetBool(intf->obj.libvlc, "intf-popupmenu", true);
    for (int i = MOUSE_BUTTON_WHEEL_UP; i <= MOUSE_BUTTON_WHEEL_RIGHT; i++)
        if (pressed & (1 << i))
        {
            int keycode = KEY_MOUSEWHEEL_FROM_BUTTON(i);
            var_SetInteger(intf->obj.libvlc, "key-pressed", keycode);
        }

    return VLC_SUCCESS;
}

static int
MouseMovedCallback(vlc_object_t *obj, char const *var,
                   vlc_value_t ov, vlc_value_t newval, void *data)
{
    VLC_UNUSED(obj); VLC_UNUSED(var); VLC_UNUSED(ov);
    intf_sys_t *sys = data;
    vlc_player_t *player = vlc_playlist_GetPlayer(sys->playlist);
    if (sys->vrnav.btn_pressed)
    {
        int i_horizontal = newval.coords.x - sys->vrnav.x;
        int i_vertical   = newval.coords.y - sys->vrnav.y;
        vlc_viewpoint_t viewpoint =
        {
            .yaw   = -i_horizontal * 0.05f,
            .pitch = -i_vertical   * 0.05f,
        };
        vlc_player_Lock(player);
        vlc_player_UpdateViewpoint(player, &viewpoint,
                                   VLC_PLAYER_WHENCE_RELATIVE);
        vlc_player_Unlock(player);
        sys->vrnav.x = newval.coords.x;
        sys->vrnav.y = newval.coords.y;
    }
    return VLC_SUCCESS;
}

static int
ViewpointMovedCallback(vlc_object_t *obj, char const *var,
                       vlc_value_t ov, vlc_value_t newval, void *data)
{
    VLC_UNUSED(obj); VLC_UNUSED(var); VLC_UNUSED(ov);
    vlc_player_t *player = data;
    vlc_player_Lock(player);
    vlc_player_UpdateViewpoint(player, (vlc_viewpoint_t *)newval.p_address,
		    	       VLC_PLAYER_WHENCE_RELATIVE);
    vlc_player_Unlock(player);
    return VLC_SUCCESS;
}

static void
player_on_vout_list_changed(vlc_player_t *player,
                            enum vlc_player_list_action action,
                            vout_thread_t *vout,
                            void *data)
{
    if (action == VLC_PLAYER_LIST_UPDATED)
        return;
    intf_thread_t *intf = data;
    bool vrnav = var_GetBool(vout, "viewpoint-changeable");
    if (action == VLC_PLAYER_LIST_ADDED)
    {
        var_AddCallback(vout, "mouse-button-down", MouseButtonCallback, intf);
        var_AddCallback(vout, "mouse-moved", MouseMovedCallback, intf->p_sys);
        if (vrnav)
            var_AddCallback(vout, "viewpoint-moved",
                            ViewpointMovedCallback, player);
    }
    else
    {
        var_DelCallback(vout, "mouse-button-down", MouseButtonCallback, intf);
        var_DelCallback(vout, "mouse-moved", MouseMovedCallback, intf->p_sys);
        if (vrnav)
            var_DelCallback(vout, "viewpoint-moved",
                            ViewpointMovedCallback, player);
    }
}

static int
ActionCallback(vlc_object_t *obj, char const *var,
               vlc_value_t ov, vlc_value_t newval, void *data)
{
    VLC_UNUSED(obj); VLC_UNUSED(var); VLC_UNUSED(ov);
    handle_action(data, newval.i_int);
    return VLC_SUCCESS;
}

/****************************
 * module opening / closing *
 ****************************/

static int
Open(vlc_object_t *this)
{
    intf_thread_t *intf = (intf_thread_t *)this;
    intf_sys_t *sys = malloc(sizeof(intf_sys_t));
    if (!sys)
        return VLC_ENOMEM;
    sys->vrnav.btn_pressed = false;
    sys->playlist = vlc_intf_GetMainPlaylist(intf);
    static struct vlc_player_cbs const player_cbs =
    {
        .on_vout_list_changed = player_on_vout_list_changed,
    };
    vlc_player_t *player = vlc_playlist_GetPlayer(sys->playlist);
    vlc_player_Lock(player);
    sys->player_listener = vlc_player_AddListener(player, &player_cbs, intf);
    vlc_player_Unlock(player);
    if (!sys->player_listener)
    {
        free(sys);
        return VLC_EGENERIC;
    }
    var_AddCallback(intf->obj.libvlc, "key-action", ActionCallback, intf);
    intf->p_sys = sys;
    return VLC_SUCCESS;
}

static void
Close(vlc_object_t *this)
{
    intf_thread_t *intf = (intf_thread_t *)this;
    intf_sys_t *sys = intf->p_sys;
    vlc_player_t *player = vlc_playlist_GetPlayer(sys->playlist);
    vlc_player_Lock(player);
    vlc_player_RemoveListener(player, sys->player_listener);
    vlc_player_Unlock(player);
    var_DelCallback(intf->obj.libvlc, "key-action", ActionCallback, intf);
    free(sys);
}

vlc_module_begin ()
    set_shortname(N_("Hotkeys"))
    set_description(N_("Hotkeys management interface"))
    set_capability("interface", 0)
    set_callbacks(Open, Close)
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_HOTKEYS)
vlc_module_end ()
