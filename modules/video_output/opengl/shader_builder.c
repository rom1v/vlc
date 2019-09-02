/*****************************************************************************
 * shader_builder.c: shader code API and interaction with tex_converters
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
# include <config.h>
#endif

#include <assert.h>

#include "shader_builder.h"
#include "converter.h"

static int BuildVertexShader(
    const opengl_vtable_t *vt,
    GLuint *shader_out,
    /* Used for sampling code */
    struct vlc_gl_shader_sampler *sampler,
    /* Used for sampling code and attribute declaration */
    struct opengl_tex_converter_t *tc,
    /* Used for version, defines, user-defined uniforms or attributes */
    const char **headers,
    /* Number of parts in the \ref headers array */
    size_t header_count,
    /* Actual code of the shader */
    const char **parts,
    /* Number of parts in the shader */
    size_t part_count)
{
    /* TODO: use chroma description or helper ? chroma description won't give opaque planes count */
    //unsigned int plane_count = vlc_gl_tc_CountPlanes(tc);

    GLuint shader = *shader_out = vt->CreateShader(GL_VERTEX_SHADER);
    if (shader == 0)
    {
        /* TODO: error */
        return VLC_ENOMEM;
    }

    /* Will generate varying and attribute TexCoords as well as assign
     * configuration function, even in non upload mode (framebuffer
     * input/output) */
    // TODO
    //struct vlc_gl_texcoords *texcoords =
    //    vlc_gl_tc_GenerateTexcoords(tc, sampler, VLC_GL_SHADER_VERTEX);

    const char **sources = malloc(sizeof(char*) * (header_count + part_count + 1));
    memcpy(sources, headers, header_count * sizeof(char*));
    memcpy(sources + header_count + 1,  parts, part_count * sizeof(char*));
    // TODO
    // sources[header_count] = texcoords->code;
    sources[header_count] = "";

    vt->ShaderSource(shader, header_count + part_count + 1, sources, NULL);

    /* TODO: check error */
    vt->CompileShader(shader);
    free(sources);

    //TODO
    //vlc_gl_tc_ReleaseTexcoords(texcoords);

    return VLC_SUCCESS;
}

static bool BuildFragmentShader(
    const opengl_vtable_t *vt,
    GLuint *shader_out,
    /* Used for texture upload code, and sampler and attribute declaration */
    const opengl_tex_converter_t *tc,
    /* Used for sampling code */
    struct vlc_gl_shader_sampler *sampler,
    /* Used for version */
    const char **headers,
    /* */
    size_t header_count,
    /* Actual code of the shader */
    const char **parts,
    /* Number of parts in the shader */
    unsigned int part_count)
{

    /* Will generate varying TexCoords */
    /* TODO: add tc private data in generatetexcoords */
    //struct vlc_gl_texcoords *texcoords = vlc_gl_tc_GenerateTexcoords(tc, VLC_GL_SHADER_FRAGMENT);

    GLuint shader = *shader_out = vt->CreateShader(GL_FRAGMENT_SHADER);
    if (shader == 0)
    {
        /* TODO: error */
        return VLC_ENOMEM;
    }

    /* Will generate sampling code from previous TexCoords */

    vt->ShaderSource(shader, part_count, parts, NULL);
    vt->CompileShader(shader);

    // TODO
    //vlc_gl_tc_ReleaseTexcoords(texcoords);
    return VLC_SUCCESS;
}

struct vlc_gl_shader_builder *
vlc_gl_shader_builder_Create(
    const opengl_vtable_t *vt,
    struct opengl_tex_converter_t *tc,
    struct vlc_gl_shader_sampler *sampler)
{
    struct vlc_gl_shader_builder *builder = malloc(sizeof(*builder));
    memset(builder->shaders, 0, sizeof(builder->shaders));
    builder->vt = vt;
    builder->module = NULL;
    builder->sampler = sampler;
    builder->tc= tc;
    builder->header = "";
    return builder;
}

void vlc_gl_shader_builder_Release(
    struct vlc_gl_shader_builder *builder)
{
    const opengl_vtable_t *vt = builder->vt;
    for (int i=0; i<ARRAY_SIZE(builder->shaders); ++i)
        if (builder->shaders[i] != 0)
            vt->DeleteShader(builder->shaders[i]);

