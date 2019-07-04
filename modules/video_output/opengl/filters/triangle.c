/*****************************************************************************
 * triangle.c: triangle test drawer for opengl
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
    struct vlc_gl_program sub_prgm;

    GLuint  buffer_objects[2];
};

/* XXX: shouldn't be here, it should either be provided by the module
 *      or by a default implementation */
static GLuint BuildVertexShader(const opengl_tex_converter_t *tc,
                                unsigned plane_count)
{
    /* Basic vertex shader */
    static const char *template =
        "#version %u\n"
        "varying vec2 TexCoord0;\n"
        "attribute vec4 MultiTexCoord0;\n"
        "%s%s"
        "attribute vec3 VertexPosition;\n"
        "uniform mat4 OrientationMatrix;\n"
        "uniform mat4 ProjectionMatrix;\n"
        "uniform mat4 ZoomMatrix;\n"
        "uniform mat4 ViewMatrix;\n"
        "void main() {\n"
        " TexCoord0 = vec4(OrientationMatrix * MultiTexCoord0).st;\n"
        "%s%s"
        " gl_Position = ProjectionMatrix * ZoomMatrix * ViewMatrix\n"
        "               * vec4(VertexPosition, 1.0);\n"
        "}";

    const char *coord1_header = plane_count > 1 ?
        "varying vec2 TexCoord1;\nattribute vec4 MultiTexCoord1;\n" : "";
    const char *coord1_code = plane_count > 1 ?
        " TexCoord1 = vec4(OrientationMatrix * MultiTexCoord1).st;\n" : "";
    const char *coord2_header = plane_count > 2 ?
        "varying vec2 TexCoord2;\nattribute vec4 MultiTexCoord2;\n" : "";
    const char *coord2_code = plane_count > 2 ?
        " TexCoord2 = vec4(OrientationMatrix * MultiTexCoord2).st;\n" : "";

    char *code;
    if (asprintf(&code, template, tc->glsl_version, coord1_header, coord2_header,
                 coord1_code, coord2_code) < 0)
        return 0;

    GLuint shader = tc->vt->CreateShader(GL_VERTEX_SHADER);
    tc->vt->ShaderSource(shader, 1, (const char **) &code, NULL);
    if (tc->b_dump_shaders)
        msg_Dbg(tc->gl, "\n=== Vertex shader for fourcc: %4.4s ===\n%s\n",
                (const char *)&tc->fmt.i_chroma, code);
    tc->vt->CompileShader(shader);
    free(code);
    return shader;
}

