/*****************************************************************************
 * triangle_rotate.c: triangle config example for opengl
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
#include <vlc_modules.h>
#include <vlc_opengl.h>

#include "../internal.h"
#include "../filter.h"
#include "../converter.h"
#include "../../placebo_utils.h"

struct vlc_gl_filter_sys
{
    struct vlc_gl_shader_program *program;

    GLuint  buffer_objects[3];

    struct {
        GLint VertexPosition;
        GLint VertexColor;
    } aloc;

    struct {
        GLint RotationMatrix;
    } uloc;

    float RotationMatrix[16];
};

static const char *vertex_shader =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec3 Color;\n"
    "attribute vec2 VertexPosition;\n"
    "attribute vec3 VertexColor;\n"
    "uniform mat4 RotationMatrix;\n"
    "void main() {\n"
    " gl_Position = RotationMatrix * vec4(VertexPosition, 0.0, 1.0);\n"
    " Color       = VertexColor;\n"
    "}";

static const char *fragment_shader =
    "#version 100\n"
    "precision highp float;\n"
    "varying vec3 Color;\n"
    "void main() {\n"
    " gl_FragColor = vec4(Color, 0.5);\n"
    "}";

#define TRIANGLE_ROTATE_ANGLE_SHORTTEXT "Set triangle rotation angle"
#define TRIANGLE_ROTATE_ANGLE_LONGTEXT \
    "This parameter controls the rotation angle along the Z axis for the triangle"

#define TRIANGLE_ROTATE_CFG_PREFIX "triangle-"
static const char * const filter_options[] = { "angle", NULL };

static void CleanupVariables(struct vlc_gl_filter *filter)
{
    var_Destroy(filter, TRIANGLE_ROTATE_CFG_PREFIX "angle");
}

static int FilterInput(struct vlc_gl_filter *filter,
                       const struct vlc_gl_filter_input *input)
{
    VLC_UNUSED(input);

    struct vlc_gl_filter_sys *sys = filter->sys;

    GLuint program = vlc_gl_shader_program_GetId(sys->program);

    /* TODO: opengl_tex_converter_t should be handled before as it might need
     *       to inject sampling code into the previous program. */
    filter->vt->UseProgram(program);

    /* TODO: should we handle state in the shader and require that state
     *       stays correct or it is undefined ?
     *       pro: matches newer rendering stateless API */
    filter->vt->Enable(GL_BLEND);
    filter->vt->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const GLfloat vertexCoord[] = {
         0,  1,
        -1, -1,
         1, -1,
    };

    const GLfloat colors[] = {
        1.f, 0.f, 0.f,
        0.f, 1.f, 0.f,
        0.f, 0.f, 1.f,
    };

    filter->vt->EnableVertexAttribArray(sys->aloc.VertexPosition);
    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->buffer_objects[1]);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoord), vertexCoord, GL_STATIC_DRAW);
    filter->vt->VertexAttribPointer(sys->aloc.VertexPosition, 2, GL_FLOAT,
                                    GL_FALSE, 0, 0);

    filter->vt->EnableVertexAttribArray(sys->aloc.VertexColor);
    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->buffer_objects[2]);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_STATIC_DRAW);
    filter->vt->VertexAttribPointer(sys->aloc.VertexColor, 3, GL_FLOAT,
                                    GL_FALSE, 0, 0);

    filter->vt->UniformMatrix4fv(sys->uloc.RotationMatrix, 1, GL_FALSE,
                                 sys->RotationMatrix);

    filter->vt->DrawArrays(GL_TRIANGLES, 0, 3);

    //filter->vt->DisableVertexAttribArray(sys->aloc.VertexColor);
    //filter->vt->DisableVertexAttribArray(sys->aloc.VertexPosition);

    filter->vt->Disable(GL_BLEND);

    return VLC_SUCCESS;
}


static void FilterClose(struct vlc_gl_filter *filter)
{
    struct vlc_gl_filter_sys *sys = filter->sys;
    vlc_gl_shader_program_Release(sys->program);
    filter->vt->DeleteBuffers(3, sys->buffer_objects);

    CleanupVariables(filter);

    free(sys);
}

static int Open(struct vlc_gl_filter *filter,
                const config_chain_t *config,
                video_format_t *fmt_in,
                video_format_t *fmt_out)
{
    struct vlc_gl_filter_sys *sys = filter->sys =
        malloc(sizeof(*sys));

    if (sys == NULL)
        return VLC_ENOMEM;

    int ret = VLC_SUCCESS;
    struct vlc_gl_shader_builder *builder = NULL;

    config_ChainParse(filter, TRIANGLE_ROTATE_CFG_PREFIX,
                      filter_options, config);

    builder = vlc_gl_shader_builder_Create(filter->vt, NULL, NULL);

    if (builder == NULL)
        goto error;

    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_VERTEX, NULL, 0,
                                           &vertex_shader, 1);

    if (ret != VLC_SUCCESS)
        goto error;

    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_FRAGMENT, NULL, 0,
                                           &fragment_shader, 1);
    if (ret != VLC_SUCCESS)
        goto error;

    sys->program = vlc_gl_shader_program_Create(builder);
    vlc_gl_shader_builder_Release(builder);

    if (sys->program == NULL)
        goto error;

    filter->vt->GenBuffers(ARRAY_SIZE(sys->buffer_objects),
                           sys->buffer_objects);

    GLuint program = vlc_gl_shader_program_GetId(sys->program);
    sys->aloc.VertexPosition =
        filter->vt->GetAttribLocation(program, "VertexPosition");
    sys->aloc.VertexColor =
        filter->vt->GetAttribLocation(program, "VertexColor");
    sys->uloc.RotationMatrix =
        filter->vt->GetUniformLocation(program, "RotationMatrix");

    float theta = var_InheritFloat(filter, TRIANGLE_ROTATE_CFG_PREFIX "angle");
    theta = theta * 3.141592f / 180.f;
    float cos_theta = cos(theta);
    float sin_theta = sin(theta);

    memcpy(sys->RotationMatrix, (float[16]) {
        cos_theta,      sin_theta,      0,      0,
        -sin_theta,     cos_theta,      0,      0,
        0,              0,              0,      0,
        0,              0,              0,      1,
    }, sizeof(sys->RotationMatrix));

    filter->filter = FilterInput;
    filter->close = FilterClose;
    filter->info.blend = true;

    fmt_in->i_chroma = VLC_CODEC_RGBA;
    fmt_out->i_chroma = VLC_CODEC_RGBA;

    return VLC_SUCCESS;

error:
    CleanupVariables(filter);
    free(sys);

    return ret == VLC_SUCCESS ? VLC_EGENERIC : ret;
}

vlc_module_begin()
    set_shortname("triangle blend rotated")
    set_description("OpenGL triangle blender with rotation")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callback(Open)
    add_shortcut("triangle_rotate")

    add_float(TRIANGLE_ROTATE_CFG_PREFIX "angle", 0.f,
              TRIANGLE_ROTATE_ANGLE_SHORTTEXT,
              TRIANGLE_ROTATE_ANGLE_LONGTEXT, false)

vlc_module_end()
