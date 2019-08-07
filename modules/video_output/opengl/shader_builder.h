/*****************************************************************************
 * shader_builder.h: shader code API and interaction with tex_converters
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

#ifndef VLC_OPENGL_SHADER_BUILDER_H
#define VLC_OPENGL_SHADER_BUILDER_H

#include <vlc_common.h>
#include <GL/gl.h>

struct opengl_tex_converter_t;
struct opengl_vtable_t;
typedef struct opengl_vtable_t opengl_vtable_t;

/**
 * Enumerate the different kind of linkable shader
 */
enum vlc_gl_shader_type
{
    VLC_GL_SHADER_VERTEX,
    VLC_GL_SHADER_FRAGMENT,
# define VLC_GL_SHADER_TYPE_COUNT ((int)VLC_GL_SHADER_FRAGMENT+1)
};


struct vlc_gl_shader_source_attachment
{
    const char *source;
    const char *name_pixel;

    bool dump_errors;
};

/* Eg. vanilla opengl, glslang
 * TODO: handling multiple API */
struct vlc_gl_shader_builder
{
    vlc_object_t obj;
    module_t *module;

    GLuint // TODO: use correct type
    shaders[VLC_GL_SHADER_TYPE_COUNT];

    const char *header;

    const opengl_vtable_t *vt;
    struct vlc_gl_shader_sampler *sampler;
    struct opengl_tex_converter_t *tc;
};

struct opengl_tex_converter_t;

/* Eg. placebo sampler module, vanilla sampler module */
struct vlc_gl_shader_sampler
{
    vlc_object_t obj;
    module_t *module;
    /* TODO fmt_out */
};

struct vlc_gl_shader_program
{
    GLuint id;
    struct vlc_gl_shader_sampler *sampler;
    struct opengl_tex_converter_t *tc;
    struct opengl_vtable_t *vt;
};

struct vlc_gl_texcoords
{
    struct
    {
        const char *header;
        const char *body;
    } code;
};

struct vlc_gl_shader_builder *
vlc_gl_shader_builder_Create(
    const opengl_vtable_t *vt,
    struct opengl_tex_converter_t *tc,
    struct vlc_gl_shader_sampler *sampler);

void vlc_gl_shader_builder_Release(
    struct vlc_gl_shader_builder *builder);

int vlc_gl_shader_AttachShaderSource(
    struct vlc_gl_shader_builder *builder,
    enum vlc_gl_shader_type shader_type,
    const char *header,
    const char *body);

struct vlc_gl_shader_program*
vlc_gl_shader_program_Create(struct vlc_gl_shader_builder *builder);

void
vlc_gl_shader_program_Release(struct vlc_gl_shader_program *builder);

GLuint vlc_gl_shader_program_GetId(struct vlc_gl_shader_program *program);

#endif
