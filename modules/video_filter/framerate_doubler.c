/*****************************************************************************
 * framerate_doubler.c
 *****************************************************************************
 * Copyright (C) 2020 Videolabs, VideoLAN and VLC authors
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

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_picture.h>

#define FILTER_CFG_PREFIX "sout-deinterlace-"

struct sys {
    vlc_tick_t last_pts;
};

static picture_t *
Filter(filter_t *filter, picture_t *pic)
{
    struct sys *sys = filter->p_sys;
    vlc_tick_t last_pts = sys->last_pts;

    sys->last_pts = pic->date;

    picture_t *dup = picture_Clone(pic);
    if (!dup)
    {
        picture_Release(pic);
        return NULL;
    }

    picture_CopyProperties(dup, pic);

    if (last_pts != VLC_TICK_INVALID)
        /*
         *                       dup->date
         *                       v
         *        |----.----|----.----|
         *        ^         ^
         * last_pts       pic->date
         */
        dup->date = (3 * pic->date - last_pts) / 2;

    else if (filter->fmt_in.video.i_frame_rate != 0)
    {
        video_format_t *fmt = &filter->fmt_in.video;
        vlc_tick_t interval =
            vlc_tick_from_samples(fmt->i_frame_rate_base, fmt->i_frame_rate);
        dup->date = pic->date + interval;
    }

    fprintf(stderr, "pic: %ld, dup: %ld\n", (long)pic->date, (long)dup->date);

    /* Return the two pictures as a chain */
    pic->p_next = dup;

    return pic;
}

static int
Open(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *) obj;

    char *mode = var_InheritString(filter, FILTER_CFG_PREFIX "mode");

    /* This implementation only provides "framerate-doubler" mode */
    bool expected_mode = !strcmp(mode, "framerate-doubler");
    free(mode);
    if (!expected_mode)
        return VLC_EGENERIC;

    struct sys *sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_EGENERIC;

    sys->last_pts = VLC_TICK_INVALID;

    filter->pf_video_filter = Filter;
    filter->p_sys = sys;
    filter->fmt_out.video.i_frame_rate *= 2;
    return VLC_SUCCESS;
}

static void
Close(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *) obj;
    struct sys *sys = filter->p_sys;

    free(sys);
}

vlc_module_begin()
    set_description(N_("Framerate doubler"))
    set_capability("video filter", 0)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_callbacks(Open, Close)
    add_shortcut("deinterlace")

vlc_module_end()
