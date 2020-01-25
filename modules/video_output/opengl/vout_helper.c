/*****************************************************************************
 * vout_helper.c: OpenGL and OpenGL ES output common code
 *****************************************************************************
 * Copyright (C) 2004-2016 VLC authors and VideoLAN
 * Copyright (C) 2009, 2011 Laurent Aimar
 *
 * Authors: Laurent Aimar <fenrir _AT_ videolan _DOT_ org>
 *          Ilkka Ollakka <ileoo@videolan.org>
 *          Rémi Denis-Courmont
 *          Adrien Maglo <magsoft at videolan dot org>
 *          Felix Paul Kühne <fkuehne at videolan dot org>
 *          Pierre d'Herbemont <pdherbemont at videolan dot org>
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
#include <math.h>

#include <vlc_common.h>
#include <vlc_subpicture.h>
#include <vlc_opengl.h>
#include <vlc_modules.h>
#include <vlc_vout.h>
#include <vlc_viewpoint.h>

#include "vout_helper.h"
#include "internal.h"
#include "renderer.h"
#include "sub_renderer.h"

/* FIXME: GL_ASSERT_NOERROR disabled for now because:
 * Proper GL error handling need to be implemented
 * glClear(GL_COLOR_BUFFER_BIT) throws a GL_INVALID_FRAMEBUFFER_OPERATION on macOS
 * assert fails on vout_display_opengl_Delete on iOS
 */
#if 0
# define HAVE_GL_ASSERT_NOERROR
#endif

#ifdef HAVE_GL_ASSERT_NOERROR
# define GL_ASSERT_NOERROR() do { \
    GLenum glError = vgl->vt.GetError(); \
    switch (glError) \
    { \
        case GL_NO_ERROR: break; \
        case GL_INVALID_ENUM: assert(!"GL_INVALID_ENUM"); \
        case GL_INVALID_VALUE: assert(!"GL_INVALID_VALUE"); \
        case GL_INVALID_OPERATION: assert(!"GL_INVALID_OPERATION"); \
        case GL_INVALID_FRAMEBUFFER_OPERATION: assert(!"GL_INVALID_FRAMEBUFFER_OPERATION"); \
        case GL_OUT_OF_MEMORY: assert(!"GL_OUT_OF_MEMORY"); \
        default: assert(!"GL_UNKNOWN_ERROR"); \
    } \
} while(0)
#else
# define GL_ASSERT_NOERROR()
#endif

struct vout_display_opengl_t {

    vlc_gl_t   *gl;
    opengl_vtable_t vt;

    struct vlc_gl_renderer *renderer;
    struct vlc_gl_sub_renderer *sub_renderer;
};

static const vlc_fourcc_t gl_subpicture_chromas[] = {
    VLC_CODEC_RGBA,
    0
};