    free(builder);
}


int vlc_gl_shader_AttachShaderSource(
    struct vlc_gl_shader_builder *builder,
    enum vlc_gl_shader_type shader_type,
    const char *header,
    const char *body)
{
    const opengl_vtable_t *vt = builder->vt;
    /* We can only set shader once
     * TODO: find a better way to add sources and be reusable
     * */
    if (builder->shaders[shader_type] != 0)
        return VLC_EGENERIC;

    GLuint shader = 0;
    int ret = VLC_EGENERIC;

    const char *headers[] =
    {
        /* Base header for defines and common stuff */
        builder->header,
        /* User-define header data */
        header,
    };

    /* Call
     * TODO: move this into a module
     */
    switch (shader_type)
    {
        case VLC_GL_SHADER_VERTEX:
            ret = BuildVertexShader(builder->vt, &shader, builder->sampler,
                                    builder->tc, headers, 2, &body, 1);
            break;
        case VLC_GL_SHADER_FRAGMENT:
            ret = BuildFragmentShader(builder->vt, &shader, builder->tc,
                                      builder->sampler, headers, 2, &body, 1);
            break;
    }

    if (shader == 0 && ret != VLC_SUCCESS)
    {
        /* can't create program */
        fprintf(stderr, "Can't create shader\n");
        return VLC_ENOMEM;
    }

    /* TODO: info log could be used everytime as it contains useful
     *       information even in case the compilation succeed. */
    GLsizei info_length;
    vt->GetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_length);
    char *info = malloc(info_length+1);
    vt->GetShaderInfoLog(shader, info_length+1, NULL, info);
    fprintf(stderr, "Shader info: %s\n", info);
    free(info);

    GLint success = GL_FALSE;
    if (shader)
        vt->GetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (shader != 0 && (ret != VLC_SUCCESS || success == GL_FALSE))
    {
        fprintf(stderr, "Shader compilation error:\n%s\n", body);
        vt->DeleteShader(shader);
        return VLC_EGENERIC;
    }

    fprintf(stderr, "Shader value : %u\n", shader);

    int error;
    bool has_error = false;
    while ((error = vt->GetError()) != GL_NO_ERROR)
    {
        fprintf(stderr, "Error (%x): %s\n", error, "unknown");
        has_error = true;
    }

    if (has_error)
    {
        vt->DeleteShader(shader);
        return VLC_EGENERIC;
    }

    builder->shaders[shader_type] = shader;

    return VLC_SUCCESS;
}

struct vlc_gl_shader_program*
vlc_gl_shader_program_Create(struct vlc_gl_shader_builder *builder)
{
    const opengl_vtable_t *vt = builder->vt;

    GLuint program_id = vt->CreateProgram();

    vt->AttachShader(program_id, builder->shaders[VLC_GL_SHADER_FRAGMENT]);
    vt->AttachShader(program_id, builder->shaders[VLC_GL_SHADER_VERTEX]);

    vt->LinkProgram(program_id);

    GLint link_status = 0;
    vt->GetProgramiv(program_id, GL_LINK_STATUS, &link_status);

    if (link_status == GL_FALSE)
    {
        GLsizei info_length;
        vt->GetProgramiv(program_id, GL_INFO_LOG_LENGTH, &info_length);
        char *info = malloc(info_length+1);
        vt->GetProgramInfoLog(program_id, info_length+1, NULL, info);
        fprintf(stderr, "Program info: %s\n", info);
        free(info);

        vt->DeleteProgram(program_id);
        return NULL;
    }

    int error;
    while ((error = vt->GetError()) != GL_NO_ERROR)
    {
        fprintf(stderr, "Error (%x): %s\n", error, "unknown");
        return NULL;
    }

    struct vlc_gl_shader_program *shader_program =
        malloc(sizeof(*shader_program));

    shader_program->id = program_id;
    shader_program->vt = vt;

    return shader_program;
}

void
vlc_gl_shader_program_Release(struct vlc_gl_shader_program *program)
{
    const opengl_vtable_t *vt = program->vt;
    vt->DeleteProgram(program->id);
    free(program);
}

GLuint vlc_gl_shader_program_GetId(struct vlc_gl_shader_program *program)
{
    return program->id;
}
