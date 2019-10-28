/*****************************************************************************
 * shader_code.c
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
# include <config.h>
#endif

#include "program_priv.h"

#include <assert.h>

void
vlc_gl_program_Init(struct vlc_gl_program *program)
{
    for (int type = 0; type < VLC_GL_SHADER_TYPE_COUNT_; ++type)
        for (int loc = 0; loc < VLC_GL_SHADER_CODE_LOCATION_COUNT_; ++loc)
            vlc_vector_init(&program->code[type][loc]);
    vlc_vector_init(&program->cbs_reg);
}

int
vlc_gl_program_InitFrom(struct vlc_gl_program *program,
                        struct vlc_gl_program *other)
{
    vlc_gl_program_Init(program);

    for (int type = 0; type < VLC_GL_SHADER_TYPE_COUNT_; ++type)
    {
        for (int loc = 0; loc < VLC_GL_SHADER_CODE_LOCATION_COUNT_; ++loc)
        {
            vec_str *vec = &program->code[type][loc];
            vec_str *ovec = &other->code[type][loc];
            if (!vlc_vector_reserve(vec, ovec->size))
                goto error;
            for (size_t i = 0; i < ovec->size; ++i)
            {
                char *str = strdup(ovec->data[i]);
                if (!str)
                    goto error;
                bool ok = vlc_vector_push(vec, str);
                assert(ok); /* we called vlc_vector_reserve() */
            }
        }
    }

    if (!vlc_vector_push_all(&program->cbs_reg, other->cbs_reg.data,
                             other->cbs_reg.size))
        goto error;

    return VLC_SUCCESS;

error:
    vlc_gl_program_Destroy(program);
    return VLC_ENOMEM;
}

void
vlc_gl_program_Destroy(struct vlc_gl_program *program)
{

    for (int type = 0; type < VLC_GL_SHADER_TYPE_COUNT_; ++type)
    {
        for (int loc = 0; loc < VLC_GL_SHADER_CODE_LOCATION_COUNT_; ++loc)
        {
            vec_str *vec = &program->code[type][loc];
            for (size_t i = 0; i < vec->size; ++i)
                free(vec->data[i]);
            vlc_vector_destroy(vec);
        }
    }
    vlc_vector_destroy(&program->cbs_reg);
}

int
vlc_gl_program_AppendShaderCodeVa(struct vlc_gl_program *program,
                                  enum vlc_gl_shader_type type,
                                  enum vlc_gl_shader_code_location loc,
                                  const char *fmt, va_list ap)
{
    char *str;
    int len = vasprintf(&str, fmt, ap);
    if (len == -1)
        return VLC_ENOMEM;

    bool ok = vlc_vector_push(&program->code[type][loc], str);
    if (!ok)
    {
        free(str);
        return VLC_ENOMEM;
    }

    return VLC_SUCCESS;
}

int
vlc_gl_program_RegisterCallbacks(struct vlc_gl_program *program,
                                 const struct vlc_gl_program_cbs *cbs,
                                 void *userdata)
{
    struct vlc_gl_program_cbs_reg reg = { cbs, userdata };
    bool ok = vlc_vector_push(&program->cbs_reg, reg);
    if (!ok)
        return VLC_ENOMEM;
    return VLC_SUCCESS;
}

int
vlc_gl_program_MergeIn(struct vlc_gl_program *program,
                       struct vlc_gl_program *other)
{
    /* reserve space separately to keep the state consistent on error */
    for (int type = 0; type < VLC_GL_SHADER_TYPE_COUNT_; ++type)
    {
        for (int loc = 0; loc < VLC_GL_SHADER_CODE_LOCATION_COUNT_; ++loc)
        {
            size_t count =
                program->code[type][loc].size + other->code[type][loc].size;
            bool ok = vlc_vector_reserve(&program->code[type][loc], count);
            if (!ok)
                return VLC_ENOMEM;
        }
    }

    {
        size_t count = program->cbs_reg.size + other->cbs_reg.size;
        bool ok = vlc_vector_reserve(&program->cbs_reg, count);
        if (!ok)
            return VLC_ENOMEM;
    }

