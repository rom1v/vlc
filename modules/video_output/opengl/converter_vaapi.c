/*****************************************************************************
 * converter_vaapi.c: OpenGL VAAPI opaque converter
 *****************************************************************************
 * Copyright (C) 2017 VLC authors and VideoLAN
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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <va/va_drmcommon.h>

#include <vlc_common.h>
#include <vlc_vout_window.h>
#include <vlc_codec.h>

#include "converter.h"
#include "../../hw/vaapi/vlc_vaapi.h"

/* From https://www.khronos.org/registry/OpenGL/extensions/OES/OES_EGL_image.txt
 * The extension is an OpenGL ES extension but can (and usually is) available on
 * OpenGL implementations. */
#ifndef GL_OES_EGL_image
#define GL_OES_EGL_image 1
typedef void *GLeglImageOES;
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum target, GLeglImageOES image);
#endif

struct priv
{
    VADisplay vadpy;
    VASurfaceID *va_surface_ids;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

    unsigned fourcc;
    EGLint drm_fourccs[3];

    struct {
        picture_t *  pic;
        VAImage      va_image;
        VABufferInfo va_buffer_info;
        void *       egl_images[3];
    } last;
};

static EGLImageKHR
vaegl_image_create(const struct vlc_gl_importer *imp, EGLint w, EGLint h,
                   EGLint fourcc, EGLint fd, EGLint offset, EGLint pitch)
{
    EGLint attribs[] = {
        EGL_WIDTH, w,
        EGL_HEIGHT, h,
        EGL_LINUX_DRM_FOURCC_EXT, fourcc,
        EGL_DMA_BUF_PLANE0_FD_EXT, fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, pitch,
        EGL_NONE
    };

    return imp->gl->egl.createImageKHR(imp->gl, EGL_LINUX_DMA_BUF_EXT, NULL,
                                       attribs);
}

static void
vaegl_image_destroy(const struct vlc_gl_importer *imp, EGLImageKHR image)
{
    imp->gl->egl.destroyImageKHR(imp->gl, image);
}

static void
vaegl_release_last_pic(const struct vlc_gl_importer *imp, struct priv *priv)
{
    vlc_object_t *o = VLC_OBJECT(imp->gl);

    for (unsigned i = 0; i < priv->last.va_image.num_planes; ++i)
        vaegl_image_destroy(imp, priv->last.egl_images[i]);

    vlc_vaapi_ReleaseBufferHandle(o, priv->vadpy, priv->last.va_image.buf);

    vlc_vaapi_DestroyImage(o, priv->vadpy, priv->last.va_image.image_id);

    picture_Release(priv->last.pic);
}

static int
vaegl_init_fourcc(const opengl_tex_converter_t *tc, struct priv *priv,
                  unsigned va_fourcc)
{
    (void) tc;
    switch (va_fourcc)
    {
        case VA_FOURCC_NV12:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('G', 'R', '8', '8');
            break;
        case VA_FOURCC_P010:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '1', '6', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('G', 'R', '3', '2');
            break;
#if 0
        /* TODO: the following fourcc are not handled for now */
        case VA_FOURCC_RGBA:
            priv->drm_fourccs[0] = VLC_FOURCC('G', 'R', '3', '2');
            break;
        case VA_FOURCC_BGRA:
            priv->drm_fourccs[0] = VLC_FOURCC('G', 'R', '3', '2');
            break;
        case VA_FOURCC_YV12:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[2] = VLC_FOURCC('R', '8', ' ', ' ');
            break;
        case VA_FOURCC_422H:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('R', '8', ' ', ' ');
            priv->drm_fourccs[2] = VLC_FOURCC('R', '8', ' ', ' ');
            break;
        case VA_FOURCC_UYVY:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '1', '6', ' ');
            break;
        case VA_FOURCC_444P:
            priv->drm_fourccs[0] = VLC_FOURCC('R', '1', '6', ' ');
            priv->drm_fourccs[1] = VLC_FOURCC('R', '1', '6', ' ');
            priv->drm_fourccs[2] = VLC_FOURCC('R', '1', '6', ' ');
            break;
#endif
        default: return VLC_EGENERIC;
    }
    priv->fourcc = va_fourcc;
    return VLC_SUCCESS;
}

