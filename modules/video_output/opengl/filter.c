/*****************************************************************************
 * filter.c
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

#include "filter_priv.h"
#include "sampler_priv.h"

#undef vlc_gl_filter_New
struct vlc_gl_filter *
vlc_gl_filter_New(vlc_object_t *parent, const struct vlc_gl_api *api,
                  const struct vlc_gl_filter_owner_ops *owner_ops,
                  void *owner_data)
{
    struct vlc_gl_filter_priv *priv = vlc_object_create(parent, sizeof(*priv));
    if (!priv)
        return NULL;

    priv->sampler = NULL;

    struct vlc_gl_filter *filter = &priv->filter;
    filter->api = api;
    filter->ops = NULL;
    filter->sys = NULL;
    filter->owner_ops = owner_ops;
    filter->owner_data = owner_data;

    return filter;
}

void
vlc_gl_filter_Delete(struct vlc_gl_filter *filter)
{
    if (filter->ops && filter->ops->close)
        filter->ops->close(filter);

    struct vlc_gl_filter_priv *priv = vlc_gl_filter_PRIV(filter);
    if (priv->sampler)
        vlc_gl_sampler_Delete(priv->sampler);

    vlc_object_delete(&filter->obj);
}
