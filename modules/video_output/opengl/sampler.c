/*****************************************************************************
 * sampler.c
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

#include "sampler_priv.h"

#include <vlc_common.h>
#include <vlc_memstream.h>
#include <vlc_opengl.h>

#ifdef HAVE_LIBPLACEBO
#include <libplacebo/shaders.h>
#include <libplacebo/shaders/colorspace.h>
#include "../placebo_utils.h"
#endif

#include "gl_api.h"
#include "gl_common.h"
#include "gl_util.h"
#include "internal.h"
#include "interop.h"

struct vlc_gl_sampler_priv {
    struct vlc_gl_sampler sampler;

    struct vlc_gl_t *gl;
    const opengl_vtable_t *vt;

    struct {
        GLfloat OrientationMatrix[4*4];
        GLfloat TexCoordsMap[PICTURE_PLANE_MAX][3*3];
    } var;
    struct {
        GLint Texture[PICTURE_PLANE_MAX];
        GLint TexSize[PICTURE_PLANE_MAX]; /* for GL_TEXTURE_RECTANGLE */
        GLint ConvMatrix;
        GLint *pl_vars; /* for pl_sh_res */

        GLint TransformMatrix;
        GLint OrientationMatrix;
        GLint TexCoordsMap[PICTURE_PLANE_MAX];
    } uloc;

    bool yuv_color;
    GLfloat conv_matrix[4*4];

    /* libplacebo context */
    struct pl_context *pl_ctx;
    struct pl_shader *pl_sh;
    const struct pl_shader_res *pl_sh_res;

    GLsizei tex_width[PICTURE_PLANE_MAX];
    GLsizei tex_height[PICTURE_PLANE_MAX];

    GLuint textures[PICTURE_PLANE_MAX];

    struct {
        unsigned int i_x_offset;
        unsigned int i_y_offset;
        unsigned int i_visible_width;
        unsigned int i_visible_height;
    } last_source;

    struct vlc_gl_interop *interop;

    /**
     * Callback to fetch locations of uniform or attributes variables
     *
     * This function pointer cannot be NULL. This callback is called one time
     * after init.
     *
     * \param sampler the sampler
     * \param program linked program that will be used by this sampler
     * \return VLC_SUCCESS or a VLC error
     */
    int (*pf_fetch_locations)(struct vlc_gl_sampler *sampler, GLuint program);

    /**
     * Callback to prepare the fragment shader
     *
     * This function pointer cannot be NULL. This callback can be used to
     * specify values of uniform variables.
     *
     * \param sampler the sampler
     */
    void (*pf_prepare_shader)(const struct vlc_gl_sampler *sampler);
};

#define PRIV(sampler) container_of(sampler, struct vlc_gl_sampler_priv, sampler)

static const float MATRIX_COLOR_RANGE_LIMITED[4*3] = {
    255.0/219,         0,         0, -255.0/219 *  16.0/255,
            0, 255.0/224,         0, -255.0/224 * 128.0/255,
            0,         0, 255.0/224, -255.0/224 * 128.0/255,
};

static const float MATRIX_COLOR_RANGE_FULL[4*3] = {
    1, 0, 0,          0,
    0, 1, 0, -128.0/255,
    0, 0, 1, -128.0/255,
};

/*
 * Construct the transformation matrix from the luma weight of the red and blue
 * component (the green component is deduced).
 */
#define MATRIX_YUV_TO_RGB(KR, KB) \
    MATRIX_YUV_TO_RGB_(KR, (1-(KR)-(KB)), KB)

/*
 * Construct the transformation matrix from the luma weight of the RGB
 * components.
 *
 * KR: luma weight of the red component
 * KG: luma weight of the green component
 * KB: luma weight of the blue component
 *
 * By definition, KR + KG + KB == 1.
 *
 * Ref: <https://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.601_conversion>
 * Ref: libplacebo: src/colorspace.c:luma_coeffs()
 * */
#define MATRIX_YUV_TO_RGB_(KR, KG, KB) { \
    1,                         0,              2*(1.0-(KR)), \
    1, -2*(1.0-(KB))*((KB)/(KG)), -2*(1.0-(KR))*((KR)/(KG)), \
    1,              2*(1.0-(KB)),                         0, \
}

static const float MATRIX_BT601[3*3] = MATRIX_YUV_TO_RGB(0.299, 0.114);
static const float MATRIX_BT709[3*3] = MATRIX_YUV_TO_RGB(0.2126, 0.0722);
static const float MATRIX_BT2020[3*3] = MATRIX_YUV_TO_RGB(0.2627, 0.0593);