static int
tc_vaegl_update(const struct vlc_gl_importer *imp, GLuint *textures,
                const GLsizei *tex_width, const GLsizei *tex_height,
                picture_t *pic, const size_t *plane_offset)
{
    (void) plane_offset;
    struct priv *priv = imp->priv;
    vlc_object_t *o = VLC_OBJECT(imp->gl);
    VAImage va_image;
    VABufferInfo va_buffer_info;
    EGLImageKHR egl_images[3] = { };
    bool release_image = false, release_buffer_info = false;

    if (pic == priv->last.pic)
    {
        va_image = priv->last.va_image;
        va_buffer_info = priv->last.va_buffer_info;
        for (unsigned i = 0; i < priv->last.va_image.num_planes; ++i)
            egl_images[i] = priv->last.egl_images[i];
    }
    else
    {
        if (vlc_vaapi_DeriveImage(o, priv->vadpy, vlc_vaapi_PicGetSurface(pic),
                                  &va_image))
            goto error;
        release_image = true;

        assert(va_image.format.fourcc == priv->fourcc);

        va_buffer_info = (VABufferInfo) {
            .mem_type = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME
        };
        if (vlc_vaapi_AcquireBufferHandle(o, priv->vadpy, va_image.buf,
                                          &va_buffer_info))
            goto error;
        release_buffer_info = true;
    }

    for (unsigned i = 0; i < va_image.num_planes; ++i)
    {
        egl_images[i] =
            vaegl_image_create(imp, tex_width[i], tex_height[i],
                               priv->drm_fourccs[i], va_buffer_info.handle,
                               va_image.offsets[i], va_image.pitches[i]);
        if (egl_images[i] == NULL)
            goto error;

        imp->vt->BindTexture(imp->tex_target, textures[i]);

        priv->glEGLImageTargetTexture2DOES(imp->tex_target, egl_images[i]);
    }

    if (pic != priv->last.pic)
    {
        if (priv->last.pic != NULL)
            vaegl_release_last_pic(imp, priv);
        priv->last.pic = picture_Hold(pic);
        priv->last.va_image = va_image;
        priv->last.va_buffer_info = va_buffer_info;
        for (unsigned i = 0; i < va_image.num_planes; ++i)
            priv->last.egl_images[i] = egl_images[i];
    }

    return VLC_SUCCESS;

error:
    if (release_image)
    {
        if (release_buffer_info)
            vlc_vaapi_ReleaseBufferHandle(o, priv->vadpy, va_image.buf);

        for (unsigned i = 0; i < 3 && egl_images[i] != NULL; ++i)
            vaegl_image_destroy(imp, egl_images[i]);

        vlc_vaapi_DestroyImage(o, priv->vadpy, va_image.image_id);
    }
    return VLC_EGENERIC;
}

static picture_pool_t *
tc_vaegl_get_pool(const struct vlc_gl_importer *imp, unsigned requested_count)
{
    vlc_object_t *o = VLC_OBJECT(imp->gl);
    struct priv *priv = imp->priv;

    vlc_decoder_device *dec_device = vlc_video_context_HoldDevice(imp->vctx);
    picture_pool_t *pool =
        vlc_vaapi_PoolNew(o, dec_device, priv->vadpy,
                          requested_count, &priv->va_surface_ids, &imp->fmt);
    vlc_decoder_device_Release(dec_device);
    if (!pool)
        return NULL;

    /* Check if a surface from the pool can be derived and displayed via dmabuf
     * */
    bool success = false;
    VAImage va_image = { .image_id = VA_INVALID_ID };
    if (vlc_vaapi_DeriveImage(o, priv->vadpy, priv->va_surface_ids[0],
                              &va_image))
        goto error;

    assert(va_image.format.fourcc == priv->fourcc);

    VABufferInfo va_buffer_info = (VABufferInfo) {
        .mem_type = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME
    };
    if (vlc_vaapi_AcquireBufferHandle(o ,priv->vadpy, va_image.buf,
                                      &va_buffer_info))
        goto error;

    for (unsigned i = 0; i < va_image.num_planes; ++i)
    {
        EGLint w = (va_image.width * imp->texs[i].w.num) / imp->texs[i].w.den;
        EGLint h = (va_image.height * imp->texs[i].h.num) / imp->texs[i].h.den;
        EGLImageKHR egl_image =
            vaegl_image_create(imp, w, h, priv->drm_fourccs[i], va_buffer_info.handle,
                               va_image.offsets[i], va_image.pitches[i]);
        if (egl_image == NULL)
        {
            msg_Warn(o, "Can't create Image KHR: kernel too old ?");
            goto error;
        }
        vaegl_image_destroy(imp, egl_image);
    }

    success = true;
error:
    if (va_image.image_id != VA_INVALID_ID)
    {
        if (va_image.buf != VA_INVALID_ID)
            vlc_vaapi_ReleaseBufferHandle(o, priv->vadpy, va_image.buf);
        vlc_vaapi_DestroyImage(o, priv->vadpy, va_image.image_id);
    }
    if (!success)
    {
        picture_pool_Release(pool);
        pool = NULL;
    }
    return pool;
}

static void
Close(vlc_object_t *obj)
{
    opengl_tex_converter_t *tc = (void *)obj;
    struct priv *priv = tc->importer.priv;

    if (priv->last.pic != NULL)
        vaegl_release_last_pic(&tc->importer, priv);

    free(priv);
}

static int strcasecmp_void(const void *a, const void *b)
{
    return strcasecmp(a, b);
}

