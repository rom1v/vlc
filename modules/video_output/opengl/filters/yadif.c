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
#include <vlc_picture.h>
#include <vlc_plugin.h>
#include <vlc_modules.h>
#include <vlc_opengl.h>

#include "../filter.h"
#include "../gl_api.h"
#include "../gl_common.h"
#include "../gl_util.h"

#define YADIF_DOUBLE_RATE_SHORTTEXT "Double the framerate"
#define YADIF_DOUBLE_RATE_LONGTEXT \
    "This parameter enabled yadif2x instead of yadif1x"

#define YADIF_CFG_PREFIX "yadif-"

static const char *const filter_options[] = { "double_rate", NULL };

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

    struct {
        GLint vertex_pos;
        GLint prev;
        GLint cur;
        GLint next;
        GLint width;
        GLint height;
    } loc;
};

struct plane {
    /* prev, current and next */
    GLuint textures[3];
    unsigned next; /* next texture index */

    /* In theory, 3 frames are needed.
     * If we only received the first frame, 2 are missing.
     * If we only received the two first frames, 1 is missing.
     */
    unsigned missing_frames;
};

struct sys {
    struct program_copy program_copy;
    struct program_yadif program_yadif;

    struct vlc_gl_sampler *sampler; /* weak reference */

    struct plane planes[PICTURE_PLANE_MAX];
};