static void
init_conv_matrix(float conv_matrix_out[],
                 video_color_space_t color_space,
                 video_color_range_t color_range)
{
    const float *space_matrix;
    switch (color_space) {
        case COLOR_SPACE_BT601:
            space_matrix = MATRIX_BT601;
            break;
        case COLOR_SPACE_BT2020:
            space_matrix = MATRIX_BT2020;
            break;
        default:
            space_matrix = MATRIX_BT709;
    }

    /* Init the conversion matrix in column-major order (OpenGL expects
     * column-major order by default, and OpenGL ES does not support row-major
     * order at all). */

    const float *range_matrix = color_range == COLOR_RANGE_FULL
                              ? MATRIX_COLOR_RANGE_FULL
                              : MATRIX_COLOR_RANGE_LIMITED;
    /* Multiply the matrices on CPU once for all */
    for (int x = 0; x < 4; ++x)
    {
        for (int y = 0; y < 3; ++y)
        {
            /* Perform intermediate computation in double precision even if the
             * result is in single-precision, to avoid unnecessary errors. */
            double sum = 0;
            for (int k = 0; k < 3; ++k)
                sum += space_matrix[y * 3 + k] * range_matrix[k * 4 + x];
            /* Notice the reversed indices: x is now the row, y is the
             * column. */
            conv_matrix_out[x * 4 + y] = sum;
        }
    }

    /* Add a row to fill a 4x4 matrix (remember it's in column-major order).
     * (non-square matrices are not supported on old OpenGL ES versions) */
    conv_matrix_out[3] = 0;
    conv_matrix_out[7] = 0;
    conv_matrix_out[11] = 0;
    conv_matrix_out[15] = 1;
}

static int
sampler_yuv_base_init(struct vlc_gl_sampler *sampler, vlc_fourcc_t chroma,
                      const vlc_chroma_description_t *desc,
                      video_color_space_t yuv_space)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    /* The current implementation always converts from limited to full range. */
    const video_color_range_t range = COLOR_RANGE_LIMITED;
    float *matrix = priv->conv_matrix;
    init_conv_matrix(matrix, yuv_space, range);

    if (desc->pixel_size == 2)
    {
        if (chroma != VLC_CODEC_P010 && chroma != VLC_CODEC_P016) {
            /* Do a bit shift if samples are stored on LSB. */
            float yuv_range_correction = (float)((1 << 16) - 1)
                                         / ((1 << desc->pixel_bits) - 1);
            /* We want to transform the input color (y, u, v, 1) to
             * (r*y, r*u, r*v, 1), where r = yuv_range_correction.
             *
             * This can be done by left-multiplying the color vector by a
             * matrix R:
             *
             *                 R
             *  / r*y \   / r 0 0 0 \   / y \
             *  | r*u | = | 0 r 0 0 | * | u |
             *  | r*v |   | 0 0 r 0 |   | v |
             *  \  1  /   \ 0 0 0 1 /   \ 1 /
             *
             * Combine this transformation with the color conversion matrix:
             *
             *     matrix := matrix * R
             *
             * This is equivalent to multipying the 3 first rows by r
             * (yuv_range_conversion).
             */
            for (int i = 0; i < 4*3; ++i)
                matrix[i] *= yuv_range_correction;
        }
    }

    priv->yuv_color = true;

    /* Some formats require to swap the U and V components.
     *
     * This can be done by left-multiplying the color vector by a matrix S:
     *
     *               S
     *  / y \   / 1 0 0 0 \   / y \
     *  | v | = | 0 0 1 0 | * | u |
     *  | u |   | 0 1 0 0 |   | v |
     *  \ 1 /   \ 0 0 0 1 /   \ 1 /
     *
     * Combine this transformation with the color conversion matrix:
     *
     *     matrix := matrix * S
     *
     * This is equivalent to swap columns 1 and 2.
     */
    bool swap_uv = chroma == VLC_CODEC_YV12 || chroma == VLC_CODEC_YV9 ||
                   chroma == VLC_CODEC_NV21;
    if (swap_uv)
    {
        /* Remember, the matrix in column-major order */
        float tmp[4];
        /* tmp <- column1 */
        memcpy(tmp, matrix + 4, sizeof(tmp));
        /* column1 <- column2 */
        memcpy(matrix + 4, matrix + 8, sizeof(tmp));
        /* column2 <- tmp */
        memcpy(matrix + 8, tmp, sizeof(tmp));
    }
    return VLC_SUCCESS;
}

