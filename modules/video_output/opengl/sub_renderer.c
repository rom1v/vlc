/*****************************************************************************
 * sub_renderer.c
 *****************************************************************************
 * Copyright (C) 2020 Videolabs
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

#include <assert.h>
#include "gl_common.h"
#include "sub_renderer.h"

typedef struct {
    GLuint   texture;
    GLsizei  width;
    GLsizei  height;

    float    alpha;

    float    top;
    float    left;
    float    bottom;
    float    right;

    float    tex_width;
    float    tex_height;
} gl_region_t;

struct vlc_sub_renderer
{
    vlc_gl_t *gl;
    const opengl_vtable_t *vt;

    int region_count;
    gl_region_t *regions;

    GLuint program_id;
    struct {
        GLint vertex_pos;
        GLint tex_coords_in;
    } aloc;
    struct {
        GLint sampler;
    } uloc;

    GLuint *buffer_objects;
    unsigned buffer_object_count;
};

static void
LogGLInfo(vlc_object_t *obj, const opengl_vtable_t *vt, GLuint id)
{
    GLint info_len;
    vt->GetShaderiv(id, GL_INFO_LOG_LENGTH, &info_len);
    if (info_len)
    {
        char *info_log = malloc(info_len);
        if (info_log)
        {
            GLsizei written;
            vt->GetShaderInfoLog(id, info_len, &written, info_log);
            msg_Err(obj, "shader: %s", info_log);
            free(info_log);
        }
    }
}

static GLuint
CreateShader(vlc_object_t *obj, const opengl_vtable_t *vt, GLenum type,
             const char *src)
{
    GLuint shader = vt->CreateShader(type);
    if (!shader)
        return 0;

    vt->ShaderSource(shader, 1, &src, NULL);
    vt->CompileShader(shader);

    LogGLInfo(obj, vt, shader);

    GLint compiled;
    vt->GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        msg_Err(obj, "Failed to compile shader");
        vt->DeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint
CreateProgram(vlc_object_t *obj, const opengl_vtable_t *vt)
{
    static const char *const VERTEX_SHADER_SRC =
        "#version 100\n"
        "attribute vec2 vertex_pos;\n"
        "attribute vec2 tex_coords_in;\n"
        "varying vec2 tex_coords;\n"
        "void main() {\n"
        "  tex_coords = tex_coords_in;\n"
        "  gl_Position = vec4(vertex_pos, 0.0, 1.0);\n"
        "}\n";

    static const char *const FRAGMENT_SHADER_SRC =
        "#version 100\n"
        "uniform sampler2D sampler\n"
        "varying vec2 tex_coords\n"
        "void main(void) {\n"
        "  gl_FragColor = texture2D(sampler, tex_coords);\n"
        "}\n";

    GLuint program = 0;

    GLuint vertex_shader = CreateShader(obj, vt, GL_VERTEX_SHADER,
                                        VERTEX_SHADER_SRC);
    if (!vertex_shader)
        return 0;

    GLuint fragment_shader = CreateShader(obj, vt, GL_FRAGMENT_SHADER,
                                          FRAGMENT_SHADER_SRC);
    if (!fragment_shader)
        goto finally_1;

    program = vt->CreateProgram();
    if (!program)
        goto finally_2;

    vt->AttachShader(program, vertex_shader);
    vt->AttachShader(program, fragment_shader);

    vt->LinkProgram(program);

    LogGLInfo(obj, vt, program);

    GLint linked;
    vt->GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        msg_Err(obj, "Failed to link program");
        vt->DeleteProgram(program);
        program = 0;
    }

finally_2:
    vt->DeleteShader(fragment_shader);
finally_1:
    vt->DeleteShader(vertex_shader);

    return program;
}

static int
FetchLocations(struct vlc_sub_renderer *sr)
{
    assert(sr->program_id);

    const opengl_vtable_t *vt = sr->vt;

#define GET_LOC(type, x, str) do { \
    x = vt->Get##type##Location(sr->program_id, str); \
    assert(x != -1); \
    if (x == -1) { \
        msg_Err(sr->gl, "Unable to Get"#type"Location(%s)", str); \
        return VLC_EGENERIC; \
    } \
} while (0)
#define GET_ULOC(x, str) GET_LOC(Uniform, x, str)
#define GET_ALOC(x, str) GET_LOC(Attrib, x, str)
    GET_ULOC(sr->uloc.sampler, "sampler");
    GET_ALOC(sr->aloc.vertex_pos, "vertex_pos");
    GET_ALOC(sr->aloc.tex_coords_in, "tex_coords_in");

#undef GET_LOC
#undef GET_ULOC
#undef GET_ALOC

    return VLC_SUCCESS;
}

struct vlc_sub_renderer *
vlc_sub_renderer_New(vlc_gl_t *gl, const opengl_vtable_t *vt)
{
    struct vlc_sub_renderer *sr = malloc(sizeof(*sr));
    if (!sr)
        return NULL;

    sr->gl = gl;
    sr->vt = vt;
    sr->region_count = 0;
    sr->regions = NULL;

    sr->program_id = CreateProgram(VLC_OBJECT(sr->gl), vt);
    if (!sr->program_id)
        return NULL;

    int ret = FetchLocations(sr);
    if (ret != VLC_SUCCESS)
    {
        vt->DeleteProgram(sr->program_id);
        return NULL;
    }

    /* Initial number of allocated buffer objects for subpictures, will grow dynamically. */
    static const unsigned INITIAL_BUFFER_OBJECT_COUNT = 8;
    sr->buffer_objects = vlc_alloc(INITIAL_BUFFER_OBJECT_COUNT, sizeof(GLuint));
    if (!sr->buffer_objects) {
        vt->DeleteProgram(sr->program_id);
        return NULL;
    }
    sr->buffer_object_count = INITIAL_BUFFER_OBJECT_COUNT;

    vt->GenBuffers(sr->buffer_object_count, sr->buffer_objects);

    return sr;
}

void
vlc_sub_renderer_Delete(struct vlc_sub_renderer *sr)
{
    free(sr);
}

int
vlc_sub_renderer_Prepare(struct vlc_sub_renderer *sr, subpicture_t *subpicture)
{

}

int
vlc_sub_renderer_Draw(struct vlc_sub_renderer *sr)
{

}