static void
CopyInput(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    const opengl_vtable_t *vt = &filter->api->vt;
    struct program_copy *prog = &sys->program_copy;

    vt->UseProgram(prog->id);

    vlc_gl_sampler_Load(sys->sampler);

    vt->BindBuffer(GL_ARRAY_BUFFER, prog->vbo);
    vt->EnableVertexAttribArray(prog->loc.vertex_pos);
    vt->VertexAttribPointer(prog->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0,
                            (const void *) 0);

    vt->Clear(GL_COLOR_BUFFER_BIT);
    vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
    assert(sampler);

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
        "void main() {\n"
        "  gl_Position = vec4(vertex_pos, 0.0, 1.0);\n"
        "}\n";

    // mrefs = y+1
    // prefs = y-1
    // prev2 = prev
    // next2 = cur
    static const char *const FRAGMENT_SHADER =
        SHADER_VERSION
        FRAGMENT_SHADER_PRECISION
        "uniform sampler2D prev;\n"
        "uniform sampler2D cur;\n"
        "uniform sampler2D next;\n"
        "uniform float width;\n"
        "uniform float height;\n"
        "\n"
        "float pix(sampler2D sampler, float x, float y) {\n"
        "  return texture2D(sampler, vec2(x / width, y / height)).x;\n"
        "}\n"
        "\n"
        "float compute_score(float x, float y, float j) {\n"
        "  return abs(pix(cur, x-1+j, y+1) - pix(cur, x-1-j, y-1))\n"
        "       + abs(pix(cur, x  +j, y+1) - pix(cur, x  -j, y-1))\n"
        "       + abs(pix(cur, x+1+j, y+1) - pix(cur, x+1-j, y-1));\n"
        "}\n"
        "\n"
        "float compute_pred(float x, float y, float j) {\n"
        "  return (pix(cur, x+j, y+1) + pix(cur, x-j, y-1)) / 2.0;"
        "}\n"
        "\n"
        "float filter(float x, float y) {\n"
        "  float prev_pix = pix(prev, x, y);\n"
        "  float cur_pix = pix(cur, x, y);\n"
        "  float next_pix = pix(next, x, y);\n"
        "\n"
        "  float c = pix(cur, x, y+1);\n"
        "  float d = (prev_pix + cur_pix) / 2.0;\n"
        "  float e = pix(cur, x, y-1);\n"
        "  float temporal_diff0 = abs(prev_pix - cur_pix) / 2.0;\n"
        "  float temporal_diff1 = (abs(pix(prev, x, y+1) - c)\n"
        "                        + abs(pix(prev, x, y-1) - e)) / 2.0;\n"
        "  float temporal_diff2 = (abs(pix(next, x, y+1) - c)\n"
        "                        + abs(pix(next, x, y-1) - e)) / 2.0;\n"
        "  float diff = max(temporal_diff0,\n"
        "                   max(temporal_diff1, temporal_diff2));\n"
        "  float spatial_pred = (c+e) / 2.0;\n"
        "  float spatial_score = abs(pix(cur, x-1, y+1) - pix(cur, x-1, y-1)) + abs(c-e)\n"
        "                      + abs(pix(cur, x+1, y+1) - pix(cur, x+1, y-1)) - 1.0/256.0;\n"
        "  float score;\n"
        "  score = compute_score(x, y, -1);\n"
        "  if (score < spatial_score) {\n"
        "    spatial_score = score;\n"
        "    spatial_pred = compute_pred(x, y, -1);\n"
        "    score = compute_score(x, y, -2);\n"
        "    if (score < spatial_score) {\n"
        "      spatial_score = score;\n"
        "      spatial_pred = compute_pred(x, y, -2);\n"
        "    }\n"
        "  }\n"
        "  score = compute_score(x, y, 1);\n"
        "  if (score < spatial_score) {\n"
        "    spatial_score = score;\n"
        "    spatial_pred = compute_pred(x, y, 1);\n"
        "    score = compute_score(x, y, 2);\n"
        "    if (score < spatial_score) {\n"
        "       spatial_score = score;\n"
        "       spatial_pred = compute_pred(x, y, 2);\n"
        "    }\n"
        "  }\n"
        "\n"
           // if mode < 2
        "  float b = (pix(prev, x, y+2) + pix(cur, x, y+2)) / 2.0;\n"
        "  float f = (pix(prev, x, y-2) + pix(cur, x, y-2)) / 2.0;\n"
        "  float vmax = max(max(d-e, d-c),\n"
        "                   min(b-c, f-e));\n"
        "  float vmin = min(min(d-e, d-c),\n"
        "                   max(b-c, f-e));\n"
        "  diff = max(diff, max(vmin, -vmax));\n"
           // endif
        "\n"
        "  spatial_pred = min(spatial_pred, d + diff);\n"
        "  spatial_pred = max(spatial_pred, d - diff);\n"
        "  return spatial_pred;\n"
        "}\n"
        "\n"
        "void main() {\n"
           /* bottom-left is (0.5, 0.5)
              top-right is (width-0.5, height-0.5) */
        "  float x = gl_FragCoord.x;\n"
        "  float y = gl_FragCoord.y;\n"
        /* The line number, expressed in non-flipped coordinates */
        "  float line = floor(height - y);\n"
        "\n"
        "  float result;\n"
        "  if (mod(line, 2.0) == 0.0) {\n"
        "    result = pix(cur, x, y);\n"
        "  } else {\n"
        "    result = filter(x, y);\n"
        "  }\n"
        "  gl_FragColor = vec4(result, 0.0, 0.0, 1.0);\n"
        "}\n";

    printf("====\n%s\n====\n", FRAGMENT_SHADER);

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

    prog->loc.cur = vt->GetUniformLocation(program_id, "cur");
    assert(prog->loc.cur != -1);

    prog->loc.next = vt->GetUniformLocation(program_id, "next");
    assert(prog->loc.next != -1);

    prog->loc.width = vt->GetUniformLocation(program_id, "width");
    assert(prog->loc.width != -1);

    prog->loc.height = vt->GetUniformLocation(program_id, "height");
    assert(prog->loc.height != -1);

    vt->GenBuffers(1, &prog->vbo);

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

static void
InitPlane(struct vlc_gl_filter *filter, unsigned plane_idx, GLsizei width,
          GLsizei height)
{
    struct sys *sys = filter->sys;
    const opengl_vtable_t *vt = &filter->api->vt;
    struct plane *plane = &sys->planes[plane_idx];

    plane->next = 0;

    /* The first call to Draw will provide the "next" frame. The "prev" and
     * "cur" frames are missing. */
    plane->missing_frames = 2;

    vt->GenTextures(3, plane->textures);
    for (int i = 0; i < 3; ++i)
    {
        vt->BindTexture(GL_TEXTURE_2D, plane->textures[i]);
        vt->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, NULL);
        vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        vt->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

static void
InitPlanes(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    struct vlc_gl_sampler *sampler = sys->sampler;

    for (unsigned i = 0; i < sampler->tex_count; ++i)
        InitPlane(filter, i, sampler->tex_widths[i], sampler->tex_heights[i]);
}

static void
DestroyPlanes(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    const opengl_vtable_t *vt = &filter->api->vt;
    struct vlc_gl_sampler *sampler = sys->sampler;

    for (unsigned i = 0; i < sampler->tex_count; ++i)
    {
        struct plane *plane = &sys->planes[i];
        vt->DeleteTextures(3, plane->textures);
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

    DestroyPlanes(filter);
    DestroyProgramYadif(filter);
    DestroyProgramCopy(filter);

    free(sys);
}

static void
Flush(struct vlc_gl_filter *filter)
{
    struct sys *sys = filter->sys;
    struct vlc_gl_sampler *sampler = sys->sampler;

    for (unsigned i = 0; i < sampler->tex_count; ++i)
    {
        struct plane *plane = &sys->planes[i];
        /* The next call to Draw will provide the "next" frame. The "prev" and
         * "cur" frames are missing. */
        plane->missing_frames = 2;
    }
}

static inline GLuint
GetDrawFramebuffer(const opengl_vtable_t *vt)
{
    GLint value;
    vt->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &value);
    return value; /* as GLuint */
}

static int
Draw(struct vlc_gl_filter *filter, const struct vlc_gl_input_meta *meta)
{
    struct sys *sys = filter->sys;
    const opengl_vtable_t *vt = &filter->api->vt;

    struct plane *plane = &sys->planes[meta->plane];

    struct program_yadif *prog = &sys->program_yadif;
    unsigned next = plane->next;
    unsigned prev = (next + 1) % 3;
    unsigned cur = (next + 2) % 3;
    plane->next = prev; /* rotate */

    GLuint draw_fb = GetDrawFramebuffer(vt);
    vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, sys->program_copy.framebuffer);
    vt->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, plane->textures[next], 0);

    CopyInput(filter);

    vt->BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);

    if (plane->missing_frames)
    {
        if (plane->missing_frames == 2)
            /* cur is missing */
            cur = next;
        /* prev is missing */
        prev = cur;
        --plane->missing_frames;
    }

    vt->UseProgram(prog->id);

    struct vlc_gl_sampler *sampler = sys->sampler;
    GLsizei width = sampler->tex_widths[meta->plane];
    GLsizei height = sampler->tex_heights[meta->plane];

    vt->Uniform1f(prog->loc.width, width);
    vt->Uniform1f(prog->loc.height, height);

    vt->ActiveTexture(GL_TEXTURE0);
    vt->BindTexture(GL_TEXTURE_2D, plane->textures[prev]);
    vt->Uniform1i(prog->loc.prev, 0);

    vt->ActiveTexture(GL_TEXTURE1);
    vt->BindTexture(GL_TEXTURE_2D, plane->textures[cur]);
    vt->Uniform1i(prog->loc.cur, 1);

    vt->ActiveTexture(GL_TEXTURE2);
    vt->BindTexture(GL_TEXTURE_2D, plane->textures[next]);
    vt->Uniform1i(prog->loc.next, 2);

    vt->BindBuffer(GL_ARRAY_BUFFER, prog->vbo);
    vt->EnableVertexAttribArray(prog->loc.vertex_pos);
    vt->VertexAttribPointer(prog->loc.vertex_pos, 2, GL_FLOAT, GL_FALSE, 0,
                            (const void *) 0);

    vt->Clear(GL_COLOR_BUFFER_BIT);
    vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    return VLC_SUCCESS;
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

    static const struct vlc_gl_filter_ops ops = {
        .draw = Draw,
        .flush = Flush,
        .close = Close,
    };
    filter->ops = &ops;
    filter->config.filter_planes = true;

    config_ChainParse(filter, YADIF_CFG_PREFIX, filter_options, config);

    bool double_rate = var_InheritBool(filter, YADIF_CFG_PREFIX "double_rate");
    // TODO

    sys->sampler = vlc_gl_filter_GetSampler(filter);
    assert(sys->sampler);

    if (sys->sampler->tex_count != 3) {
        msg_Err(filter, "Deinterlace assumes 1 component per plane");
        return false;
    }

    int ret = InitProgramCopy(filter);
    if (ret != VLC_SUCCESS)
        goto error1;

    ret = InitProgramYadif(filter);
    if (ret != VLC_SUCCESS)
        goto error2;

    InitPlanes(filter);

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

    add_bool(YADIF_CFG_PREFIX "double_rate", 0.f,
             YADIF_DOUBLE_RATE_SHORTTEXT,
             YADIF_DOUBLE_RATE_LONGTEXT, false)
vlc_module_end()