int
vout_display_opengl_GenTextures(const struct vlc_gl_interop *interop,
                                const GLsizei *tex_width,
                                const GLsizei *tex_height, GLuint *textures)
{
    interop->vt->GenTextures(interop->tex_count, textures);

    for (unsigned i = 0; i < interop->tex_count; i++)
    {
        interop->vt->BindTexture(interop->tex_target, textures[i]);

#if !defined(USE_OPENGL_ES2)
        /* Set the texture parameters */
        interop->vt->TexParameterf(interop->tex_target, GL_TEXTURE_PRIORITY, 1.0);
        interop->vt->TexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
#endif

        interop->vt->TexParameteri(interop->tex_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        interop->vt->TexParameteri(interop->tex_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        interop->vt->TexParameteri(interop->tex_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        interop->vt->TexParameteri(interop->tex_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    if (interop->ops->allocate_textures != NULL)
    {
        int ret = interop->ops->allocate_textures(interop, textures, tex_width, tex_height);
        if (ret != VLC_SUCCESS)
        {
            interop->vt->DeleteTextures(interop->tex_count, textures);
            memset(textures, 0, interop->tex_count * sizeof(GLuint));
            return ret;
        }
    }
    return VLC_SUCCESS;
}

void
vout_display_opengl_DelTextures(const struct vlc_gl_interop *interop,
                                GLuint *textures)
{
    interop->vt->DeleteTextures(interop->tex_count, textures);
    memset(textures, 0, interop->tex_count * sizeof(GLuint));
}

struct vlc_gl_interop *
vout_display_opengl_CreateInterop(struct vlc_gl_t *gl,
                                  const opengl_vtable_t *vt,
                                  vlc_video_context *context,
                                  const video_format_t *fmt, bool subpics)
{
    const char *glexts = (const char *) vt->GetString(GL_EXTENSIONS);
    assert(glexts);
    if (!glexts)
    {
        msg_Err(gl, "glGetString returned NULL");
        return NULL;
    }

    struct vlc_gl_interop *interop = vlc_gl_interop_New(gl, vt);
    if (!interop)
        return NULL;

#ifdef USE_OPENGL_ES2
    interop->is_gles = true;
#else
    interop->is_gles = false;
#endif

    interop->init = opengl_interop_init_impl;
    interop->ops = NULL;
    interop->glexts = glexts;
    interop->fmt = *fmt;
    /* this is the only allocated field, and we don't need it */
    interop->fmt.p_palette = NULL;


    int ret;
    if (subpics)
    {
        interop->fmt.i_chroma = VLC_CODEC_RGB32;
        /* Normal orientation and no projection for subtitles */
        interop->fmt.orientation = ORIENT_NORMAL;
        interop->fmt.projection_mode = PROJECTION_MODE_RECTANGULAR;
        interop->fmt.primaries = COLOR_PRIMARIES_UNDEF;
        interop->fmt.transfer = TRANSFER_FUNC_UNDEF;
        interop->fmt.space = COLOR_SPACE_UNDEF;

        ret = opengl_interop_generic_init(interop, false);
    }
    else
    {
        const vlc_chroma_description_t *desc =
            vlc_fourcc_GetChromaDescription(fmt->i_chroma);

        if (desc == NULL)
        {
            vlc_object_delete(interop);
            return NULL;
        }
        if (desc->plane_count == 0)
        {
            /* Opaque chroma: load a module to handle it */
            interop->vctx = context;
            interop->module = module_need_var(interop, "glinterop", "glinterop");
        }

        if (interop->module != NULL)
            ret = VLC_SUCCESS;
        else
        {
            /* Software chroma or gl hw converter failed: use a generic
             * converter */
            ret = opengl_interop_generic_init(interop, true);
        }
    }

    if (ret != VLC_SUCCESS)
    {
        vlc_gl_interop_Delete(interop);
        return NULL;
    }

    return interop;
}

static void
ResizeFormatToGLMaxTexSize(video_format_t *fmt, unsigned int max_tex_size)
{
    if (fmt->i_width > fmt->i_height)
    {
        unsigned int const  vis_w = fmt->i_visible_width;
        unsigned int const  vis_h = fmt->i_visible_height;
        unsigned int const  nw_w = max_tex_size;
        unsigned int const  nw_vis_w = nw_w * vis_w / fmt->i_width;

        fmt->i_height = nw_w * fmt->i_height / fmt->i_width;
        fmt->i_width = nw_w;
        fmt->i_visible_height = nw_vis_w * vis_h / vis_w;
        fmt->i_visible_width = nw_vis_w;
    }
    else
    {
        unsigned int const  vis_w = fmt->i_visible_width;
        unsigned int const  vis_h = fmt->i_visible_height;
        unsigned int const  nw_h = max_tex_size;
        unsigned int const  nw_vis_h = nw_h * vis_h / fmt->i_height;

        fmt->i_width = nw_h * fmt->i_width / fmt->i_height;
        fmt->i_height = nw_h;
        fmt->i_visible_width = nw_vis_h * vis_w / vis_h;
        fmt->i_visible_height = nw_vis_h;
    }
}

vout_display_opengl_t *vout_display_opengl_New(video_format_t *fmt,
                                               const vlc_fourcc_t **subpicture_chromas,
                                               vlc_gl_t *gl,
                                               const vlc_viewpoint_t *viewpoint,
                                               vlc_video_context *context)
{
    vout_display_opengl_t *vgl = calloc(1, sizeof(*vgl));
    if (!vgl)
        return NULL;

    vgl->gl = gl;

#if defined(USE_OPENGL_ES2) || defined(HAVE_GL_CORE_SYMBOLS)
#define GET_PROC_ADDR_CORE(name) vgl->vt.name = gl##name
#else
#define GET_PROC_ADDR_CORE(name) GET_PROC_ADDR_EXT(name, true)
#endif
#define GET_PROC_ADDR_EXT(name, critical) do { \
    vgl->vt.name = vlc_gl_GetProcAddress(gl, "gl"#name); \
    if (vgl->vt.name == NULL && critical) { \
        msg_Err(gl, "gl"#name" symbol not found, bailing out"); \
        free(vgl); \
        return NULL; \
    } \
} while(0)
#if defined(USE_OPENGL_ES2)
#define GET_PROC_ADDR(name) GET_PROC_ADDR_CORE(name)
#define GET_PROC_ADDR_CORE_GL(name) GET_PROC_ADDR_EXT(name, false) /* optional for GLES */
#else
#define GET_PROC_ADDR(name) GET_PROC_ADDR_EXT(name, true)
#define GET_PROC_ADDR_CORE_GL(name) GET_PROC_ADDR_CORE(name)
#endif
#define GET_PROC_ADDR_OPTIONAL(name) GET_PROC_ADDR_EXT(name, false) /* GL 3 or more */

    GET_PROC_ADDR_CORE(BindTexture);
    GET_PROC_ADDR_CORE(BlendFunc);
    GET_PROC_ADDR_CORE(Clear);
    GET_PROC_ADDR_CORE(ClearColor);
    GET_PROC_ADDR_CORE(DeleteTextures);
    GET_PROC_ADDR_CORE(DepthMask);
    GET_PROC_ADDR_CORE(Disable);
    GET_PROC_ADDR_CORE(DrawArrays);
    GET_PROC_ADDR_CORE(DrawElements);
    GET_PROC_ADDR_CORE(Enable);
    GET_PROC_ADDR_CORE(Finish);
    GET_PROC_ADDR_CORE(Flush);
    GET_PROC_ADDR_CORE(GenTextures);
    GET_PROC_ADDR_CORE(GetError);
    GET_PROC_ADDR_CORE(GetIntegerv);
    GET_PROC_ADDR_CORE(GetString);
    GET_PROC_ADDR_CORE(PixelStorei);
    GET_PROC_ADDR_CORE(TexImage2D);
    GET_PROC_ADDR_CORE(TexParameterf);
    GET_PROC_ADDR_CORE(TexParameteri);
    GET_PROC_ADDR_CORE(TexSubImage2D);
    GET_PROC_ADDR_CORE(Viewport);

    GET_PROC_ADDR_CORE_GL(GetTexLevelParameteriv);
    GET_PROC_ADDR_CORE_GL(TexEnvf);

    GET_PROC_ADDR(CreateShader);
    GET_PROC_ADDR(ShaderSource);
    GET_PROC_ADDR(CompileShader);
    GET_PROC_ADDR(AttachShader);
    GET_PROC_ADDR(DeleteShader);

    GET_PROC_ADDR(GetProgramiv);
    GET_PROC_ADDR(GetShaderiv);
    GET_PROC_ADDR(GetProgramInfoLog);
    GET_PROC_ADDR(GetShaderInfoLog);

    GET_PROC_ADDR(GetUniformLocation);
    GET_PROC_ADDR(GetAttribLocation);
    GET_PROC_ADDR(VertexAttribPointer);
    GET_PROC_ADDR(EnableVertexAttribArray);
    GET_PROC_ADDR(UniformMatrix4fv);
    GET_PROC_ADDR(UniformMatrix3fv);
    GET_PROC_ADDR(UniformMatrix2fv);
    GET_PROC_ADDR(Uniform4fv);
    GET_PROC_ADDR(Uniform4f);
    GET_PROC_ADDR(Uniform3f);
    GET_PROC_ADDR(Uniform2f);
    GET_PROC_ADDR(Uniform1f);
    GET_PROC_ADDR(Uniform1i);

    GET_PROC_ADDR(CreateProgram);
    GET_PROC_ADDR(LinkProgram);
    GET_PROC_ADDR(UseProgram);
    GET_PROC_ADDR(DeleteProgram);

    GET_PROC_ADDR(ActiveTexture);

    GET_PROC_ADDR(GenBuffers);
    GET_PROC_ADDR(BindBuffer);
    GET_PROC_ADDR(BufferData);
    GET_PROC_ADDR(DeleteBuffers);

    GET_PROC_ADDR_OPTIONAL(GetFramebufferAttachmentParameteriv);

    GET_PROC_ADDR_OPTIONAL(BufferSubData);
    GET_PROC_ADDR_OPTIONAL(BufferStorage);
    GET_PROC_ADDR_OPTIONAL(MapBufferRange);
    GET_PROC_ADDR_OPTIONAL(FlushMappedBufferRange);
    GET_PROC_ADDR_OPTIONAL(UnmapBuffer);
    GET_PROC_ADDR_OPTIONAL(FenceSync);
    GET_PROC_ADDR_OPTIONAL(DeleteSync);
    GET_PROC_ADDR_OPTIONAL(ClientWaitSync);
#undef GET_PROC_ADDR

    GL_ASSERT_NOERROR();

    const char *extensions = (const char *)vgl->vt.GetString(GL_EXTENSIONS);
    assert(extensions);
    if (!extensions)
    {
        msg_Err(gl, "glGetString returned NULL");
        free(vgl);
        return NULL;
    }
#if !defined(USE_OPENGL_ES2)
    const unsigned char *ogl_version = vgl->vt.GetString(GL_VERSION);
    bool supports_shaders = strverscmp((const char *)ogl_version, "2.0") >= 0;
    if (!supports_shaders)
    {
        msg_Err(gl, "shaders not supported, bailing out");
        free(vgl);
        return NULL;
    }
#endif

    /* Resize the format if it is greater than the maximum texture size
     * supported by the hardware */
    GLint       max_tex_size;
    vgl->vt.GetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);

    if ((GLint)fmt->i_width > max_tex_size ||
        (GLint)fmt->i_height > max_tex_size)
        ResizeFormatToGLMaxTexSize(fmt, max_tex_size);

    /* Non-power-of-2 texture size support */
    bool supports_npot;
#if defined(USE_OPENGL_ES2)
    /* OpenGL ES 2 includes support for non-power of 2 textures by specification
     * so checks for extensions are bound to fail. Check for OpenGL ES version instead. */
    supports_npot = true;
#else
    supports_npot = vlc_gl_StrHasToken(extensions, "GL_ARB_texture_non_power_of_two") ||
                    vlc_gl_StrHasToken(extensions, "GL_APPLE_texture_2D_limited_npot");
#endif

    bool b_dump_shaders = var_InheritInteger(gl, "verbose") >= 4;

    vgl->sub_renderer =
        vlc_gl_sub_renderer_New(vgl->gl, &vgl->vt, supports_npot);
    if (!vgl->sub_renderer)
    {
        msg_Err(gl, "Could not create sub renderer");
        free(vgl);
        return NULL;
    }

    GL_ASSERT_NOERROR();
    struct vlc_gl_renderer *renderer = vgl->renderer =
        vlc_gl_renderer_New(vgl->gl, &vgl->vt, context, fmt, supports_npot,
                            b_dump_shaders);
    if (!vgl->renderer)
    {
        msg_Warn(gl, "Could not create renderer for %4.4s",
                 (const char *) &fmt->i_chroma);
        vlc_gl_sub_renderer_Delete(vgl->sub_renderer);
        free(vgl);
        return NULL;
    }

    GL_ASSERT_NOERROR();

    if (renderer->fmt.projection_mode != PROJECTION_MODE_RECTANGULAR
     && vout_display_opengl_SetViewpoint(vgl, viewpoint) != VLC_SUCCESS)
    {
        vout_display_opengl_Delete(vgl);
        return NULL;
    }

    *fmt = renderer->fmt;
    if (subpicture_chromas) {
        *subpicture_chromas = gl_subpicture_chromas;
    }

    GL_ASSERT_NOERROR();
    return vgl;
}

void vout_display_opengl_Delete(vout_display_opengl_t *vgl)
{
    GL_ASSERT_NOERROR();

    /* */
    vgl->vt.Finish();
    vgl->vt.Flush();

    struct vlc_gl_renderer *renderer = vgl->renderer;
    const struct vlc_gl_interop *interop = renderer->interop;
    const size_t main_tex_count = interop->tex_count;
    const bool main_del_texs = !interop->handle_texs_gen;

    vlc_gl_sub_renderer_Delete(vgl->sub_renderer);
    vlc_gl_renderer_Delete(vgl->renderer);

    vgl->vt.DeleteBuffers(1, &renderer->vertex_buffer_object);
    vgl->vt.DeleteBuffers(1, &renderer->index_buffer_object);
    vgl->vt.DeleteBuffers(main_tex_count, renderer->texture_buffer_object);

    if (main_del_texs)
        vgl->vt.DeleteTextures(main_tex_count, renderer->textures);

    GL_ASSERT_NOERROR();

    free(vgl);
}

int vout_display_opengl_SetViewpoint(vout_display_opengl_t *vgl,
                                     const vlc_viewpoint_t *p_vp)
{
    return vlc_gl_renderer_SetViewpoint(vgl->renderer, p_vp);
}

void vout_display_opengl_SetWindowAspectRatio(vout_display_opengl_t *vgl,
                                              float f_sar)
{
    vlc_gl_renderer_SetWindowAspectRatio(vgl->renderer, f_sar);
}

void vout_display_opengl_Viewport(vout_display_opengl_t *vgl, int x, int y,
                                  unsigned width, unsigned height)
{
    vgl->vt.Viewport(x, y, width, height);
}

int vout_display_opengl_Prepare(vout_display_opengl_t *vgl,
                                picture_t *picture, subpicture_t *subpicture)
{
    GL_ASSERT_NOERROR();

    int ret = vlc_gl_renderer_Prepare(vgl->renderer, picture);
    if (ret != VLC_SUCCESS)
        return ret;

    ret = vlc_gl_sub_renderer_Prepare(vgl->sub_renderer, subpicture);
    GL_ASSERT_NOERROR();
    return ret;
}
int vout_display_opengl_Display(vout_display_opengl_t *vgl,
                                const video_format_t *source)
{
    GL_ASSERT_NOERROR();

    /* Why drawing here and not in Render()? Because this way, the
       OpenGL providers can call vout_display_opengl_Display to force redraw.
       Currently, the OS X provider uses it to get a smooth window resizing */

    int ret = vlc_gl_renderer_Draw(vgl->renderer, source);
    if (ret != VLC_SUCCESS)
        return ret;

    ret = vlc_gl_sub_renderer_Draw(vgl->sub_renderer);
    if (ret != VLC_SUCCESS)
        return ret;

    /* Display */
    vlc_gl_Swap(vgl->gl);

    GL_ASSERT_NOERROR();

    return VLC_SUCCESS;
}