static int
tc_va_check_interop_blacklist(opengl_tex_converter_t *tc, VADisplay *vadpy)
{
    const char *vendor = vaQueryVendorString(vadpy);
    if (vendor == NULL)
        return VLC_SUCCESS;

#define BL_SIZE_MAX 19
    static const char blacklist_prefix[][BL_SIZE_MAX] = {
        /* XXX: case insensitive and alphabetical order */
        "mesa gallium vaapi",
    };

    char vendor_prefix[BL_SIZE_MAX];
    strncpy(vendor_prefix, vendor, sizeof(vendor_prefix) - 1);
    vendor_prefix[sizeof(vendor_prefix) - 1] = '\0';

    const char *found = bsearch(vendor_prefix, blacklist_prefix,
                                ARRAY_SIZE(blacklist_prefix),
                                BL_SIZE_MAX, strcasecmp_void);
    if (found != NULL)
    {
        msg_Warn(tc->gl, "The '%s' driver is blacklisted: no interop", found);
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

static int
tc_va_check_derive_image(const struct vlc_gl_importer *imp,
                         vlc_decoder_device *dec_device)
{
    vlc_object_t *o = VLC_OBJECT(imp->gl);
    struct priv *priv = imp->priv;
    VASurfaceID *va_surface_ids;

    picture_pool_t *pool = vlc_vaapi_PoolNew(o, dec_device, priv->vadpy, 1,
                                             &va_surface_ids, &imp->fmt);
    if (!pool)
        return VLC_EGENERIC;

    VAImage va_image = { .image_id = VA_INVALID_ID };
    int ret = vlc_vaapi_DeriveImage(o, priv->vadpy, va_surface_ids[0],
                                    &va_image);

    picture_pool_Release(pool);

    return ret;
}

static int
Open(vlc_object_t *obj)
{
    opengl_tex_converter_t *tc = (void *) obj;

    if (tc->importer.vctx == NULL)
        return VLC_EGENERIC;
    vlc_decoder_device *dec_device = vlc_video_context_HoldDevice(tc->importer.vctx);
    if (dec_device->type != VLC_DECODER_DEVICE_VAAPI
     || !vlc_vaapi_IsChromaOpaque(tc->importer.fmt.i_chroma)
     || tc->gl->ext != VLC_GL_EXT_EGL
     || tc->gl->egl.createImageKHR == NULL
     || tc->gl->egl.destroyImageKHR == NULL)
    {
        vlc_decoder_device_Release(dec_device);
        return VLC_EGENERIC;
    }

    if (!vlc_gl_StrHasToken(tc->glexts, "GL_OES_EGL_image"))
    {
        vlc_decoder_device_Release(dec_device);
        return VLC_EGENERIC;
    }

    const char *eglexts = tc->gl->egl.queryString(tc->gl, EGL_EXTENSIONS);
    if (eglexts == NULL || !vlc_gl_StrHasToken(eglexts, "EGL_EXT_image_dma_buf_import"))
    {
        vlc_decoder_device_Release(dec_device);
        return VLC_EGENERIC;
    }

    struct priv *priv = tc->importer.priv = calloc(1, sizeof(struct priv));
    if (unlikely(priv == NULL))
        goto error;
    priv->fourcc = 0;

    int va_fourcc;
    int vlc_sw_chroma;
    switch (tc->importer.fmt.i_chroma)
    {
        case VLC_CODEC_VAAPI_420:
            va_fourcc = VA_FOURCC_NV12;
            vlc_sw_chroma = VLC_CODEC_NV12;
            break;
        case VLC_CODEC_VAAPI_420_10BPP:
            va_fourcc = VA_FOURCC_P010;
            vlc_sw_chroma = VLC_CODEC_P010;
            break;
        default:
            vlc_assert_unreachable();
    }

    if (vaegl_init_fourcc(tc, priv, va_fourcc))
        goto error;

    priv->glEGLImageTargetTexture2DOES =
        vlc_gl_GetProcAddress(tc->gl, "glEGLImageTargetTexture2DOES");
    if (priv->glEGLImageTargetTexture2DOES == NULL)
        goto error;

    priv->vadpy = dec_device->opaque;
    assert(priv->vadpy != NULL);

    if (tc_va_check_interop_blacklist(tc, priv->vadpy))
        goto error;

    if (tc_va_check_derive_image(&tc->importer, dec_device))
        goto error;

    tc->fshader = opengl_fragment_shader_init(tc, GL_TEXTURE_2D, vlc_sw_chroma,
                                              tc->importer.fmt.space);
    if (tc->fshader == 0)
        goto error;

    static const struct vlc_gl_importer_ops ops = {
        .update_textures = tc_vaegl_update,
        .get_pool = tc_vaegl_get_pool,
    };
    tc->importer.ops = &ops;

    vlc_decoder_device_Release(dec_device);

    return VLC_SUCCESS;
error:
    vlc_decoder_device_Release(dec_device);
    free(priv);
    return VLC_EGENERIC;
}

vlc_module_begin ()
    set_description("VA-API OpenGL surface converter")
    set_capability("glconv", 1)
    set_callbacks(Open, Close)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    add_shortcut("vaapi")
vlc_module_end ()
