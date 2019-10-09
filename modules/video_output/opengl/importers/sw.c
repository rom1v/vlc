/*****************************************************************************
 * sw.c: OpenGL importer
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

#include "../importer.h"

struct sw_sys {
    int dummy;
};

static int
upload_plane(struct vlc_gl_importer *importer, unsigned idx, GLsizei width,
             GLsizei height, unsigned pitch, unsigned visible_pitch,
             const void *pixels)
{
    struct vlc_gl_tex_cfg *cfg = &importer->cfg[idx];
    const opengl_vtable_t *gl = importer->vt;

    /* This unpack alignment is the default, but setting it just in case. */
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // TODO handle !has_unpack_subimage
    if (visible_pitch == 0)
        visible_pitch = 1;
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, pitch * width / visible_pitch);
    gl->TexSubImage2D(importer->tex_target, 0, 0, 0, width, height, cfg->format,
                      cfg->type, pixels);
    return VLC_SUCCESS;
}

static int
Import(struct vlc_gl_importer *importer, GLuint textures[],
       const GLsizei tex_width[], const GLsizei tex_height[],
       unsigned tex_count, picture_t *pic, const size_t plane_offsets[])
{
    const opengl_vtable_t *gl = importer->vt;

    for (unsigned i = 0; i < tex_count; ++i)
    {
        assert(textures[i]);
        gl->ActiveTexture(GL_TEXTURE0 + i);
        gl->BindTexture(importer->tex_target, textures[i]);

        size_t offset = plane_offsets ? plane_offsets[i] : 0;
        const void *pixels = pic->p[i].p_pixels + offset;

        int ret =
            upload_plane(importer, i, tex_width[i], tex_height[i],
                         pic->p[i].i_pitch, pic->p[i].i_visible_pitch, pixels);
        if (ret != VLC_SUCCESS)
            return ret;
    }
    return VLC_SUCCESS;
}

static void
Close(struct vlc_gl_importer *importer)
{
    free(importer->sys);
}

static const struct vlc_gl_importer_ops ops = {
    .import = Import,
    .close = Close,
};

static int
Open(struct vlc_gl_importer *importer, vlc_fourcc_t fourcc,
     video_color_space_t color_space)
{
    struct sw_sys *sys = importer->sys = malloc(sizeof(*sys));
    if (!importer->sys)
        return VLC_ENOMEM;

    importer->ops = &ops;

    return VLC_SUCCESS;
}

vlc_module_begin ()
    set_description("OpenGL importer generic software")
    set_capability("glimporter", 0)
    set_callback(Open)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    add_shortcut("sw")
vlc_module_end ()
