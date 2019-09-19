/*****************************************************************************
 * gpufilter.c: example filter with auxiliary data
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
#include <vlc_list.h>

#include "../internal.h"
#include "../filter.h"
#include "../converter.h"
#include "../../placebo_utils.h"

struct command_node
{
    enum {
        COMMAND_START_TIME, COMMAND_END_TIME, COMMAND_RECT, COMMAND_COLOR
    } type;

    union {
        struct {
            vlc_tick_t time;
            struct command_node *other;
        } timeinfo;

        struct {
            int x, y, w, h;
            struct command_node *timeinfo;
        } rect;

        struct {
            int r, g, b;
            struct command_node *timeinfo;
        } color;
    };

    struct vlc_list node;
};

struct vlc_gl_filter_sys
{
    struct vlc_gl_shader_program *program;

    GLuint  buffer_objects[3];
    struct vlc_list commands;

    struct {
        GLint VertexPosition;
    } aloc;

    struct {
        GLint Color;
    } uloc;

    struct {
        unsigned width;
        unsigned height;
    } source;
};

static const char *vertex_shader =
    "#version 130\n"
    "uniform vec4 Color;\n"
    "attribute vec2 VertexPosition;\n"
    "void main() {\n"
    "    gl_Position = vec4(VertexPosition, 0.0, 1.0);\n"
    "}";

static const char *fragment_shader =
    "#version 130\n"
    "uniform vec4 Color;\n"
    "void main() {\n"
    "    gl_FragColor = Color;\n"
    "}";

static void ParseCommands(struct vlc_gl_filter *filter,
                          FILE *stream)
{
    struct vlc_gl_filter_sys *sys = filter->sys;
    char *line = NULL;
    size_t size;

    struct command_node *start_time = NULL;

    for (;;)
    {
        ssize_t count = getline(&line, &size, stream);

        if (count < 0)
            break;

        if (count == 0)
            continue;

        struct command_node *cmd = malloc(sizeof(*cmd));
        if (cmd == NULL)
        {
            msg_Err(filter, "Allocation error");
            break;
        }

        switch(line[0])
        {
            /* End time / Start time */
            case 'e':
            case 's': {
                vlc_tick_t pts;
                if (sscanf(line+2, "%" PRId64, &pts) != 1)
                {
                    msg_Err(filter, "Error when parsing line: `%s`", line);
                    msg_Err(filter, "Format is: `%c pts`", line[0]);
                    free(cmd);
                    continue;
                }

                cmd->type = (line[0] == 's') ? COMMAND_START_TIME
                                             : COMMAND_END_TIME;
                cmd->timeinfo.time  = pts;
                cmd->timeinfo.other = NULL;

                if (cmd->type == COMMAND_START_TIME)
                    start_time = cmd;
                else if (start_time != NULL)
                {
                    cmd->timeinfo.other = start_time;
                    start_time->timeinfo.other = cmd;
                    start_time = NULL;
                }
                else
                {
                    msg_Err(filter, "Error when parsing end time: no matching start_time");
                    free(cmd);
                    continue;
                }
                break;
            }

            /* Draw rectangle */
            case 'r': {
                if (sscanf(line+2, "%d %d %d %d",
                           &cmd->rect.x, &cmd->rect.y,
                           &cmd->rect.w, &cmd->rect.h) != 4)
                {
                    msg_Err(filter, "Error when parsing line: `%s`", line);
                    msg_Err(filter, "Format is: `r x y w h`");
                    free(cmd);
                    continue;
                }
                cmd->type = COMMAND_RECT;
                cmd->rect.timeinfo = start_time;
                break;
            }

            /* Change color */
            case 'c': {
                if (sscanf(line+2, "%d %d %d", &cmd->color.r,
                           &cmd->color.g, &cmd->color.b) != 3)
                {
                    msg_Err(filter, "Error when parsing line: `%s`", line);
                    msg_Err(filter, "Format is: `c r g b`");
                    free(cmd);
                    continue;
                }
                cmd->type = COMMAND_COLOR;
                cmd->color.timeinfo = start_time;
                break;
            }

            default: {
                msg_Err(filter, "Error when parsing line, unknown command: `%s`", line);
                free(cmd);
                continue;
            }
        }

        vlc_list_append(&cmd->node, &sys->commands);
    }

    free(line);
}

static void DrawRect(struct vlc_gl_filter *filter,
                     GLfloat color[4],
                     struct command_node *cmd)
{
    struct vlc_gl_filter_sys* sys = filter->sys;
    assert(cmd->type == COMMAND_RECT);

    sys->source.width = 2000;
    sys->source.height = 2000;
    msg_Info(filter, "Source: %ux%u", sys->source.width, sys->source.height);

    float left   = 2.f * (cmd->rect.x / (float)sys->source.width  - 0.5f);
    float bottom = 2.f * (cmd->rect.y / (float)sys->source.height - 0.5f);
    float right  = left   + 2.f * (cmd->rect.w / (float)sys->source.width);
    float top    = bottom + 2.f * (cmd->rect.h / (float)sys->source.height);

    const GLfloat vertexCoords[] = {
        left,   bottom,
        right,  bottom,
        left,   top,
        right,  top,
    };

    GLfloat *v = vertexCoords;
    msg_Info(filter, "Coords: %f %f / %f %f / %f %f / "
                     "%f %f / %f %f / %f %f",
             v[0], v[1], v[2], v[3], v[4], v[5],
             v[6], v[7], v[8], v[9], v[10], v[11]);

