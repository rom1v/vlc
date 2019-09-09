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
struct vlc_gl_picture;
struct vlc_gl_shader_program;
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
    const struct vlc_gl_shader_sampler *sampler;
    struct opengl_tex_converter_t *tc;
};

struct vlc_gl_shader_program
{
    GLuint id;
    const struct vlc_gl_shader_sampler *sampler;
    struct opengl_tex_converter_t *tc;
    struct opengl_vtable_t *vt;
};

struct vlc_gl_shader_sampler
{
    /**
     * Fragment shader code defining the function:
     *    vec4 vlc_texture(vec2 coords);
     *
     * Like in the built-in texture(sampler2D, coords), coords.x and coords.y
     * are expressed between 0.0 and 1.0.
     *
     * The fragment codes will be concatenated when the shader source is
     * attached to the OpenGL program.
     */
    char **fragment_codes;

    /** Number of fragment codes */
    size_t fragment_code_count;

    /**
     * Number of textures (or planes) in the input pictures.
     *
     * The chroma will always use the firsts GL_TEXTUREx, so the filter may
     * use textures from GL_TEXTURE{input_texture_count}.
     */
    unsigned input_texture_count;

    /**
     * This function will be called once, after the filter program (containing
     * the injected shader code) is compiled and linked.
     *
     * Its purpose is typically to retrieve uniforms and attributes locations
     * (in particular, the location of the sampler2D uniforms, where the input
     * texture is stored).
     */
    int
    (*prepare)(const struct vlc_gl_shader_program *program, void *userdata);

    /**
     * This function will be called explicitly by the OpenGL filters, for every
     * picture.
     *
     * Its purpose is to load attributes and uniforms. Typically, it will bind
     * the picture textures and load the sampler2D uniforms.
     */
    int
    (*load)(const struct vlc_gl_picture *pic, void *userdata);

    /**
     * This function will be called explicitly by the OpenGL filters, for every
     * picture.
     *
     * Its purpose is to unbind textures.
     */
    void
    (*unload)(const struct vlc_gl_picture *pic, void *userdata);

    /**
     * Opaque pointer passed back to functions.
     */
    void *userdata;

};

static inline int
vlc_gl_shader_sampler_Prepare(const struct vlc_gl_shader_sampler *sampler,
                              const struct vlc_gl_shader_program *program)
{
    if (sampler->prepare)
        return sampler->prepare(program, sampler->userdata);
    return VLC_SUCCESS;
}

static inline int
vlc_gl_shader_sampler_Load(const struct vlc_gl_shader_sampler *sampler,
                           const struct vlc_gl_picture *pic)
{
    if (sampler->load)
        return sampler->load(pic, sampler->userdata);
    return VLC_SUCCESS;
}

static inline void
vlc_gl_shader_sampler_Unload(const struct vlc_gl_shader_sampler *sampler,
                             const struct vlc_gl_picture *pic)
{
    if (sampler->unload)
        sampler->unload(pic, sampler->userdata);
}

static inline void
vlc_gl_shader_sampler_Destroy(struct vlc_gl_shader_sampler *sampler)
{
    for (size_t i = 0; i < sampler->fragment_code_count; ++i)
        free(sampler->fragment_codes[i]);
    free(sampler->fragment_codes);
}

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
    const struct vlc_gl_shader_sampler *sampler);

void vlc_gl_shader_builder_Release(
    struct vlc_gl_shader_builder *builder);

int vlc_gl_shader_AttachShaderSource(
    struct vlc_gl_shader_builder *builder,
    enum vlc_gl_shader_type shader_type,
    const char **headers, size_t header_count,
    const char **bodies, size_t body_count);

struct vlc_gl_shader_program*
vlc_gl_shader_program_Create(struct vlc_gl_shader_builder *builder);

void
vlc_gl_shader_program_Release(struct vlc_gl_shader_program *builder);

GLuint vlc_gl_shader_program_GetId(struct vlc_gl_shader_program *program);

#endif
