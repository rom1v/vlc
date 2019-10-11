/*****************************************************************************
 * identity.c: OpenGL chroma converter
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_modules.h>
#include <vlc_opengl.h>

#include "../internal.h"
#include "../filter.h"
#include "../chroma_converter.h"
#include "../converter.h"

static const char *const FRAGMENT_CODE =
    "uniform sampler2D tex;\n"
    "vec4 vlc_texture(vec2 c) {\n"
    "  vec2 coords = vlc_picture_coords(c);\n"
    "  return texture2D(tex, coords);\n"
    "}\n";

#define MAX_PLANE_COUNT 4

struct nv12_sys {
    unsigned input_texture_first_index;
    unsigned plane_count;
    GLint planes[MAX_PLANE_COUNT];
};

static int
Prepare(const struct vlc_gl_shader_program *program, void *userdata)
{
    struct vlc_gl_chroma_converter *converter = userdata;
    struct nv12_sys *sys = converter->sys;
    const struct opengl_vtable_t *vt = converter->vt;

    sys->planes[0] = vt->GetUniformLocation(program->id, "planes[0]");

    return VLC_SUCCESS;
}

static int
Load(const struct vlc_gl_picture *pic, void *userdata)
{
    struct vlc_gl_chroma_converter *converter = userdata;
    struct nv12_sys *sys = converter->sys;
    const struct opengl_vtable_t *vt = converter->vt;

    for (unsigned i = 0; i < sys->plane_count; ++i)
    {
        unsigned gltex_index = sys->input_texture_first_index + i;
        vt->ActiveTexture(GL_TEXTURE0 + gltex_index);
        vt->BindTexture(GL_TEXTURE_2D, pic->textures[i]);
        vt->Uniform1i(sys->planes[i], gltex_index);
    }

    return VLC_SUCCESS;
}

static void
Unload(const struct vlc_gl_picture *pic, void *userdata)
{
    (void) pic;

    struct vlc_gl_chroma_converter *converter = userdata;
    struct nv12_sys *sys = converter->sys;
    const struct opengl_vtable_t *vt = converter->vt;

    for (unsigned i = 0; i < sys->plane_count; ++i)
    {
        unsigned gltex_index = sys->input_texture_first_index + i;
        vt->ActiveTexture(GL_TEXTURE0 + gltex_index);
        vt->BindTexture(GL_TEXTURE_2D, 0);
    }
}

static void
Close(struct vlc_gl_chroma_converter *converter)
{
    free(converter->sys);
}

static const struct vlc_gl_chroma_converter_ops ops = {
    .close = Close,
};

static int
Open(struct vlc_gl_chroma_converter *converter,
     const video_format_t *fmt_in,
     const video_format_t *fmt_out,
     int vflip,
     struct vlc_gl_shader_sampler *sampler_out)
{
    if (fmt_in->i_chroma != fmt_out->i_chroma)
        return VLC_EGENERIC;

    const vlc_chroma_description_t *desc =
        vlc_fourcc_GetChromaDescription(fmt_in->i_chroma);
    if (!desc)
        return VLC_EGENERIC;

    // for now, works only when format uses only 1 plane
    if (desc->plane_count != 1)
        return VLC_EGENERIC;

    struct nv12_sys *sys = converter->sys = malloc(sizeof(*sys));
    if (!converter->sys)
        return VLC_ENOMEM;

    char **fragment_codes = vlc_alloc(2, sizeof(*fragment_codes));
    if (!fragment_codes)
    {
        free(converter->sys);
        return VLC_ENOMEM;
    }
    const char *coords = vflip ? FRAGMENT_COORDS_VFLIPPED
                               : FRAGMENT_COORDS_NORMAL;
    fragment_codes[0] = strdup(coords);
    if (!fragment_codes[0])
    {
        free(fragment_codes);
        free(converter->sys);
        return VLC_ENOMEM;
    }
    fragment_codes[1] = strdup(FRAGMENT_CODE);
    if (!fragment_codes[1])
    {
        free(fragment_codes[0]);
        free(fragment_codes);
        free(converter->sys);
        return VLC_ENOMEM;
    }

    sys->input_texture_first_index = 0;
    sys->plane_count = desc->plane_count;

    sampler_out->fragment_codes = fragment_codes;
    sampler_out->fragment_code_count = 1;
    sampler_out->input_texture_first_index = 0;
    sampler_out->input_texture_count = 1;
    sampler_out->prepare = Prepare;
    sampler_out->load = Load;
    sampler_out->unload = Unload;
    sampler_out->userdata = converter;

    converter->ops = &ops;

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_shortname("chroma converter identity")
    set_description("OpenGL identity chroma converter")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl chroma converter", 10000)
    set_callback(Open)
    add_shortcut("glchroma")
vlc_module_end()