static int
sampler_base_fetch_locations(struct vlc_gl_sampler *sampler, GLuint program)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    struct vlc_gl_interop *interop = priv->interop;
    const opengl_vtable_t *vt = priv->vt;

    if (priv->yuv_color)
    {
        priv->uloc.ConvMatrix = vt->GetUniformLocation(program,
                                                          "ConvMatrix");
        if (priv->uloc.ConvMatrix == -1)
            return VLC_EGENERIC;
    }

    priv->uloc.TransformMatrix =
        vt->GetUniformLocation(program, "TransformMatrix");
    if (priv->uloc.TransformMatrix == -1)
        return VLC_EGENERIC;

    priv->uloc.OrientationMatrix =
        vt->GetUniformLocation(program, "OrientationMatrix");
    if (priv->uloc.OrientationMatrix == -1)
        return VLC_EGENERIC;

    for (unsigned int i = 0; i < interop->tex_count; ++i)
    {
        char name[sizeof("TexCoordsMapX")];

        snprintf(name, sizeof(name), "Texture%1u", i);
        priv->uloc.Texture[i] = vt->GetUniformLocation(program, name);
        if (priv->uloc.Texture[i] == -1)
            return VLC_EGENERIC;

        snprintf(name, sizeof(name), "TexCoordsMap%1u", i);
        priv->uloc.TexCoordsMap[i] = vt->GetUniformLocation(program, name);
        if (priv->uloc.TexCoordsMap[i] == -1)
            return VLC_EGENERIC;

        if (interop->tex_target == GL_TEXTURE_RECTANGLE)
        {
            snprintf(name, sizeof(name), "TexSize%1u", i);
            priv->uloc.TexSize[i] = vt->GetUniformLocation(program, name);
            if (priv->uloc.TexSize[i] == -1)
                return VLC_EGENERIC;
        }
    }

#ifdef HAVE_LIBPLACEBO
    const struct pl_shader_res *res = priv->pl_sh_res;
    for (int i = 0; res && i < res->num_variables; i++) {
        struct pl_shader_var sv = res->variables[i];
        priv->uloc.pl_vars[i] = vt->GetUniformLocation(program, sv.var.name);
    }
#endif

    return VLC_SUCCESS;
}

static void
sampler_base_prepare_shader(const struct vlc_gl_sampler *sampler)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    const struct vlc_gl_interop *interop = priv->interop;
    const opengl_vtable_t *vt = priv->vt;

    if (priv->yuv_color)
        vt->UniformMatrix4fv(priv->uloc.ConvMatrix, 1, GL_FALSE,
                             priv->conv_matrix);

    for (unsigned i = 0; i < interop->tex_count; ++i)
    {
        vt->Uniform1i(priv->uloc.Texture[i], i);

        assert(priv->textures[i] != 0);
        vt->ActiveTexture(GL_TEXTURE0 + i);
        vt->BindTexture(interop->tex_target, priv->textures[i]);

        vt->UniformMatrix3fv(priv->uloc.TexCoordsMap[i], 1, GL_FALSE,
                             priv->var.TexCoordsMap[i]);
    }

    /* In column-major order */
    static const float MATRIX4_VFLIP[4*4] = {
        1,  0,  0,  0,
        0, -1,  0 , 0,
        0,  0,  1,  0,
        0,  1,  0,  1,
    };

    const GLfloat *tm = NULL;
    if (interop->ops && interop->ops->get_transform_matrix)
        tm = interop->ops->get_transform_matrix(interop);
    if (!tm)
        /*
         * Due to the way texture coordinates are expressed, the interpretation
         * of the video orientation is flipped vertically in the OpenGL world.
         *
         * For example, a video stored from top-left to bottom-right is
         * considered in its natural orientation (ORIENT_NORMAL) in VLC, but
         * flipped vertically (ORIENT_VFLIPPED) in OpenGL, since the origin of
         * coordinates (0, 0) is at the bottom-right.
         *
         * Unless a specific matrix is specified by get_transform_matrix(),
         * flip vertically by default.
         */
        tm = MATRIX4_VFLIP;

    vt->UniformMatrix4fv(priv->uloc.TransformMatrix, 1, GL_FALSE, tm);

    vt->UniformMatrix4fv(priv->uloc.OrientationMatrix, 1, GL_FALSE,
                         priv->var.OrientationMatrix);

    if (interop->tex_target == GL_TEXTURE_RECTANGLE)
    {
        for (unsigned i = 0; i < interop->tex_count; ++i)
            vt->Uniform2f(priv->uloc.TexSize[i], priv->tex_width[i],
                          priv->tex_height[i]);
    }

#ifdef HAVE_LIBPLACEBO
    const struct pl_shader_res *res = priv->pl_sh_res;
    for (int i = 0; res && i < res->num_variables; i++) {
        GLint loc = priv->uloc.pl_vars[i];
        if (loc == -1) // uniform optimized out
            continue;

        struct pl_shader_var sv = res->variables[i];
        struct pl_var var = sv.var;
        // libplacebo doesn't need anything else anyway
        if (var.type != PL_VAR_FLOAT)
            continue;
        if (var.dim_m > 1 && var.dim_m != var.dim_v)
            continue;

        const float *f = sv.data;
        switch (var.dim_m) {
        case 4: vt->UniformMatrix4fv(loc, 1, GL_FALSE, f); break;
        case 3: vt->UniformMatrix3fv(loc, 1, GL_FALSE, f); break;
        case 2: vt->UniformMatrix2fv(loc, 1, GL_FALSE, f); break;

        case 1:
            switch (var.dim_v) {
            case 1: vt->Uniform1f(loc, f[0]); break;
            case 2: vt->Uniform2f(loc, f[0], f[1]); break;
            case 3: vt->Uniform3f(loc, f[0], f[1], f[2]); break;
            case 4: vt->Uniform4f(loc, f[0], f[1], f[2], f[3]); break;
            }
            break;
        }
    }
