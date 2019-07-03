/*****************************************************************************
 * vout_helper.c: OpenGL and OpenGL ES output common code
 *****************************************************************************
 * Copyright (C) 2019 VideoLabs
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

#include "../filter.h"
#include "../converter.h"

struct vlc_gl_filter_sys
{
    struct vlc_gl_program *sub_prgm;

    int     buffer_object_count;
    GLuint *buffer_objects;
};

static int FilterInput(struct vlc_gl_filter *filter,
                       const struct vlc_gl_filter_input *input)
{
    struct vlc_gl_filter_sys *sys = filter->sys;

    /* Draw the subpictures */

    /* TODO: program should be a vlc_gl_program loaded by a shader API */
    struct vlc_gl_program *prgm = sys->sub_prgm;
    GLuint program = prgm->id;

    /* TODO: opengl_tex_converter_t should be handled before as it might need
     *       to inject sampling code into the previous program. */
    opengl_tex_converter_t *tc = prgm->tc;
    filter->vt->UseProgram(program);

    /* TODO: should we handle state in the shader and require that state
     *       stays correct or it is undefined ?
     *       pro: matches newer rendering stateless API */
    filter->vt->Enable(GL_BLEND);
    filter->vt->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* We need two buffer objects for each region: for vertex and texture coordinates. */
    /* TODO: declare buffer objects */
    if (2 * input->region_count > sys->buffer_object_count) {
        if (sys->buffer_object_count > 0)
            filter->vt->DeleteBuffers(sys->buffer_object_count,
                                      sys->buffer_objects);
        sys->buffer_object_count = 0;

        int new_count = 2 * input->region_count;
        sys->buffer_objects = realloc_or_free(sys->buffer_objects, new_count * sizeof(GLuint));
        if (!sys->buffer_objects)
            return VLC_ENOMEM;

        sys->buffer_object_count = new_count;
        filter->vt->GenBuffers(sys->buffer_object_count,
                               sys->buffer_objects);
    }

    /* TODO: enabled texture tracking ? */
    filter->vt->ActiveTexture(GL_TEXTURE0 + 0);
    for (int i = 0; i < input->region_count; i++) {
        struct vlc_gl_region *glr = &input->regions[i];
        const GLfloat vertexCoord[] = {
            glr->left,  glr->top,
            glr->left,  glr->bottom,
            glr->right, glr->top,
            glr->right, glr->bottom,
        };
        const GLfloat textureCoord[] = {
            0.0, 0.0,
            0.0, glr->tex_height,
            glr->tex_width, 0.0,
            glr->tex_width, glr->tex_height,
        };

        assert(glr->texture != 0);
        /* TODO: binded texture tracker ? */
        filter->vt->BindTexture(tc->tex_target, glr->texture);

        /* TODO: as above, texture_converter and shaders are dual and this
         *       should be more transparent. */
        tc->pf_prepare_shader(tc, &glr->width, &glr->height, glr->alpha);

        /* TODO: attribute handling in shader ? */
        filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->buffer_objects[2 * i]);
        filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STATIC_DRAW);
        filter->vt->EnableVertexAttribArray(prgm->aloc.MultiTexCoord[0]);
        filter->vt->VertexAttribPointer(prgm->aloc.MultiTexCoord[0], 2, GL_FLOAT,
                                        0, 0, 0);

        /* TODO: attribute handling in shader ? */
        filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->buffer_objects[2 * i + 1]);
        filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoord), vertexCoord, GL_STATIC_DRAW);
        filter->vt->EnableVertexAttribArray(prgm->aloc.VertexPosition);
        filter->vt->VertexAttribPointer(prgm->aloc.VertexPosition, 2, GL_FLOAT,
                                        0, 0, 0);

        /* TODO: where to store this UBO, shader API ? */
        filter->vt->UniformMatrix4fv(prgm->uloc.OrientationMatrix, 1, GL_FALSE,
                                    prgm->var.OrientationMatrix);
        filter->vt->UniformMatrix4fv(prgm->uloc.ProjectionMatrix, 1, GL_FALSE,
                                    prgm->var.ProjectionMatrix);
        filter->vt->UniformMatrix4fv(prgm->uloc.ViewMatrix, 1, GL_FALSE,
                                    prgm->var.ViewMatrix);
        filter->vt->UniformMatrix4fv(prgm->uloc.ZoomMatrix, 1, GL_FALSE,
                                    prgm->var.ZoomMatrix);

        filter->vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    filter->vt->Disable(GL_BLEND);

}

static int Open(struct vlc_gl_filter *filter)
{
    filter->filter = FilterInput;
    return VLC_SUCCESS;
}

static void Close(struct vlc_gl_filter *filter)
{ }

vlc_module_begin()
    set_shortname("spu blender")
    set_description("OpenGL subpicture blender")
    set_capability("opengl filter", 0)
    set_callbacks(Open, Close)
vlc_module_end()
