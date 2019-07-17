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
    GLuint *shader_out,
    /* Used for sampling code */
    struct vlc_gl_shader_sampler *sampler,
    /* Used for sampling code and attribute declaration */
    struct opengl_tex_converter_t *tc,
    /* Used for version */
    const char *header,
    /* Actual code of the shader */
    const char **parts,
    /* Number of parts in the shader */
    unsigned int part_count)
{
    const opengl_vtable_t *vt = tc->vt;
    /* TODO: use chroma description or helper ? */
    //unsigned int plane_count = vlc_gl_tc_CountPlanes(tc);

    /* Basic vertex shader */
    //static const char *template =
    //    "#version %u\n"
    //    "varying vec2 TexCoord0;\n"
    //    "attribute vec4 MultiTexCoord0;\n"
    //    "%s%s"
    //    "attribute vec3 VertexPosition;\n"
    //    "uniform mat4 OrientationMatrix;\n"
    //    "uniform mat4 ProjectionMatrix;\n"
    //    "uniform mat4 ZoomMatrix;\n"
    //    "uniform mat4 ViewMatrix;\n"
    //    "void main() {\n"
    //    " TexCoord0 = vec4(OrientationMatrix * MultiTexCoord0).st;\n"
    //    "%s%s"
    //    " gl_Position = ProjectionMatrix * ZoomMatrix * ViewMatrix\n"
    //    "               * vec4(VertexPosition, 1.0);\n"
    //    "}";

    /* Will generate varying and attribute TexCoords as well as assign
     * configuration function, even in non upload mode (framebuffer
     * input/output) */
    // TODO
    //struct vlc_gl_texcoords *texcoords = vlc_gl_tc_GenerateTexcoords(sampler, tc, VLC_GL_SHADER_VERTEX);

    GLuint shader = tc->vt->CreateShader(GL_VERTEX_SHADER);
    if (shader == 0)
    {
        /* TODO: error */
        return VLC_ENOMEM;
    }
    const char **sources = malloc(sizeof(char*) * (part_count + 2));
    sources[0] = header;
    // TODO
    // sources[1] = texcoords->code;

    memcpy(sources + 2, parts, part_count * sizeof (*parts));

    tc->vt->ShaderSource(shader, part_count + 2, sources, NULL);

    /* TODO: check error */
    tc->vt->CompileShader(shader);
    free(sources);

    //TODO
    //vlc_gl_tc_ReleaseTexcoords(texcoords);

    *shader_out = shader;
    return VLC_SUCCESS;
}

static bool BuildFragmentShader(
    GLuint *shader_out,
    /* Used for texture upload code, and sampler and attribute declaration */
    const opengl_tex_converter_t *tc,
    /* Used for sampling code */
    struct vlc_gl_shader_sampler *sampler,
    /* Used for version */
    const char *header,
    /* Actual code of the shader */
    const char **parts,
    /* Number of parts in the shader */
    unsigned int part_count)
{

    /* Will generate varying TexCoords */
    /* TODO: add tc private data in generatetexcoords */
    //struct vlc_gl_texcoords *texcoords = vlc_gl_tc_GenerateTexcoords(tc, VLC_GL_SHADER_FRAGMENT);

    /* Will generate sampling code from previous TexCoords */

    // TODO
    //vlc_gl_tc_ReleaseTexcoords(texcoords);

    return VLC_EGENERIC;
}

struct vlc_gl_shader_builder *
vlc_gl_shader_builder_Create(
    struct opengl_tex_converter_t *tc,
    struct vlc_gl_shader_sampler *sampler)
{
    struct vlc_gl_shader_builder *builder = malloc(sizeof(*builder));
    memset(builder->shaders, 0, sizeof(builder->shaders));
    builder->module = NULL;
    builder->sampler = sampler;
    builder->tc= tc;
    return builder;
}

void vlc_gl_shader_builder_Release(
    struct vlc_gl_shader_builder *builder)
{
    const opengl_vtable_t *vt = builder->tc->vt;
    for (int i=0; i<ARRAY_SIZE(builder->shaders); ++i)
        if (builder->shaders[i] != 0)
            vt->DeleteShader(builder->shaders[i]);

    free(builder);
}


int vlc_gl_shader_AttachShaderSource(
    struct vlc_gl_shader_builder *builder,
    enum vlc_gl_shader_type shader_type
   )
{
    const opengl_vtable_t *vt = builder->tc->vt;
    /* We can only set shader once
     * TODO: find a better way to add sources and be reusable
     * */
    if (builder->shaders[shader_type] != NULL)
        return VLC_EGENERIC;

    GLuint shader = 0;
    int ret = VLC_EGENERIC;

    // TODO
    char *shader_source = NULL;

    /* Call
     * TODO: move this into a module
     */
    switch (shader_type)
    {
        case VLC_GL_SHADER_VERTEX:
            ret = BuildVertexShader(&shader, builder->sampler, builder->tc, builder->header,
                                    &shader_source, 1);
            break;
        case VLC_GL_SHADER_FRAGMENT:
            ret = BuildFragmentShader(&shader, builder->tc, builder->sampler,
                                      builder->header, &shader_source, 1);
            break;
    }

    if (shader == 0 && ret != VLC_SUCCESS)
    {
        /* can't create program */
        return VLC_ENOMEM;
    }

    if (shader != 0 && ret != VLC_SUCCESS)
    {
        /* TODO: info log could be used everytime as it contains useful
         *       information even in case the compilation succeed. */
        GLsizei info_length;
        vt->GetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_length);
        const char *info = malloc(info_length+1);
        vt->GetShaderInfoLog(shader, info_length+1, NULL, info);
        return VLC_EGENERIC;
    }

    builder->shaders[shader_type] = shader;

    return VLC_SUCCESS;
}

struct vlc_gl_shader_program*
vlc_gl_shader_program_Create(struct vlc_gl_shader_builder *builder)
{
    const opengl_vtable_t *vt = builder->tc->vt;

    GLuint program_id = vt->CreateProgram();

    vt->AttachShader(program_id, builder->shaders[VLC_GL_SHADER_FRAGMENT]);
    vt->AttachShader(program_id, builder->shaders[VLC_GL_SHADER_VERTEX]);

    vt->LinkProgram(program_id);

    struct vlc_gl_shader_program *shader_program =
        malloc(sizeof(*shader_program));
    return shader_program;
}

void
vlc_gl_shader_program_Release(struct vlc_gl_shader_program *program)
{
    const opengl_vtable_t *vt = program->tc->vt;
    vt->DeleteProgram(program->id);
    free(program);
}

GLuint vlc_gl_shader_program_GetId(
        struct vlc_gl_shader_program *program)
{
    return program->id;
}
