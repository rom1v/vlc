/*****************************************************************************
 * filters.c
 *****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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

#include "filters.h"

#include <vlc_common.h>

#include "filter_priv.h"
#include "renderer.h"

void
vlc_gl_filters_Init(struct vlc_gl_filters *filters, struct vlc_gl_t *gl,
                    const struct vlc_gl_api *api)
{
    filters->gl = gl;
    filters->api = api;
    vlc_list_init(&filters->list);
}

struct vlc_gl_renderer *
vlc_gl_filters_AppendRenderer(struct vlc_gl_filters *filters,
                              struct vlc_gl_sampler *sampler)
{
    struct vlc_gl_filter *filter = vlc_gl_filter_New(filters->gl, filters->api);
    if (!filter)
        return NULL;

    struct vlc_gl_renderer *renderer =
        vlc_gl_renderer_Open(filters->gl, filters->api, filter, sampler);
    if (!renderer)
    {
        /* Reset the close() callback in case the renderer Open() set it */
        filter->ops = NULL;
        vlc_gl_filter_Delete(filter);
        return NULL;
    }

    struct vlc_gl_filter_priv *priv = vlc_gl_filter_PRIV(filter);
    vlc_list_append(&priv->node, &filters->list);

    return renderer;
}

int
vlc_gl_filters_Draw(struct vlc_gl_filters *filters)
{
    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        struct vlc_gl_filter *filter = &priv->filter;
        int ret = filter->ops->draw(filter);
        if (ret != VLC_SUCCESS)
            return ret;
    }

    return VLC_SUCCESS;
}

void
vlc_gl_filters_Destroy(struct vlc_gl_filters *filters)
{
    struct vlc_gl_filter_priv *priv;
    vlc_list_foreach(priv, &filters->list, node)
    {
        struct vlc_gl_filter *filter = &priv->filter;
        vlc_gl_filter_Delete(filter);
    }
}