#endif
}

static int
sampler_xyz12_fetch_locations(struct vlc_gl_sampler *sampler, GLuint program)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);
    const opengl_vtable_t *vt = priv->vt;

    priv->uloc.Texture[0] = vt->GetUniformLocation(program, "Texture0");
    return priv->uloc.Texture[0] != -1 ? VLC_SUCCESS : VLC_EGENERIC;
}

static void
sampler_xyz12_prepare_shader(const struct vlc_gl_sampler *sampler)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);
    const opengl_vtable_t *vt = priv->vt;

    vt->Uniform1i(priv->uloc.Texture[0], 0);
}

static int
xyz12_shader_init(struct vlc_gl_sampler *sampler)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    priv->pf_fetch_locations = sampler_xyz12_fetch_locations;
    priv->pf_prepare_shader = sampler_xyz12_prepare_shader;

    /* Shader for XYZ to RGB correction
     * 3 steps :
     *  - XYZ gamma correction
     *  - XYZ to RGB matrix conversion
     *  - reverse RGB gamma correction
     */
    static const char *template =
        "uniform sampler2D Texture0;"
        "uniform vec4 xyz_gamma = vec4(2.6);"
        "uniform vec4 rgb_gamma = vec4(1.0/2.2);"
        /* WARN: matrix Is filled column by column (not row !) */
        "uniform mat4 matrix_xyz_rgb = mat4("
        "    3.240454 , -0.9692660, 0.0556434, 0.0,"
        "   -1.5371385,  1.8760108, -0.2040259, 0.0,"
        "    -0.4985314, 0.0415560, 1.0572252,  0.0,"
        "    0.0,      0.0,         0.0,        1.0 "
        " );"

        "vec4 vlc_texture(vec2 pic_coords)\n"
        "{ "
        " vec4 v_in, v_out;"
        " v_in  = texture2D(Texture0, pic_coords);"
        " v_in = pow(v_in, xyz_gamma);"
        " v_out = matrix_xyz_rgb * v_in ;"
        " v_out = pow(v_out, rgb_gamma) ;"
        " v_out = clamp(v_out, 0.0, 1.0) ;"
        " return v_out;"
        "}\n";

    sampler->shader.body = strdup(template);
    if (!sampler->shader.body)
        return VLC_ENOMEM;

    return VLC_SUCCESS;
}

static int
opengl_init_swizzle(const struct vlc_gl_interop *interop,
                    const char *swizzle_per_tex[],
                    vlc_fourcc_t chroma,
                    const vlc_chroma_description_t *desc)
{
    GLint oneplane_texfmt;
    if (vlc_gl_StrHasToken(interop->api->extensions, "GL_ARB_texture_rg"))
        oneplane_texfmt = GL_RED;
    else
        oneplane_texfmt = GL_LUMINANCE;

    if (desc->plane_count == 3)
        swizzle_per_tex[0] = swizzle_per_tex[1] = swizzle_per_tex[2] = "r";
    else if (desc->plane_count == 2)
    {
        if (oneplane_texfmt == GL_RED)
        {
            swizzle_per_tex[0] = "r";
            swizzle_per_tex[1] = "rg";
        }
        else
        {
            swizzle_per_tex[0] = "x";
            swizzle_per_tex[1] = "xa";
        }
    }
    else if (desc->plane_count == 1)
    {
        /*
         * Set swizzling in Y1 U V order
         * R  G  B  A
         * U  Y1 V  Y2 => GRB
         * Y1 U  Y2 V  => RGA
         * V  Y1 U  Y2 => GBR
         * Y1 V  Y2 U  => RAG
         */
        switch (chroma)
        {
            case VLC_CODEC_UYVY:
                swizzle_per_tex[0] = "grb";
                break;
            case VLC_CODEC_YUYV:
                swizzle_per_tex[0] = "rga";
                break;
            case VLC_CODEC_VYUY:
                swizzle_per_tex[0] = "gbr";
                break;
            case VLC_CODEC_YVYU:
                swizzle_per_tex[0] = "rag";
                break;
            default:
                assert(!"missing chroma");
                return VLC_EGENERIC;
        }
    }
    return VLC_SUCCESS;
}