/* TODO: refactor into public functions */
static int
opengl_link_program(struct vlc_gl_program *prgm)
{
    opengl_tex_converter_t *tc = prgm->tc;

    /* TODO: replace by user-defined shader */
    GLuint vertex_shader = BuildVertexShader(tc, tc->tex_count);

    /* TODO: the tex_converter should generate sample code and handle
     *       import, not generate shader */
    GLuint shaders[] = { tc->fshader, vertex_shader };

    /* Check shaders messages */
    for (unsigned i = 0; i < 2; i++) {
        int infoLength;
        tc->vt->GetShaderiv(shaders[i], GL_INFO_LOG_LENGTH, &infoLength);
        if (infoLength <= 1)
            continue;

        char *infolog = malloc(infoLength);
        if (infolog != NULL)
        {
            int charsWritten;
            tc->vt->GetShaderInfoLog(shaders[i], infoLength, &charsWritten,
                                      infolog);
            msg_Err(tc->gl, "shader %u: %s", i, infolog);
            free(infolog);
        }
    }

    prgm->id = tc->vt->CreateProgram();
    tc->vt->AttachShader(prgm->id, tc->fshader);
    tc->vt->AttachShader(prgm->id, vertex_shader);
    tc->vt->LinkProgram(prgm->id);

    tc->vt->DeleteShader(vertex_shader);
    tc->vt->DeleteShader(tc->fshader);

    /* Check program messages */
    int infoLength = 0;
    tc->vt->GetProgramiv(prgm->id, GL_INFO_LOG_LENGTH, &infoLength);
    if (infoLength > 1)
    {
        char *infolog = malloc(infoLength);
        if (infolog != NULL)
        {
            int charsWritten;
            tc->vt->GetProgramInfoLog(prgm->id, infoLength, &charsWritten,
                                       infolog);
            msg_Err(tc->gl, "shader program: %s", infolog);
            free(infolog);
        }

        /* If there is some message, better to check linking is ok */
        GLint link_status = GL_TRUE;
        tc->vt->GetProgramiv(prgm->id, GL_LINK_STATUS, &link_status);
        if (link_status == GL_FALSE)
        {
            msg_Err(tc->gl, "Unable to use program");
            goto error;
        }
    }

    /* Fetch UniformLocations and AttribLocations */
#define GET_LOC(type, x, str) do { \
    x = tc->vt->Get##type##Location(prgm->id, str); \
    assert(x != -1); \
    if (x == -1) { \
        msg_Err(tc->gl, "Unable to Get"#type"Location(%s)", str); \
        goto error; \
    } \
} while (0)
#define GET_ULOC(x, str) GET_LOC(Uniform, prgm->uloc.x, str)
#define GET_ALOC(x, str) GET_LOC(Attrib, prgm->aloc.x, str)
    GET_ULOC(OrientationMatrix, "OrientationMatrix");
    GET_ULOC(ProjectionMatrix, "ProjectionMatrix");
    GET_ULOC(ViewMatrix, "ViewMatrix");
    GET_ULOC(ZoomMatrix, "ZoomMatrix");

    GET_ALOC(VertexPosition, "VertexPosition");
    GET_ALOC(MultiTexCoord[0], "MultiTexCoord0");
    /* MultiTexCoord 1 and 2 can be optimized out if not used */
    if (prgm->tc->tex_count > 1)
        GET_ALOC(MultiTexCoord[1], "MultiTexCoord1");
    else
        prgm->aloc.MultiTexCoord[1] = -1;
    if (prgm->tc->tex_count > 2)
        GET_ALOC(MultiTexCoord[2], "MultiTexCoord2");
    else
        prgm->aloc.MultiTexCoord[2] = -1;
#undef GET_LOC
#undef GET_ULOC
#undef GET_ALOC
    int ret = prgm->tc->pf_fetch_locations(prgm->tc, prgm->id);
    assert(ret == VLC_SUCCESS);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(tc->gl, "Unable to get locations from tex_conv");
        goto error;
    }

    return VLC_SUCCESS;

error:
    tc->vt->DeleteProgram(prgm->id);
    prgm->id = 0;
    return VLC_EGENERIC;
}

static void
opengl_deinit_program(struct vlc_gl_filter *filter,
                      struct vlc_gl_program *prgm)
{
    opengl_tex_converter_t *tc = prgm->tc;
    if (tc->p_module != NULL)
        module_unneed(tc, tc->p_module);
    else if (tc->priv != NULL)
        opengl_tex_converter_generic_deinit(tc);
    if (prgm->id != 0)
        filter->vt->DeleteProgram(prgm->id);

#ifdef HAVE_LIBPLACEBO
    FREENULL(tc->uloc.pl_vars);
    if (tc->pl_ctx)
        pl_context_destroy(&tc->pl_ctx);
#endif

    vlc_object_delete(tc);
}