    for (int type = 0; type < VLC_GL_SHADER_TYPE_COUNT_; ++type)
    {
        for (int loc = 0; loc < VLC_GL_SHADER_CODE_LOCATION_COUNT_; ++loc)
        {
            bool ok = vlc_vector_push_all(&program->code[type][loc],
                                          other->code[type][loc].data,
                                          other->code[type][loc].size);
            assert(ok); /* we called vlc_vector_reserve() beforehand */
            /* the code have been moved out of the "other" structure */
            vlc_vector_clear(&other->code[type][loc]);
        }
    }

    {
        bool ok = vlc_vector_push_all(&program->cbs_reg, other->cbs_reg.data,
                                      other->cbs_reg.size);
        assert(ok);
        vlc_vector_clear(&other->cbs_reg);
    }

    vlc_gl_program_Destroy(other);

    return VLC_SUCCESS;
}

static int
MergeVecs(vec_str *out, vec_str vecs[], size_t count)
{
    vlc_vector_init(out);

    for (size_t i = 0; i < count; ++i)
        if (!vlc_vector_push_all(out, vecs[i].data, vecs[i].size))
            goto error;

    return VLC_SUCCESS;

error:
    vlc_vector_destroy(out);
    return VLC_ENOMEM;
}

static GLuint
LoadShader(const opengl_vtable_t *gl, GLenum type, vec_str *srcs)
{
    GLuint shader = gl->CreateShader(type);
    if (!shader)
        return 0;

    gl->ShaderSource(shader, srcs->size, (const char **) srcs->data, NULL);
    gl->CompileShader(shader);

    GLint compiled;
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        // TODO log error message
        fprintf(stderr, "shader not compiled\n");
        gl->DeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint
vlc_gl_program_Compile(struct vlc_gl_program *program,
                       const opengl_vtable_t *gl)
{
    int program_id = 0;

    vec_str vertex_src;
    int ret = MergeVecs(&vertex_src, program->code[VLC_GL_SHADER_VERTEX],
                        VLC_GL_SHADER_CODE_LOCATION_COUNT_);
    if (ret != VLC_SUCCESS)
        return 0;

    GLuint vertex_shader = LoadShader(gl, GL_VERTEX_SHADER, &vertex_src);
    vlc_vector_destroy(&vertex_src);
    if (!vertex_shader)
        return 0;

    vec_str fragment_src;
    ret = MergeVecs(&fragment_src, program->code[VLC_GL_SHADER_FRAGMENT],
                    VLC_GL_SHADER_CODE_LOCATION_COUNT_);
    if (ret != VLC_SUCCESS)
    {
        gl->DeleteShader(vertex_shader);
        return 0;
    }

    GLuint fragment_shader = LoadShader(gl, GL_FRAGMENT_SHADER, &fragment_src);
    vlc_vector_destroy(&fragment_src);
    if (!fragment_shader)
    {
        gl->DeleteShader(vertex_shader);
        return 0;
    }

    program_id = gl->CreateProgram();
    if (!program_id)
    {
        gl->DeleteShader(fragment_shader);
        gl->DeleteShader(vertex_shader);
        return 0;
    }

    gl->AttachShader(program_id, vertex_shader);
    gl->DeleteShader(vertex_shader);

    gl->AttachShader(program_id, fragment_shader);
    gl->DeleteShader(fragment_shader);

    gl->LinkProgram(program_id);

    GLint linked;
    gl->GetProgramiv(program_id, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        // TODO log error message
        gl->DeleteProgram(program_id);
        fprintf(stderr, "program not linked\n");
        return 0;
    }

    // execute callbacks
    for (size_t i = 0; i < program->cbs_reg.size; ++i)
    {
        struct vlc_gl_program_cbs_reg *reg = &program->cbs_reg.data[i];
        reg->cbs->on_program_compiled(program_id, reg->userdata);
    }

    return program_id;
}

int
vlc_gl_program_PrepareShaders(const struct vlc_gl_program *program)
{
    for (size_t i = 0; i < program->cbs_reg.size; ++i)
    {
        struct vlc_gl_program_cbs_reg *reg = &program->cbs_reg.data[i];
        int ret = reg->cbs->prepare_shaders(reg->userdata);
        if (ret != VLC_SUCCESS)
            return ret;
    }

    return VLC_SUCCESS;
}