static void
InitOrientationMatrix(GLfloat matrix[static 4*4],
                      video_orientation_t orientation)
{
    memcpy(matrix, MATRIX4_IDENTITY, sizeof(MATRIX4_IDENTITY));

/**
 * / C0R0  C1R0     0  C3R0 \
 * | C0R1  C1R1     0  C3R1 |
 * |    0     0     1     0 |  <-- we never transform the z coordinate
 * \    0     0     0     1 /
 *                  ^
 *                  |
 *                  z never impacts the orientation
 *
 * (note that in memory, the matrix is stored in column-major order)
 */
#define MATRIX_SET(C0R0, C1R0, C3R0, \
                   C0R1, C1R1, C3R1) \
    matrix[0*4 + 0] = C0R0; \
    matrix[1*4 + 0] = C1R0; \
    matrix[3*4 + 0] = C3R0; \
    matrix[0*4 + 1] = C0R1; \
    matrix[1*4 + 1] = C1R1; \
    matrix[3*4 + 1] = C3R1;

    switch (orientation) {
        case ORIENT_ROTATED_90:
            /**
             *     1---2          2---3
             *   y |   |   --->   |   | X
             *   | 4---3          1---4 |
             *   +---x              Y---+
             *
             *          x = 1-Y
             *          y = X
             */
                     /* X  Y  1 */
            MATRIX_SET( 0,-1, 1, /* 1-Y */
                        1, 0, 0) /* X */
            break;
        case ORIENT_ROTATED_180:
            /**
             *                      X---+
             *     1---2          3---4 |
             *   y |   |   --->   |   | Y
             *   | 4---3          2---1
             *   +---x
             *
             *          x = 1-X
             *          y = 1-Y
             */
                     /* X  Y  1 */
            MATRIX_SET(-1, 0, 1, /* 1-X */
                        0,-1, 1) /* 1-Y */
            break;
        case ORIENT_ROTATED_270:
            /**
             *                    +---Y
             *     1---2          | 4---1
             *   y |   |   --->   X |   |
             *   | 4---3            3---2
             *   +---x
             *
             *          x = Y
             *          y = 1-X
             */
                     /* X  Y  1 */
            MATRIX_SET( 0, 1, 0, /* Y */
                       -1, 0, 1) /* 1-X */
            break;
        case ORIENT_HFLIPPED:
            /**
             *     1---2          2---1
             *   y |   |   --->   |   | Y
             *   | 4---3          3---4 |
             *   +---x              X---+
             *
             *          x = 1-X
             *          y = Y
             */
                     /* X  Y  1 */
            MATRIX_SET(-1, 0, 1, /* 1-X */
                        0, 1, 0) /* Y */
            break;
        case ORIENT_VFLIPPED:
            /**
             *                    +---X
             *     1---2          | 4---3
             *   y |   |   --->   Y |   |
             *   | 4---3            1---2
             *   +---x
             *
             *          x = X
             *          y = 1-Y
             */
                     /* X  Y  1 */
            MATRIX_SET( 1, 0, 0, /* X */
                        0,-1, 1) /* 1-Y */
            break;
        case ORIENT_TRANSPOSED:
            /**
             *                      Y---+
             *     1---2          1---4 |
             *   y |   |   --->   |   | X
             *   | 4---3          2---3
             *   +---x
             *
             *          x = 1-Y
             *          y = 1-X
             */
                     /* X  Y  1 */
            MATRIX_SET( 0,-1, 1, /* 1-Y */
                       -1, 0, 1) /* 1-X */
            break;
        case ORIENT_ANTI_TRANSPOSED:
            /**
             *     1---2            3---2
             *   y |   |   --->   X |   |
             *   | 4---3          | 4---1
             *   +---x            +---Y
             *
             *          x = Y
             *          y = X
             */
                     /* X  Y  1 */
            MATRIX_SET( 0, 1, 0, /* Y */
                        1, 0, 0) /* X */
            break;
        default:
            break;
    }
}

static int
opengl_fragment_shader_init(struct vlc_gl_sampler *sampler, GLenum tex_target,
                            vlc_fourcc_t chroma, video_color_space_t yuv_space,
                            video_orientation_t orientation)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    struct vlc_gl_interop *interop = priv->interop;
    const opengl_vtable_t *vt = priv->vt;

    const char *swizzle_per_tex[PICTURE_PLANE_MAX] = { NULL, };
    const bool is_yuv = vlc_fourcc_IsYUV(chroma);
    int ret;

    const vlc_chroma_description_t *desc = vlc_fourcc_GetChromaDescription(chroma);
    if (desc == NULL)
        return VLC_EGENERIC;

    if (chroma == VLC_CODEC_XYZ12)
        return xyz12_shader_init(sampler);

    InitOrientationMatrix(priv->var.OrientationMatrix, orientation);

    if (is_yuv)
    {
        ret = sampler_yuv_base_init(sampler, chroma, desc, yuv_space);
        if (ret != VLC_SUCCESS)
            return ret;
        ret = opengl_init_swizzle(interop, swizzle_per_tex, chroma, desc);
        if (ret != VLC_SUCCESS)
            return ret;
    }

    const char *sampler_name, *lookup;
    switch (tex_target)
    {
        case GL_TEXTURE_EXTERNAL_OES:
            sampler_name = "samplerExternalOES";
            lookup = "texture2D";
            break;
        case GL_TEXTURE_2D:
            sampler_name = "sampler2D";
            lookup  = "texture2D";
            break;
        case GL_TEXTURE_RECTANGLE:
            sampler_name = "sampler2DRect";
            lookup  = "texture2DRect";
            break;
        default:
            vlc_assert_unreachable();
    }

    struct vlc_memstream ms;
    if (vlc_memstream_open(&ms) != 0)
        return ret;