static int
opengl_init_program(struct vlc_gl_filter *filter, vlc_video_context *context,
                    struct vlc_gl_program *prgm, const char *glexts,
                    const video_format_t *fmt, bool subpics,
                    bool b_dump_shaders)
{
    opengl_tex_converter_t *tc =
        vlc_object_create(filter->gl, sizeof(opengl_tex_converter_t));
    if (tc == NULL)
        return VLC_ENOMEM;

    tc->gl = filter->gl;
    tc->vt = filter->vt;
    tc->b_dump_shaders = b_dump_shaders;
    tc->pf_fragment_shader_init = opengl_fragment_shader_init_impl;
    tc->glexts = glexts;
#if defined(USE_OPENGL_ES2)
    tc->is_gles = true;
    tc->glsl_version = 100;
    tc->glsl_precision_header = "precision highp float;\n";
#else
    tc->is_gles = false;
    tc->glsl_version = 120;
    tc->glsl_precision_header = "";
#endif
    tc->fmt = *fmt;

#ifdef HAVE_LIBPLACEBO
    // Create the main libplacebo context
    if (!subpics)
    {
        tc->pl_ctx = vlc_placebo_Create(VLC_OBJECT(tc));
        if (tc->pl_ctx) {
#   if PL_API_VER >= 20
            tc->pl_sh = pl_shader_alloc(tc->pl_ctx, NULL);
#   elif PL_API_VER >= 6
            tc->pl_sh = pl_shader_alloc(tc->pl_ctx, NULL, 0);
#   else
            tc->pl_sh = pl_shader_alloc(tc->pl_ctx, NULL, 0, 0);
#   endif
        }
    }
#endif

    int ret;
    if (subpics)
    {
        tc->fmt.i_chroma = VLC_CODEC_RGB32;
        /* Normal orientation and no projection for subtitles */
        tc->fmt.orientation = ORIENT_NORMAL;
        tc->fmt.projection_mode = PROJECTION_MODE_RECTANGULAR;
        tc->fmt.primaries = COLOR_PRIMARIES_UNDEF;
        tc->fmt.transfer = TRANSFER_FUNC_UNDEF;
        tc->fmt.space = COLOR_SPACE_UNDEF;

        ret = opengl_tex_converter_generic_init(tc, false);
    }
    else
    {
        const vlc_chroma_description_t *desc =
            vlc_fourcc_GetChromaDescription(fmt->i_chroma);

        if (desc == NULL)
        {
            vlc_object_delete(tc);
            return VLC_EGENERIC;
        }
        if (desc->plane_count == 0)
        {
            /* Opaque chroma: load a module to handle it */
            tc->dec_device = context ? context->device : NULL;
            tc->p_module = module_need_var(tc, "glconv", "glconv");
        }

        if (tc->p_module != NULL)
            ret = VLC_SUCCESS;
        else
        {
            /* Software chroma or gl hw converter failed: use a generic
             * converter */
            ret = opengl_tex_converter_generic_init(tc, true);
        }
    }

    if (ret != VLC_SUCCESS)
    {
        vlc_object_delete(tc);
        return VLC_EGENERIC;
    }

    assert(tc->fshader != 0 && tc->tex_target != 0 && tc->tex_count > 0 &&
           tc->pf_update != NULL && tc->pf_fetch_locations != NULL &&
           tc->pf_prepare_shader != NULL);

    prgm->tc = tc;

    ret = opengl_link_program(prgm);
    if (ret != VLC_SUCCESS)
    {
        opengl_deinit_program(filter, prgm);
        return VLC_EGENERIC;
    }

    //getOrientationTransformMatrix(tc->fmt.orientation,
    //                              prgm->var.OrientationMatrix);
    //getViewpointMatrixes(vgl, tc->fmt.projection_mode, prgm);

    return VLC_SUCCESS;
}


