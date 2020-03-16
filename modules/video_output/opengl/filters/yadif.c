/*****************************************************************************
 * yadif.c
 *****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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

#include "../filter.h"
#include "../gl_api.h"
#include "../gl_common.h"
#include "../gl_util.h"

struct program_copy {
    GLuint id;
    GLuint vbo;
    GLuint framebuffer;
    struct {
        GLint vertex_pos;
    } loc;
};

struct program_yadif {
    GLuint id;
    GLuint vbo;
    GLuint textures[3];
    struct {
        GLint vertex_pos;
        GLint prev;
        GLint cur;
        GLint next;
    } loc;
};

struct sys {
    struct program_copy program_copy;
    struct program_yadif program_yadif;
};

static void
CopyInput(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    const opengl_vtable_t *vt = &filter->api->vt;
    struct program_copy *prog = &sys->program_copy;

    vt->UseProgram(prog->id);

    struct vlc_gl_sampler *sampler = vlc_gl_filter_GetSampler(filter);
    vlc_gl_sampler_Load(sampler);

    vt->BindBuffer(GL_ARRAY_BUFFER, prog->vbo);
    vt->EnableVertexAttribArray(prog->loc.vertex_pos);
    vt->VertexAttribPointer(prog->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0,
                            (const void *) 0);

    vt->Clear(GL_COLOR_BUFFER_BIT);
    vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static inline GLuint
GetReadFramebuffer(const opengl_vtable_t *vt)
{
    GLint value;
    vt->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &value);
    return value; /* as GLuint */
}

static inline void
SetReadFramebuffer(const opengl_vtable_t *vt, GLuint read_fb)
{
    vt->BindFramebuffer(GL_READ_FRAMEBUFFER, read_fb);
}

static inline GLuint
GetDrawFramebuffer(const opengl_vtable_t *vt)
{
    GLint value;
    vt->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
    return value; /* as GLuint */
}

static inline void
SetDrawFramebuffer(const opengl_vtable_t *vt, GLuint draw_fb)
{
    vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);
}

static int
Draw(struct vlc_gl_filter *filter, const struct vlc_gl_input_meta *meta)
{
    (void) meta;

    struct sys *sys = filter->sys;
    const opengl_vtable_t *vt = &filter->api->vt;

    GLuint read_fb = GetReadFramebuffer(vt);
    GLuint draw_fb = GetDrawFramebuffer(vt);

    vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, sys->program_copy.framebuffer);
    vt->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, sys->program_yadif.textures[0], 0);

    CopyInput(filter);

    vt->BindFramebuffer(GL_READ_FRAMEBUFFER, sys->program_copy.framebuffer);
    vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);

    struct program_yadif *prog = &sys->program_yadif;
    vt->UseProgram(prog->id);

    vt->ActiveTexture(GL_TEXTURE0);
    vt->BindTexture(GL_TEXTURE_2D, prog->textures[0]);
    vt->Uniform1i(prog->loc.cur, 0);

    vt->BindBuffer(GL_ARRAY_BUFFER, prog->vbo);
    vt->EnableVertexAttribArray(prog->loc.vertex_pos);
    vt->VertexAttribPointer(prog->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0,
                            (const void *) 0);

    vt->Clear(GL_COLOR_BUFFER_BIT);
    vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    return VLC_SUCCESS;
}

#ifdef USE_OPENGL_ES2
# define SHADER_VERSION "#version 100\n"
# define FRAGMENT_SHADER_PRECISION "precision highp float;\n"
#else
# define SHADER_VERSION "#version 120\n"
# define FRAGMENT_SHADER_PRECISION
#endif

static int
InitProgramCopy(struct vlc_gl_filter *filter)
{
    static const char *const VERTEX_SHADER =
        SHADER_VERSION
        "attribute vec2 vertex_pos;\n"
        "varying vec2 tex_coords;\n"
        "void main() {\n"
        "  gl_Position = vec4(vertex_pos, 0.0, 1.0);\n"
        "  tex_coords = vec2((vertex_pos.x + 1.0) / 2.0,\n"
        "                    (vertex_pos.y + 1.0) / 2.0);\n"
        "}\n";

    static const char *const FRAGMENT_SHADER_TEMPLATE =
        SHADER_VERSION
        "%s\n" /* extensions */
        FRAGMENT_SHADER_PRECISION
        "%s\n" /* vlc_texture definition */
        "varying vec2 tex_coords;\n"
        "void main() {\n"
        "  gl_FragColor = vlc_texture(tex_coords);\n"
        "}\n";

    struct sys *sys = filter->sys;
    struct program_copy *prog = &sys->program_copy;
    const opengl_vtable_t *vt = &filter->api->vt;

    struct vlc_gl_sampler *sampler = vlc_gl_filter_GetSampler(filter);

    const char *extensions = sampler->shader.extensions
                           ? sampler->shader.extensions : "";

    char *fragment_shader;
    int ret = asprintf(&fragment_shader, FRAGMENT_SHADER_TEMPLATE, extensions,
                       sampler->shader.body);
    if (ret < 0)
        return VLC_ENOMEM;

    GLuint program_id =
        vlc_gl_BuildProgram(VLC_OBJECT(filter), vt,
                            1, (const char **) &VERTEX_SHADER,
                            1, (const char **) &fragment_shader);

    free(fragment_shader);
    if (!program_id)
        return VLC_EGENERIC;

    vlc_gl_sampler_FetchLocations(sampler, program_id);

    prog->id = program_id;

    prog->loc.vertex_pos = vt->GetAttribLocation(program_id, "vertex_pos");
    assert(prog->loc.vertex_pos != -1);

    vt->GenBuffers(1, &prog->vbo);
    vt->GenFramebuffers(1, &prog->framebuffer);

    static const GLfloat vertex_pos[] = {
        -1,  1,
        -1, -1,
         1,  1,
         1, -1,
    };

    vt->BindBuffer(GL_ARRAY_BUFFER, prog->vbo);
    vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertex_pos), vertex_pos,
                   GL_STATIC_DRAW);

    vt->BindBuffer(GL_ARRAY_BUFFER, 0);

    return VLC_SUCCESS;
}