#define ADD(x) vlc_memstream_puts(&ms, x)
#define ADDF(x, ...) vlc_memstream_printf(&ms, x, ##__VA_ARGS__)

    ADD("uniform mat4 TransformMatrix;\n"
        "uniform mat4 OrientationMatrix;\n");
    for (unsigned i = 0; i < interop->tex_count; ++i)
        ADDF("uniform %s Texture%u;\n"
             "uniform mat3 TexCoordsMap%u;\n", sampler_name, i, i);

#ifdef HAVE_LIBPLACEBO
    if (priv->pl_sh) {
        struct pl_shader *sh = priv->pl_sh;
        struct pl_color_map_params color_params = pl_color_map_default_params;
        color_params.intent = var_InheritInteger(priv->gl, "rendering-intent");
        color_params.tone_mapping_algo = var_InheritInteger(priv->gl, "tone-mapping");
        color_params.tone_mapping_param = var_InheritFloat(priv->gl, "tone-mapping-param");
#    if PL_API_VER >= 10
        color_params.desaturation_strength = var_InheritFloat(priv->gl, "desat-strength");
        color_params.desaturation_exponent = var_InheritFloat(priv->gl, "desat-exponent");
        color_params.desaturation_base = var_InheritFloat(priv->gl, "desat-base");
#    else
        color_params.tone_mapping_desaturate = var_InheritFloat(priv->gl, "tone-mapping-desat");
#    endif
        color_params.gamut_warning = var_InheritBool(priv->gl, "tone-mapping-warn");

        struct pl_color_space dst_space = pl_color_space_unknown;
        dst_space.primaries = var_InheritInteger(priv->gl, "target-prim");
        dst_space.transfer = var_InheritInteger(priv->gl, "target-trc");

        pl_shader_color_map(sh, &color_params,
                vlc_placebo_ColorSpace(&interop->fmt),
                dst_space, NULL, false);

        struct pl_shader_obj *dither_state = NULL;
        int method = var_InheritInteger(priv->gl, "dither-algo");
        if (method >= 0) {

            unsigned out_bits = 0;
            int override = var_InheritInteger(priv->gl, "dither-depth");
            if (override > 0)
                out_bits = override;
            else
            {
                GLint fb_depth = 0;
#if !defined(USE_OPENGL_ES2)
                /* fetch framebuffer depth (we are already bound to the default one). */
                if (vt->GetFramebufferAttachmentParameteriv != NULL)
                    vt->GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT,
                                                            GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE,
                                                            &fb_depth);
#endif
                if (fb_depth <= 0)
                    fb_depth = 8;
                out_bits = fb_depth;
            }

            pl_shader_dither(sh, out_bits, &dither_state, &(struct pl_dither_params) {
                .method   = method,
                .lut_size = 4, // avoid too large values, since this gets embedded
            });
        }

        const struct pl_shader_res *res = priv->pl_sh_res = pl_shader_finalize(sh);
        pl_shader_obj_destroy(&dither_state);

        FREENULL(priv->uloc.pl_vars);
        priv->uloc.pl_vars = calloc(res->num_variables, sizeof(GLint));
        for (int i = 0; i < res->num_variables; i++) {
            struct pl_shader_var sv = res->variables[i];
            const char *glsl_type_name = pl_var_glsl_type_name(sv.var);
            ADDF("uniform %s %s;\n", glsl_type_name, sv.var.name);
        }

        // We can't handle these yet, but nothing we use requires them, either
        assert(res->num_vertex_attribs == 0);
        assert(res->num_descriptors == 0);

        ADD(res->glsl);
    }
#else
    if (interop->fmt.transfer == TRANSFER_FUNC_SMPTE_ST2084 ||
        interop->fmt.primaries == COLOR_PRIMARIES_BT2020)
    {
        // no warning for HLG because it's more or less backwards-compatible
        msg_Warn(priv->gl, "VLC needs to be built with support for libplacebo "
                 "in order to display wide gamut or HDR signals correctly.");
    }