static int FilterInput(struct vlc_gl_filter *filter,
                       const struct vlc_gl_filter_input *input)
{

    printf("FILTERING TRIANGLE\n");
    struct vlc_gl_filter_sys *sys = filter->sys;

    /* Draw the subpictures */

    /* TODO: program should be a vlc_gl_program loaded by a shader API */
    struct vlc_gl_program *prgm = &sys->sub_prgm;

    GLuint program = prgm->id;

    /* TODO: opengl_tex_converter_t should be handled before as it might need
     *       to inject sampling code into the previous program. */
    opengl_tex_converter_t *tc = prgm->tc;
    filter->vt->UseProgram(program);

    /* TODO: should we handle state in the shader and require that state
     *       stays correct or it is undefined ?
     *       pro: matches newer rendering stateless API */
    filter->vt->Enable(GL_BLEND);
    filter->vt->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* TODO: enabled texture tracking ? */
    struct vlc_gl_region *glr = &input->picture;
    const GLfloat vertexCoord[] = {
        (glr->left+glr->right)/2,   glr->top,
        glr->right,                 glr->bottom,
        glr->left,                  glr->bottom,
    };
    const GLfloat textureCoord[] = {
        glr->tex_width/2.f, 0.0,
        -.5f, glr->tex_height,
        glr->tex_width, glr->tex_height,
    };

    assert(glr->texture != 0);
    /* TODO: binded texture tracker ? */
    filter->vt->BindTexture(tc->tex_target, glr->texture);

    /* TODO: as above, texture_converter and shaders are dual and this
     *       should be more transparent. */
    tc->pf_prepare_shader(tc, &glr->width, &glr->height, glr->alpha);

    /* TODO: attribute handling in shader ? */
    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->buffer_objects[0]);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STATIC_DRAW);
    filter->vt->EnableVertexAttribArray(prgm->aloc.MultiTexCoord[0]);
    filter->vt->VertexAttribPointer(prgm->aloc.MultiTexCoord[0], 2, GL_FLOAT,
                                    0, 0, 0);

    /* TODO: attribute handling in shader ? */
    filter->vt->BindBuffer(GL_ARRAY_BUFFER, sys->buffer_objects[1]);
    filter->vt->BufferData(GL_ARRAY_BUFFER, sizeof(vertexCoord), vertexCoord, GL_STATIC_DRAW);
    filter->vt->EnableVertexAttribArray(prgm->aloc.VertexPosition);
    filter->vt->VertexAttribPointer(prgm->aloc.VertexPosition, 2, GL_FLOAT,
                                    0, 0, 0);

    /* TODO: where to store this UBO, shader API ? */
    filter->vt->UniformMatrix4fv(prgm->uloc.OrientationMatrix, 1, GL_FALSE,
                                 input->var.OrientationMatrix);
    filter->vt->UniformMatrix4fv(prgm->uloc.ProjectionMatrix, 1, GL_FALSE,
                                 input->var.ProjectionMatrix);
    filter->vt->UniformMatrix4fv(prgm->uloc.ViewMatrix, 1, GL_FALSE,
                                 input->var.ViewMatrix);
    filter->vt->UniformMatrix4fv(prgm->uloc.ZoomMatrix, 1, GL_FALSE,
                                 input->var.ZoomMatrix);

    filter->vt->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    filter->vt->Disable(GL_BLEND);

    return VLC_SUCCESS;
}

static int Open(struct vlc_gl_filter *filter)
{
    struct vlc_gl_filter_sys *sys = filter->sys =
        malloc(sizeof(*sys));
        //vlc_obj_malloc(VLC_OBJECT(filter), sizeof(*sys));

    filter->vt->GenBuffers(ARRAY_SIZE(sys->buffer_objects),
                           sys->buffer_objects);

    const char *extensions = (const char *)filter->vt->GetString(GL_EXTENSIONS);

    opengl_init_program(filter, NULL /* context */,
                        &sys->sub_prgm, extensions,
                        filter->fmt, true, false);

    filter->filter = FilterInput;
    return VLC_SUCCESS;
}

static void Close(struct vlc_gl_filter *filter)
{ }

vlc_module_begin()
    set_shortname("triangle blend")
    set_description("OpenGL triangle blender")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("opengl filter", 0)
    set_callbacks(Open, Close)
    add_shortcut("triangle blend")
vlc_module_end()
