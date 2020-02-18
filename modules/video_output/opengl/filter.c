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

#include <assert.h>

#include <vlc_common.h>
#include <vlc_modules.h>

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
    filter->owner_ops = owner_ops;
    filter->owner_data = owner_data;
    filter->module = NULL;
    return filter;
}

static int
ActivateGLFilter(void *func, bool forced, va_list args)
{
    (void) forced;
    vlc_gl_filter_open_fn *activate = func;
    struct vlc_gl_filter *filter = va_arg(args, struct vlc_gl_filter *);
    const config_chain_t *config = va_arg(args, config_chain_t *);

    return activate(filter, config);
}

#undef vlc_gl_filter_LoadModule
struct vlc_gl_filter *
vlc_gl_filter_LoadModule(vlc_object_t *parent, const struct vlc_gl_api *api,
                         const char *name, const config_chain_t *config,
                         const struct vlc_gl_filter_owner_ops *owner_ops,
                         void *owner_data)
{
    struct vlc_gl_filter *filter =
        vlc_gl_filter_New(parent, api, owner_ops, owner_data);
    if (!filter)
        return NULL;

    filter->module = vlc_module_load(parent, "opengl filter", name, true,
                                     ActivateGLFilter, filter, config);
    if (!filter->module)
    {
        vlc_gl_filter_Delete(filter);
        return NULL;
    }

    assert(filter->ops->draw);

    return filter;
}

void
vlc_gl_filter_Delete(struct vlc_gl_filter *filter)
{
    struct vlc_gl_filter_priv *priv = vlc_gl_filter_PRIV(filter);
    if (priv->sampler)
        vlc_gl_sampler_Delete(priv->sampler);
    if (filter->module)
        module_unneed(filter, filter->module);
    vlc_object_delete(&filter->obj);
}
