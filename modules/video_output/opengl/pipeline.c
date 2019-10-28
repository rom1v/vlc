/*****************************************************************************
 * pipeline.c
 *****************************************************************************
 * Copyright (C) 2019 Videolabs
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

#include "pipeline.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc_common.h>
#include <vlc_modules.h>

#include "program_priv.h"

struct vlc_gl_pipeline {
    struct vlc_gl_importer *importer;
    struct vlc_gl_renderer *renderer;
    struct vlc_gl_program program;
};

static int
ActivateImporter(void *func, bool forced, va_list args)
{
    vlc_gl_importer_open_fn *activate = func;
    struct vlc_gl_importer *importer = va_arg(args, struct vlc_gl_importer *);
    struct vlc_gl_program *program = va_arg(args, struct vlc_gl_program *);
    VLC_UNUSED(forced);
    return activate(importer, program);
}

static inline struct vlc_gl_importer *
ImporterNew(vlc_object_t *obj, const opengl_vtable_t *gl,
            const video_format_t *fmt)
{
    struct vlc_gl_importer *importer =
        vlc_object_create(obj, sizeof(*importer));
    if (!importer)
        return NULL;

    importer->gl = gl;
    importer->vctx = NULL;

    int ret = video_format_Copy(&importer->fmt, fmt);
    if (ret != VLC_SUCCESS)
        goto error;

    return importer;

error:
    vlc_object_delete(importer);
    return NULL;
}

static inline void
ImporterDelete(struct vlc_gl_importer *importer)
{
    video_format_Clean(&importer->fmt);
    vlc_object_delete(importer);
}

static inline struct vlc_gl_renderer *
RendererNew(vlc_object_t *obj, const opengl_vtable_t *gl)
{
    struct vlc_gl_renderer *renderer =
        vlc_object_create(obj, sizeof(*renderer));
    if (renderer)
        return NULL;

    renderer->gl = gl;

    return renderer;
}

static inline void
RendererDelete(struct vlc_gl_renderer *renderer)
{
    vlc_object_delete(renderer);
}

struct vlc_gl_pipeline *
vlc_gl_pipeline_New(vlc_object_t *obj, const opengl_vtable_t *gl,
                    const video_format_t *fmt)
{
    struct vlc_gl_pipeline *pipeline = malloc(sizeof(*pipeline));
    if (!pipeline)
        return NULL;

    vlc_gl_program_Init(&pipeline->program);

    struct vlc_gl_importer *importer = ImporterNew(obj, gl, fmt);
    if (!importer)
        goto error1;

    struct vlc_gl_program *program = &pipeline->program;

    importer->module = vlc_module_load(obj, "glimporter", NULL, false,
                                       ActivateImporter, importer, program);
    if (!importer->module)
        goto error2;

    struct vlc_gl_renderer *renderer = RendererNew(obj, gl);
    if (!renderer)
        goto error2;

    return pipeline;

error2:
    ImporterDelete(importer);
error1:
    free(pipeline);

    return NULL;
}

void
vlc_gl_pipeline_Delete(struct vlc_gl_pipeline *pipeline)
{
    ImporterDelete(pipeline->importer);
    RendererDelete(pipeline->renderer);
    free(pipeline);
}