#endif

    if (tex_target == GL_TEXTURE_RECTANGLE)
    {
        for (unsigned i = 0; i < interop->tex_count; ++i)
            ADDF("uniform vec2 TexSize%u;\n", i);
    }

    if (is_yuv)
        ADD("uniform mat4 ConvMatrix;\n");

    ADD("vec4 vlc_texture(vec2 pic_coords) {\n"
        /* Homogeneous (oriented) coordinates */
        " vec3 pic_hcoords = vec3((TransformMatrix * OrientationMatrix * vec4(pic_coords, 0.0, 1.0)).st, 1.0);\n"
        " vec2 tex_coords;\n");

    if (tex_target == GL_TEXTURE_RECTANGLE)
    {
        for (unsigned i = 0; i < interop->tex_count; ++i)
        {
            ADDF(" tex_coords = (TexCoordsMap%u * pic_hcoords).st;\n", i);
            ADDF(" vec2 TexCoordsRect%u = vec2(tex_coords.x * TexSize%u.x, "
                 "tex_coords.y * TexSize%u.y);\n", i, i, i);
        }
    }

    unsigned color_count;
    if (is_yuv) {
        ADD(" vec4 texel;\n"
            " vec4 pixel = vec4(0.0, 0.0, 0.0, 1.0);\n");
        unsigned color_idx = 0;
        for (unsigned i = 0; i < interop->tex_count; ++i)
        {
            const char *swizzle = swizzle_per_tex[i];
            assert(swizzle);
            size_t swizzle_count = strlen(swizzle);
            ADDF(" tex_coords = (TexCoordsMap%u * pic_hcoords).st;\n", i);
            ADDF(" texel = %s(Texture%u, tex_coords);\n", lookup, i);
            for (unsigned j = 0; j < swizzle_count; ++j)
            {
                ADDF(" pixel[%u] = texel.%c;\n", color_idx, swizzle[j]);
                color_idx++;
                assert(color_idx <= PICTURE_PLANE_MAX);
            }
        }
        ADD(" vec4 result = ConvMatrix * pixel;\n");
        color_count = color_idx;
    }
    else
    {
        ADD(" tex_coords = (TexCoordsMap0 * pic_hcoords).st;\n");
        ADDF(" vec4 result = %s(Texture0, tex_coords);\n", lookup);
        color_count = 1;
    }
    assert(yuv_space == COLOR_SPACE_UNDEF || color_count == 3);

#ifdef HAVE_LIBPLACEBO
    if (priv->pl_sh_res) {
        const struct pl_shader_res *res = priv->pl_sh_res;
        assert(res->input  == PL_SHADER_SIG_COLOR);
        assert(res->output == PL_SHADER_SIG_COLOR);
        ADDF(" result = %s(result);\n", res->name);
    }
#endif

    ADD(" return result;\n"
        "}\n");

#undef ADD
#undef ADDF

    if (vlc_memstream_close(&ms) != 0)
        return VLC_EGENERIC;

    if (interop->tex_target == GL_TEXTURE_EXTERNAL_OES)
    {
        sampler->shader.extensions =
            strdup("#extension GL_OES_EGL_image_external : require\n");
        if (!sampler->shader.extensions)
            return VLC_EGENERIC;
    }
    sampler->shader.body = ms.ptr;

    priv->pf_fetch_locations = sampler_base_fetch_locations;
    priv->pf_prepare_shader = sampler_base_prepare_shader;

    return VLC_SUCCESS;
}

struct vlc_gl_sampler *
vlc_gl_sampler_New(struct vlc_gl_interop *interop)
{
    struct vlc_gl_sampler_priv *priv = calloc(1, sizeof(*priv));
    if (!priv)
        return NULL;

    struct vlc_gl_sampler *sampler = &priv->sampler;

    priv->interop = interop;
    priv->gl = interop->gl;
    priv->vt = interop->vt;

    sampler->fmt = &interop->fmt;
    sampler->sw_fmt = &interop->sw_fmt;

    sampler->shader.extensions = NULL;
    sampler->shader.body = NULL;

#ifdef HAVE_LIBPLACEBO
    // Create the main libplacebo context
    priv->pl_ctx = vlc_placebo_Create(VLC_OBJECT(interop->gl));
    if (priv->pl_ctx) {
#   if PL_API_VER >= 20
        priv->pl_sh = pl_shader_alloc(priv->pl_ctx, NULL);
#   elif PL_API_VER >= 6
        priv->pl_sh = pl_shader_alloc(priv->pl_ctx, NULL, 0);
#   else
        priv->pl_sh = pl_shader_alloc(priv->pl_ctx, NULL, 0, 0);
#   endif
    }
#endif

    /* Texture size */
    for (unsigned j = 0; j < interop->tex_count; j++) {
        const GLsizei w = interop->fmt.i_visible_width  * interop->texs[j].w.num
                        / interop->texs[j].w.den;
        const GLsizei h = interop->fmt.i_visible_height * interop->texs[j].h.num
                        / interop->texs[j].h.den;
        if (interop->api->supports_npot) {
            priv->tex_width[j]  = w;
            priv->tex_height[j] = h;
        } else {
            priv->tex_width[j]  = vlc_align_pot(w);
            priv->tex_height[j] = vlc_align_pot(h);
        }
    }

    int ret =
        opengl_fragment_shader_init(sampler, interop->tex_target,
                                    interop->sw_fmt.i_chroma,
                                    interop->sw_fmt.space,
                                    interop->sw_fmt.orientation);
    if (ret != VLC_SUCCESS)
    {
        free(sampler);
        return NULL;
    }

    if (!interop->handle_texs_gen)
    {
        ret = vlc_gl_interop_GenerateTextures(interop, priv->tex_width,
                                              priv->tex_height,
                                              priv->textures);
        if (ret != VLC_SUCCESS)
        {
            free(sampler);
            return NULL;
        }
    }

    return sampler;
}

