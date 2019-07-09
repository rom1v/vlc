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

static bool BuildVertexShader(
    GLuint *shader_out,
    /* Used for sampling code and attribute declaration */
    const opengl_tex_converter_t *tc,
    /* Used for version */
    const char *header,
    /* Actual code of the shader */
    const char **parts,
    /* Number of parts in the shader */
    unsigned int part_count)
{
    /* TODO: use chroma description or helper ? */
    unsigned int plane_count = vlc_gl_tc_CountPlanes(tc);

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
    struct vlc_gl_texcoords *texcoords = vlc_gl_tc_GenerateTexcoords(tc, VLC_GL_SHADER_VERTEX);

    GLuint shader = tc->vt->CreateShader(GL_VERTEX_SHADER);
    if (shader == 0)
    {
        /* TODO: error */
        reutrn VLC_ENOMEM;
    }
    const char * sources[part_count + 2] =
    {
        header,
        texcoords->code,
    };

    memcpy(sources + 2, parts, part_count * sizeof (*parts))

    tc->vt->ShaderSource(shader, part_count + 2, sources, NULL);

    /* TODO better dump parameter and logger */
    if (tc->b_dump_shaders)
        msg_Dbg(tc->gl, "\n=== Vertex shader for fourcc: %4.4s ===\n%s\n",
                (const char *)&tc->fmt.i_chroma, code);
    /* TODO: check error */
    tc->vt->CompileShader(shader);
    free(code);

    vlc_gl_tc_ReleaseTexcoords(texcoords);

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
    struct vlc_gl_texcoords *texcoords = vlc_gl_tc_GenerateTexcoords(tc, VLC_GL_SHADER_FRAGMENT);

    /* Will generate sampling code from previous TexCoords */

    return VLC_EGENERIC;
}

struct opengl_tex_converter_t;

/* Eg. placebo sampler module, vanilla sampler module */
struct vlc_gl_shader_sampler
{
    vlc_object_t obj;
    module_t *module;
    /* TODO fmt_out */
};

enum vlc_gl_shader_type
{
    VLC_GL_SHADER_VERTEX,
    VLC_GL_SHADER_FRAGMENT,
# define VLC_GL_SHADER_TYPE_MAX VLC_GL_SHADER_FRAGMENT
};

/* Eg. vanilla opengl, glslang
 * TODO: handling multiple API */
struct vlc_gl_shader_builder
{
    vlc_object_t obj;
    module_t *module;

    void* // TODO: use correct type
    shaders[VLC_GL_SHADER_TYPE_MAX];
};

struct vlc_gl_shader_builder *
vlc_gl_shader_builder_Create(
    struct opengl_tex_converter_t *tc,
    struct vlc_gl_shader_sampler *sampler)
{
    struct vlc_gl_shader_builder *builder = malloc(sizeof(*builder));
    builder->module = NULL;
    memset(builder->shaders, 0, sizeof(shaders));
    return builder;
}

void vlc_gl_shader_builder_Release(
    struct vlc_gl_shader_builder *builder)
{
    for (int i=0; i<ARRAY_SIZE(builder->shaders); ++i)
        if (builder->shaders[i] != 0)
            glDeleteShader(builder->shaders[i]

    free(builder);
}

struct vlc_gl_shader_source_attachment
{
    const char *source,
    const char *entrypoint,

    bool dump_errors;
};

int vlc_gl_shader_AttachShaderSource(
    struct vlc_gl_shader_builder *builder,
    enum vlc_gl_shader_type shader_type
   )
{
    /* We can only set shader once
     * TODO: find a better way to add sources and be reusable
     * */
    if (builder->shaders[shader_type] != NULL)
        return VLC_EGENERIC;

    GLuint shader = 0;
    int ret = VLC_EGENERIC;

    /* Call
     * TODO: move this into a module
     */
    switch (shader_type)
    {
        case VLC_GL_SHADER_VERTEX:
            ret = BuildVertexShader(&shader, builder->tc, builder->header,
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
        GLsize info_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info);
        const char *infos = malloc(info_length+1);
        glGetShaderInfoLog(shader, info_length+1, NULL, infos);
        return VLC_EGENERIC;
    }

    builder->shaders[shader_type] = shader;

    return VLC_SUCCESS;
}

struct vlc_gl_shader_program*
vlc_gl_shader_program_Create(struct vlc_gl_shader_builder *builder)
{
    GLuint program_id = glCreateProgram();

    glAttachShader(program_id, builder->shaders[VLC_GL_SHADER_FRAGMENT]);
    glAttachShader(program_id, builder->shaders[VLC_GL_SHADER_VERTEX]);

    glLinkProgram(program_id);

    struct vlc_gl_shader_program *shader_program = malloc(sizeof(*program));
    return shader_program;
}

void
vlc_gl_shader_program_Release(struct vlc_gl_shader_program *program)
{
    free(program);
}

GLuint vlc_gl_shader_program_GetId(
        struct vlc_gl_shader_program *program)
{
    return program->id;
}