static int
InitProgramYadif(struct vlc_gl_filter *filter)
{
    static const char *const VERTEX_SHADER =
        SHADER_VERSION
        "attribute vec2 vertex_pos;\n"
        "varying vec2 tex_coords;\n"
        "void main() {\n"
        "  gl_Position = vec4(vertex_pos, 0.0, 1.0);\n"
        "  tex_coords = vec2((vertex_pos.x + 1.0) / 2.0,\n"
        "                    (vertex_pos.y + 1.0) / 2.0);\n"
        "}\n";

    static const char *const FRAGMENT_SHADER =
        SHADER_VERSION
        FRAGMENT_SHADER_PRECISION
        "varying vec2 tex_coords;\n"
        "uniform sampler2D prev;\n"
        "uniform sampler2D cur;\n"
        "uniform sampler2D next;\n"
        "void main() {\n"
        "  vec3 v = texture2D(prev, tex_coords).rgb;\n"
        "  v += texture2D(cur, tex_coords).rgb;\n"
        "  v += texture2D(next, tex_coords).rgb;\n"
        "  gl_FragColor = vec4(v / 3.0, 1.0);\n"
        "}\n";

    struct sys *sys = filter->sys;
    struct program_yadif *prog = &sys->program_yadif;
    const opengl_vtable_t *vt = &filter->api->vt;

    GLuint program_id =
        vlc_gl_BuildProgram(VLC_OBJECT(filter), vt,
                            1, (const char **) &VERTEX_SHADER,
                            1, (const char **) &FRAGMENT_SHADER);

    if (!program_id)
        return VLC_EGENERIC;

    prog->id = program_id;

    prog->loc.vertex_pos = vt->GetAttribLocation(program_id, "vertex_pos");
    assert(prog->loc.vertex_pos != -1);

    prog->loc.prev = vt->GetUniformLocation(program_id, "prev");
    assert(prog->loc.prev != -1);

    prog->loc.prev = vt->GetUniformLocation(program_id, "cur");
    assert(prog->loc.cur != -1);

    prog->loc.prev = vt->GetUniformLocation(program_id, "next");
    assert(prog->loc.next != -1);

    vt->GenBuffers(1, &prog->vbo);

    static const GLfloat vertex_pos[] = {
        -1,  1,
        -1, -1,
         1,  0.5, //FIXME
         1, -1,
    };

    vt->BindBuffer(GL_ARRAY_BUFFER, prog->vbo);
    vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertex_pos), vertex_pos,
                   GL_STATIC_DRAW);

    vt->BindBuffer(GL_ARRAY_BUFFER, 0);

    return VLC_SUCCESS;
}

static void
InitTextures(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    struct program_yadif *prog = &sys->program_yadif;
    const opengl_vtable_t *vt = &filter->api->vt;

    struct vlc_gl_sampler *sampler = vlc_gl_filter_GetSampler(filter);
    unsigned width = sampler->fmt->i_visible_width;
    unsigned height = sampler->fmt->i_visible_height;

    vt->GenTextures(3, prog->textures);

    for (int i = 0; i < 3; ++i)
    {
        vt->BindTexture(GL_TEXTURE_2D, prog->textures[i]);
        vt->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, NULL);
        vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

static void
DestroyProgramCopy(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    struct program_copy *prog = &sys->program_copy;
    const opengl_vtable_t *vt = &filter->api->vt;

    vt->DeleteProgram(prog->id);
    vt->DeleteFramebuffers(1, &prog->framebuffer);
    vt->DeleteBuffers(1, &prog->vbo);
}

static void
DestroyProgramYadif(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    struct program_yadif *prog = &sys->program_yadif;
    const opengl_vtable_t *vt = &filter->api->vt;
    vt->DeleteProgram(prog->id);
    vt->DeleteBuffers(1, &prog->vbo);
}

static void
Close(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    const opengl_vtable_t *vt = &filter->api->vt;

    DestroyProgramYadif(filter);
    DestroyProgramCopy(filter);
    vt->DeleteTextures(3, sys->program_yadif.textures);

    free(sys);
}

static vlc_gl_filter_open_fn Open;
static int
Open(struct vlc_gl_filter *filter, const config_chain_t *config,
     struct vlc_gl_tex_size *size_out)
{
    (void) config;
    (void) size_out;

    struct sys *sys = filter->sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_EGENERIC;

    int ret = InitProgramCopy(filter);
    if (ret != VLC_SUCCESS)
        goto error1;

    ret = InitProgramYadif(filter);
    if (ret != VLC_SUCCESS)
        goto error2;

    InitTextures(filter);

    filter->config.msaa_level = 4;

    static const struct vlc_gl_filter_ops ops = {
        .draw = Draw,
        .close = Close,
    };
    filter->ops = &ops;

    return VLC_SUCCESS;

error2:
    DestroyProgramCopy(filter);
error1:
    free(sys);
    return VLC_EGENERIC;
}

vlc_module_begin()
    set_shortname("yadif")
    set_description("OpenGL yadif deinterlace filter")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callback(Open)
    add_shortcut("yadif")
vlc_module_end()