    filter->vt->EnableVertexAttribArray(sys->aloc.VertexPosition);
    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->buffer_objects[1]);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoords), vertexCoords, GL_STATIC_DRAW);
    filter->vt->VertexAttribPointer(sys->aloc.VertexPosition, 2, GL_FLOAT,
                                    GL_FALSE, 0, 0);

    filter->vt->Uniform4fv(sys->uloc.Color, 1, color);

    filter->vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static int FilterInput(struct vlc_gl_filter *filter,
                       const struct vlc_gl_shader_sampler *sampler,
                       const struct vlc_gl_filter_input *input)
{

    struct vlc_gl_filter_sys *sys = filter->sys;
    VLC_UNUSED(input);

    GLuint program = vlc_gl_shader_program_GetId(sys->program);

    filter->vt->UseProgram(program);

    filter->vt->Enable(GL_BLEND);
    filter->vt->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLfloat current_color[] = { 0.f, 0.f, 0.f, 1.f };

    bool draw = true;

    struct command_node *node;
    vlc_list_foreach(node, &sys->commands, node)
    {
        if (node->type == COMMAND_COLOR)
        {
            memcpy(current_color, (GLfloat[4]) {
                       node->color.r / 255.f,
                       node->color.g / 255.f,
                       node->color.b / 255.f, 1.f}, sizeof(current_color));
            msg_Info(filter, "Changing color to %d,%d,%d",
                     node->color.r, node->color.g, node->color.b);
        }

        else if (node->type == COMMAND_START_TIME &&
            node->timeinfo.other != NULL)
        {
            draw = input->picture_date > node->timeinfo.time &&
                   input->picture_date < node->timeinfo.other->timeinfo.time;
            msg_Info(filter, "Start command for range %" PRId64" -- %" PRId64
                    ", current_time = %" PRId64", %s",
                    node->timeinfo.time, node->timeinfo.other->timeinfo.time,
                    input->picture_date, draw ? "allowing draw" : "not drawing");
        }

        else if (node->type == COMMAND_END_TIME)
        {
            draw = true;
            msg_Info(filter, "End of time range, drawing again");
        }

        else if (node->type == COMMAND_RECT && draw)
        {
            DrawRect(filter, current_color, node);
            msg_Info(filter, "Drawing rectangle: x=%d, y=%d, width=%d, height=%d",
                     node->rect.x, node->rect.y, node->rect.w, node->rect.h);
        }
    }

    filter->vt->Disable(GL_BLEND);

    return VLC_SUCCESS;
}

static void FilterClose(struct vlc_gl_filter *filter)
{
    struct vlc_gl_filter_sys *sys = filter->sys;
    vlc_gl_shader_program_Release(sys->program);
    filter->vt->DeleteBuffers(3, sys->buffer_objects);
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

    vlc_list_init(&sys->commands);

    static const char * const ppsz_options[] = { "cfg", NULL };
    config_ChainParse(filter, "command-blend-", ppsz_options, config);

    int ret = VLC_SUCCESS;

    char *filename = var_InheritString(filter, "command-blend-cfg");
    if (filename == NULL)
    {
        msg_Err(filter, "No configuration file provided");
        free(sys);
        return VLC_EGENERIC;
    }

    FILE *stream = fopen(filename, "r");

    if (stream == NULL)
    {
        msg_Err(filter, "cannot open file %s", "test.cfg");
        free(sys);
        free(filename);
        return VLC_EGENERIC;
    }
    free(filename);

    struct vlc_gl_shader_builder *builder =
        vlc_gl_shader_builder_Create(filter->vt, NULL, NULL);

    if (builder == NULL)
    {
        ret = VLC_ENOMEM;
        goto error;
    }

    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_VERTEX,
                                           NULL, 0, &vertex_shader, 1);

    // TODO: free
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "cannot attach vertex shader");
        return VLC_EGENERIC;
    }

    ret = vlc_gl_shader_AttachShaderSource(builder, VLC_GL_SHADER_FRAGMENT,
                                           NULL, 0, &fragment_shader, 1);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(filter, "cannot attach fragment shader");
        return VLC_EGENERIC;
    }

    sys->program = vlc_gl_shader_program_Create(builder);

    if (sys->program == NULL)
    {
        msg_Err(filter, "cannot create vlc_gl_shader_program");
        return VLC_EGENERIC;
    }

    vlc_gl_shader_builder_Release(builder);

    filter->vt->GenBuffers(ARRAY_SIZE(sys->buffer_objects),
                           sys->buffer_objects);

    GLuint program = vlc_gl_shader_program_GetId(sys->program);
    sys->aloc.VertexPosition =
        filter->vt->GetAttribLocation(program, "VertexPosition");
    sys->uloc.Color =
        filter->vt->GetUniformLocation(program, "Color");

    sys->source.width  = fmt_in->i_visible_width;
    sys->source.height = fmt_in->i_visible_height;

    ParseCommands(filter, stream);

    filter->prepare = NULL;
    filter->filter = FilterInput;
    filter->close = FilterClose;
    filter->info.blend = true;
    fmt_in->i_chroma = fmt_out->i_chroma = VLC_CODEC_RGBA;

    return VLC_SUCCESS;

error:
    return ret == VLC_SUCCESS ? VLC_EGENERIC : ret;
}

vlc_module_begin()
    set_shortname("command blend")
    set_description("OpenGL command blender example")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callback(Open)
    add_shortcut("command_blend")

    add_string("command-blend-cfg", NULL, "", "", false)
vlc_module_end()