void
vlc_gl_sampler_Delete(struct vlc_gl_sampler *sampler)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    struct vlc_gl_interop *interop = priv->interop;
    const opengl_vtable_t *vt = interop->vt;

    if (!interop->handle_texs_gen)
        vt->DeleteTextures(interop->tex_count, priv->textures);

#ifdef HAVE_LIBPLACEBO
    FREENULL(priv->uloc.pl_vars);
    if (priv->pl_ctx)
        pl_context_destroy(&priv->pl_ctx);
#endif

    free(sampler->shader.extensions);
    free(sampler->shader.body);

    free(priv);
}

int
vlc_gl_sampler_Update(struct vlc_gl_sampler *sampler, picture_t *picture)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    const struct vlc_gl_interop *interop = priv->interop;
    const video_format_t *source = &picture->format;

    if (source->i_x_offset != priv->last_source.i_x_offset
     || source->i_y_offset != priv->last_source.i_y_offset
     || source->i_visible_width != priv->last_source.i_visible_width
     || source->i_visible_height != priv->last_source.i_visible_height)
    {
        memset(priv->var.TexCoordsMap, 0,
               sizeof(priv->var.TexCoordsMap));
        for (unsigned j = 0; j < interop->tex_count; j++)
        {
            float scale_w = (float)interop->texs[j].w.num / interop->texs[j].w.den
                          / priv->tex_width[j];
            float scale_h = (float)interop->texs[j].h.num / interop->texs[j].h.den
                          / priv->tex_height[j];

            /* Warning: if NPOT is not supported a larger texture is
               allocated. This will cause right and bottom coordinates to
               land on the edge of two texels with the texels to the
               right/bottom uninitialized by the call to
               glTexSubImage2D. This might cause a green line to appear on
               the right/bottom of the display.
               There are two possible solutions:
               - Manually mirror the edges of the texture.
               - Add a "-1" when computing right and bottom, however the
               last row/column might not be displayed at all.
            */
            float left   = (source->i_x_offset +                       0 ) * scale_w;
            float top    = (source->i_y_offset +                       0 ) * scale_h;
            float right  = (source->i_x_offset + source->i_visible_width ) * scale_w;
            float bottom = (source->i_y_offset + source->i_visible_height) * scale_h;

            /**
             * This matrix converts from picture coordinates (in range [0; 1])
             * to textures coordinates where the picture is actually stored
             * (removing paddings).
             *
             *        texture           (in texture coordinates)
             *       +----------------+--- 0.0
             *       |                |
             *       |  +---------+---|--- top
             *       |  | picture |   |
             *       |  +---------+---|--- bottom
             *       |  .         .   |
             *       |  .         .   |
             *       +----------------+--- 1.0
             *       |  .         .   |
             *      0.0 left  right  1.0  (in texture coordinates)
             *
             * In particular:
             *  - (0.0, 0.0) is mapped to (left, top)
             *  - (1.0, 1.0) is mapped to (right, bottom)
             *
             * This is an affine 2D transformation, so the input coordinates
             * are given as a 3D vector in the form (x, y, 1), and the output
             * is (x', y', 1).
             *
             * The paddings are l (left), r (right), t (top) and b (bottom).
             *
             *               / (r-l)   0     l \
             *      matrix = |   0   (b-t)   t |
             *               \   0     0     1 /
             *
             * It is stored in column-major order.
             */
            GLfloat *matrix = priv->var.TexCoordsMap[j];
#define COL(x) (x*3)
#define ROW(x) (x)
            matrix[COL(0) + ROW(0)] = right - left;
            matrix[COL(1) + ROW(1)] = bottom - top;
            matrix[COL(2) + ROW(0)] = left;
            matrix[COL(2) + ROW(1)] = top;
#undef COL
#undef ROW
        }

        priv->last_source.i_x_offset = source->i_x_offset;
        priv->last_source.i_y_offset = source->i_y_offset;
        priv->last_source.i_visible_width = source->i_visible_width;
        priv->last_source.i_visible_height = source->i_visible_height;
    }

    /* Update the texture */
    return interop->ops->update_textures(interop, priv->textures,
                                         priv->tex_width, priv->tex_height,
                                         picture, NULL);
}

int
vlc_gl_sampler_FetchLocations(struct vlc_gl_sampler *sampler, GLuint program)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    return priv->pf_fetch_locations(sampler, program);
}

void
vlc_gl_sampler_PrepareShader(const struct vlc_gl_sampler *sampler)
{
    struct vlc_gl_sampler_priv *priv = PRIV(sampler);

    priv->pf_prepare_shader(sampler);
}
