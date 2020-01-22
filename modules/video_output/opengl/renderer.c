/*****************************************************************************
 * renderer.c
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

#include "renderer.h"

#include <vlc_common.h>
#include <vlc_picture.h>

#define SPHERE_RADIUS 1.f

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

struct vlc_gl_renderer
{
    vlc_gl_t *gl;
    const opengl_vtable_t *vt;

    struct vlc_gl_interop *interop;

    bool supports_npot;

    video_format_t fmt;

    GLsizei tex_widths[PICTURE_PLANE_MAX];
    GLsizei tex_heights[PICTURE_PLANE_MAX];

    GLuint textures[PICTURE_PLANE_MAX];

    unsigned nb_indices;
    GLuint vertex_buffer_object;
    GLuint index_buffer_object;
    GLuint texture_buffer_object[PICTURE_PLANE_MAX];

    struct {
        unsigned int i_x_offset;
        unsigned int i_y_offset;
        unsigned int i_visible_width;
        unsigned int i_visible_height;
    } last_source;

    /* View point */
    vlc_viewpoint_t vp;
    float f_teta;
    float f_phi;
    float f_roll;
    float f_fovx; /* f_fovx and f_fovy are linked but we keep both */
    float f_fovy; /* to avoid recalculating them when needed.      */
    float f_z;    /* Position of the camera on the shpere radius vector */
    float f_sar;

    GLuint program_id;

    struct {
        GLfloat OrientationMatrix[16];
        GLfloat ProjectionMatrix[16];
        GLfloat ZoomMatrix[16];
        GLfloat ViewMatrix[16];
    } var;

    struct { /* UniformLocation */
        GLint TransformMatrix;
        GLint OrientationMatrix;
        GLint ProjectionMatrix;
        GLint ViewMatrix;
        GLint ZoomMatrix;
    } uloc;
    struct { /* AttribLocation */
        GLint MultiTexCoord[3];
        GLint VertexPosition;
    } aloc;
};

static const GLfloat identity[] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

static void getZoomMatrix(float zoom, GLfloat matrix[static 16]) {

    const GLfloat m[] = {
        /* x   y     z     w */
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, zoom, 1.0f
    };

    memcpy(matrix, m, sizeof(m));
}

/* perspective matrix see https://www.opengl.org/sdk/docs/man2/xhtml/gluPerspective.xml */
static void getProjectionMatrix(float sar, float fovy, GLfloat matrix[static 16]) {

    float zFar  = 1000;
    float zNear = 0.01;

    float f = 1.f / tanf(fovy / 2.f);

    const GLfloat m[] = {
        f / sar, 0.f,                   0.f,                0.f,
        0.f,     f,                     0.f,                0.f,
        0.f,     0.f,     (zNear + zFar) / (zNear - zFar), -1.f,
        0.f,     0.f, (2 * zNear * zFar) / (zNear - zFar),  0.f};

     memcpy(matrix, m, sizeof(m));
}

static void getViewpointMatrixes(vout_display_opengl_t *vgl,
                                 video_projection_mode_t projection_mode,
                                 struct prgm *prgm)
{
    if (projection_mode == PROJECTION_MODE_EQUIRECTANGULAR
        || projection_mode == PROJECTION_MODE_CUBEMAP_LAYOUT_STANDARD)
    {
        getProjectionMatrix(vgl->f_sar, vgl->f_fovy, prgm->var.ProjectionMatrix);
        getZoomMatrix(vgl->f_z, prgm->var.ZoomMatrix);

        /* vgl->vp has been reversed and is a world transform */
        vlc_viewpoint_to_4x4(&vgl->vp, prgm->var.ViewMatrix);
    }
    else
    {
        memcpy(prgm->var.ProjectionMatrix, identity, sizeof(identity));
        memcpy(prgm->var.ZoomMatrix, identity, sizeof(identity));
        memcpy(prgm->var.ViewMatrix, identity, sizeof(identity));
    }

}

static void getOrientationTransformMatrix(video_orientation_t orientation,
                                          GLfloat matrix[static 16])
{
    memcpy(matrix, identity, sizeof(identity));

    const int k_cos_pi = -1;
    const int k_cos_pi_2 = 0;
    const int k_cos_n_pi_2 = 0;

    const int k_sin_pi = 0;
    const int k_sin_pi_2 = 1;
    const int k_sin_n_pi_2 = -1;

    switch (orientation) {

        case ORIENT_ROTATED_90:
            matrix[0 * 4 + 0] = k_cos_pi_2;
            matrix[0 * 4 + 1] = -k_sin_pi_2;
            matrix[1 * 4 + 0] = k_sin_pi_2;
            matrix[1 * 4 + 1] = k_cos_pi_2;
            matrix[3 * 4 + 1] = 1;
            break;
        case ORIENT_ROTATED_180:
            matrix[0 * 4 + 0] = k_cos_pi;
            matrix[0 * 4 + 1] = -k_sin_pi;
            matrix[1 * 4 + 0] = k_sin_pi;
            matrix[1 * 4 + 1] = k_cos_pi;
            matrix[3 * 4 + 0] = 1;
            matrix[3 * 4 + 1] = 1;
            break;
        case ORIENT_ROTATED_270:
            matrix[0 * 4 + 0] = k_cos_n_pi_2;
            matrix[0 * 4 + 1] = -k_sin_n_pi_2;
            matrix[1 * 4 + 0] = k_sin_n_pi_2;
            matrix[1 * 4 + 1] = k_cos_n_pi_2;
            matrix[3 * 4 + 0] = 1;
            break;
        case ORIENT_HFLIPPED:
            matrix[0 * 4 + 0] = -1;
            matrix[3 * 4 + 0] = 1;
            break;
        case ORIENT_VFLIPPED:
            matrix[1 * 4 + 1] = -1;
            matrix[3 * 4 + 1] = 1;
            break;
        case ORIENT_TRANSPOSED:
            matrix[0 * 4 + 0] = 0;
            matrix[1 * 4 + 1] = 0;
            matrix[2 * 4 + 2] = -1;
            matrix[0 * 4 + 1] = 1;
            matrix[1 * 4 + 0] = 1;
            break;
        case ORIENT_ANTI_TRANSPOSED:
            matrix[0 * 4 + 0] = 0;
            matrix[1 * 4 + 1] = 0;
            matrix[2 * 4 + 2] = -1;
            matrix[0 * 4 + 1] = -1;
            matrix[1 * 4 + 0] = -1;
            matrix[3 * 4 + 0] = 1;
            matrix[3 * 4 + 1] = 1;
            break;
        default:
            break;
    }
}

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
        "uniform mat4 TransformMatrix;\n"
        "uniform mat4 OrientationMatrix;\n"
        "uniform mat4 ProjectionMatrix;\n"
        "uniform mat4 ZoomMatrix;\n"
        "uniform mat4 ViewMatrix;\n"
        "void main() {\n"
        " TexCoord0 = vec4(OrientationMatrix * TransformMatrix * MultiTexCoord0).st;\n"
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
                (const char *)&tc->interop->fmt.i_chroma, code);
    tc->vt->CompileShader(shader);
    free(code);
    return shader;
}

static int
opengl_link_program(struct prgm *prgm)
{
    opengl_tex_converter_t *tc = prgm->tc;
    struct vlc_gl_interop *interop = tc->interop;

    GLuint vertex_shader = BuildVertexShader(tc, interop->tex_count);
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
    GET_ULOC(TransformMatrix, "TransformMatrix");
    GET_ULOC(OrientationMatrix, "OrientationMatrix");
    GET_ULOC(ProjectionMatrix, "ProjectionMatrix");
    GET_ULOC(ViewMatrix, "ViewMatrix");
    GET_ULOC(ZoomMatrix, "ZoomMatrix");

    GET_ALOC(VertexPosition, "VertexPosition");
    GET_ALOC(MultiTexCoord[0], "MultiTexCoord0");
    /* MultiTexCoord 1 and 2 can be optimized out if not used */
    if (interop->tex_count > 1)
        GET_ALOC(MultiTexCoord[1], "MultiTexCoord1");
    else
        prgm->aloc.MultiTexCoord[1] = -1;
    if (interop->tex_count > 2)
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
opengl_deinit_program(vout_display_opengl_t *vgl, struct prgm *prgm)
{
    opengl_tex_converter_t *tc = prgm->tc;
    struct vlc_gl_interop *interop = tc->interop;
    if (interop->module != NULL)
        module_unneed(interop, interop->module);
    else if (interop->priv != NULL)
        opengl_interop_generic_deinit(interop);
    vlc_object_delete(interop);
    if (prgm->id != 0)
        vgl->vt.DeleteProgram(prgm->id);

#ifdef HAVE_LIBPLACEBO
    FREENULL(tc->uloc.pl_vars);
    if (tc->pl_ctx)
        pl_context_destroy(&tc->pl_ctx);
#endif

    free(tc);
}

static int
opengl_init_program(vout_display_opengl_t *vgl, vlc_video_context *context,
                    struct prgm *prgm, const char *glexts,
                    const video_format_t *fmt, bool b_dump_shaders)
{
    opengl_tex_converter_t *tc = calloc(1, sizeof(*tc));
    if (tc == NULL)
        return VLC_ENOMEM;

    struct vlc_gl_interop *interop =
        CreateInterop(vgl->gl, &vgl->vt, context, glexts, fmt, false);
    if (!interop)
    {
        free(tc);
        return VLC_ENOMEM;
    }

    tc->interop = interop;

    tc->gl = vgl->gl;
    tc->vt = &vgl->vt;
    tc->b_dump_shaders = b_dump_shaders;
#if defined(USE_OPENGL_ES2)
    tc->glsl_version = 100;
    tc->glsl_precision_header = "precision highp float;\n";
#else
    tc->glsl_version = 120;
    tc->glsl_precision_header = "";
#endif

#ifdef HAVE_LIBPLACEBO
    // Create the main libplacebo context
    tc->pl_ctx = vlc_placebo_Create(VLC_OBJECT(vgl->gl));
    if (tc->pl_ctx) {
#   if PL_API_VER >= 20
        tc->pl_sh = pl_shader_alloc(tc->pl_ctx, NULL);
#   elif PL_API_VER >= 6
        tc->pl_sh = pl_shader_alloc(tc->pl_ctx, NULL, 0);
#   else
        tc->pl_sh = pl_shader_alloc(tc->pl_ctx, NULL, 0, 0);
#   endif
    }
#endif

    GLuint fragment_shader =
        opengl_fragment_shader_init(tc, interop->tex_target,
                                    interop->sw_fmt.i_chroma,
                                    interop->sw_fmt.space);
    if (!fragment_shader)
    {
        vlc_object_delete(interop);
        free(tc);
        return VLC_EGENERIC;
    }

    tc->fshader = fragment_shader;

    assert(tc->fshader != 0 &&
           interop->tex_target != 0 &&
           interop->tex_count > 0 &&
           interop->ops->update_textures != NULL &&
           tc->pf_fetch_locations != NULL &&
           tc->pf_prepare_shader != NULL);

    prgm->tc = tc;

    int ret = opengl_link_program(prgm);
    if (ret != VLC_SUCCESS)
    {
        opengl_deinit_program(vgl, prgm);
        return VLC_EGENERIC;
    }

    getOrientationTransformMatrix(interop->fmt.orientation,
                                  prgm->var.OrientationMatrix);
    getViewpointMatrixes(vgl, interop->fmt.projection_mode, prgm);

    return VLC_SUCCESS;
}

static void UpdateZ(vout_display_opengl_t *vgl)
{
    /* Do trigonometry to calculate the minimal z value
     * that will allow us to zoom out without seeing the outside of the
     * sphere (black borders). */
    float tan_fovx_2 = tanf(vgl->f_fovx / 2);
    float tan_fovy_2 = tanf(vgl->f_fovy / 2);
    float z_min = - SPHERE_RADIUS / sinf(atanf(sqrtf(
                    tan_fovx_2 * tan_fovx_2 + tan_fovy_2 * tan_fovy_2)));

    /* The FOV value above which z is dynamically calculated. */
    const float z_thresh = 90.f;

    if (vgl->f_fovx <= z_thresh * M_PI / 180)
        vgl->f_z = 0;
    else
    {
        float f = z_min / ((FIELD_OF_VIEW_DEGREES_MAX - z_thresh) * M_PI / 180);
        vgl->f_z = f * vgl->f_fovx - f * z_thresh * M_PI / 180;
        if (vgl->f_z < z_min)
            vgl->f_z = z_min;
    }
}

static void UpdateFOVy(vout_display_opengl_t *vgl)
{
    vgl->f_fovy = 2 * atanf(tanf(vgl->f_fovx / 2) / vgl->f_sar);
}

int vout_display_opengl_SetViewpoint(vout_display_opengl_t *vgl,
                                     const vlc_viewpoint_t *p_vp)
{
    if (p_vp->fov > FIELD_OF_VIEW_DEGREES_MAX
            || p_vp->fov < FIELD_OF_VIEW_DEGREES_MIN)
        return VLC_EBADVAR;

    // Convert degree into radian
    float f_fovx = p_vp->fov * (float)M_PI / 180.f;

    /* vgl->vp needs to be converted into world transform */
    vlc_viewpoint_reverse(&vgl->vp, p_vp);

    if (fabsf(f_fovx - vgl->f_fovx) >= 0.001f)
    {
        /* FOVx has changed. */
        vgl->f_fovx = f_fovx;
        UpdateFOVy(vgl);
        UpdateZ(vgl);
    }
    getViewpointMatrixes(vgl, vgl->fmt.projection_mode, &vgl->prgm);

    return VLC_SUCCESS;
}


void vout_display_opengl_SetWindowAspectRatio(vout_display_opengl_t *vgl,
                                              float f_sar)
{
    /* Each time the window size changes, we must recompute the minimum zoom
     * since the aspect ration changes.
     * We must also set the new current zoom value. */
    vgl->f_sar = f_sar;
    UpdateFOVy(vgl);
    UpdateZ(vgl);
    getViewpointMatrixes(vgl, vgl->fmt.projection_mode, &vgl->prgm);
}

void vout_display_opengl_Viewport(vout_display_opengl_t *vgl, int x, int y,
                                  unsigned width, unsigned height)
{
    vgl->vt.Viewport(x, y, width, height);
}

int
vlc_gl_renderer_Prepare(struct vlc_gl_renderer *r, picture_t *picture)
{
}

static int BuildSphere(unsigned nbPlanes,
                        GLfloat **vertexCoord, GLfloat **textureCoord, unsigned *nbVertices,
                        GLushort **indices, unsigned *nbIndices,
                        const float *left, const float *top,
                        const float *right, const float *bottom)
{
    unsigned nbLatBands = 128;
    unsigned nbLonBands = 128;

    *nbVertices = (nbLatBands + 1) * (nbLonBands + 1);
    *nbIndices = nbLatBands * nbLonBands * 3 * 2;

    *vertexCoord = vlc_alloc(*nbVertices * 3, sizeof(GLfloat));
    if (*vertexCoord == NULL)
        return VLC_ENOMEM;
    *textureCoord = vlc_alloc(nbPlanes * *nbVertices * 2, sizeof(GLfloat));
    if (*textureCoord == NULL)
    {
        free(*vertexCoord);
        return VLC_ENOMEM;
    }
    *indices = vlc_alloc(*nbIndices, sizeof(GLushort));
    if (*indices == NULL)
    {
        free(*textureCoord);
        free(*vertexCoord);
        return VLC_ENOMEM;
    }

    for (unsigned lat = 0; lat <= nbLatBands; lat++) {
        float theta = lat * (float) M_PI / nbLatBands;
        float sinTheta, cosTheta;

        sincosf(theta, &sinTheta, &cosTheta);

        for (unsigned lon = 0; lon <= nbLonBands; lon++) {
            float phi = lon * 2 * (float) M_PI / nbLonBands;
            float sinPhi, cosPhi;

            sincosf(phi, &sinPhi, &cosPhi);

            float x = cosPhi * sinTheta;
            float y = cosTheta;
            float z = sinPhi * sinTheta;

            unsigned off1 = (lat * (nbLonBands + 1) + lon) * 3;
            (*vertexCoord)[off1] = SPHERE_RADIUS * x;
            (*vertexCoord)[off1 + 1] = SPHERE_RADIUS * y;
            (*vertexCoord)[off1 + 2] = SPHERE_RADIUS * z;

            for (unsigned p = 0; p < nbPlanes; ++p)
            {
                unsigned off2 = (p * (nbLatBands + 1) * (nbLonBands + 1)
                                + lat * (nbLonBands + 1) + lon) * 2;
                float width = right[p] - left[p];
                float height = bottom[p] - top[p];
                float u = (float)lon / nbLonBands * width;
                float v = (float)lat / nbLatBands * height;
                (*textureCoord)[off2] = u;
                (*textureCoord)[off2 + 1] = v;
            }
        }
    }

    for (unsigned lat = 0; lat < nbLatBands; lat++) {
        for (unsigned lon = 0; lon < nbLonBands; lon++) {
            unsigned first = (lat * (nbLonBands + 1)) + lon;
            unsigned second = first + nbLonBands + 1;

            unsigned off = (lat * nbLatBands + lon) * 3 * 2;

            (*indices)[off] = first;
            (*indices)[off + 1] = second;
            (*indices)[off + 2] = first + 1;

            (*indices)[off + 3] = second;
            (*indices)[off + 4] = second + 1;
            (*indices)[off + 5] = first + 1;
        }
    }

    return VLC_SUCCESS;
}


static int BuildCube(unsigned nbPlanes,
                     float padW, float padH,
                     GLfloat **vertexCoord, GLfloat **textureCoord, unsigned *nbVertices,
                     GLushort **indices, unsigned *nbIndices,
                     const float *left, const float *top,
                     const float *right, const float *bottom)
{
    *nbVertices = 4 * 6;
    *nbIndices = 6 * 6;

    *vertexCoord = vlc_alloc(*nbVertices * 3, sizeof(GLfloat));
    if (*vertexCoord == NULL)
        return VLC_ENOMEM;
    *textureCoord = vlc_alloc(nbPlanes * *nbVertices * 2, sizeof(GLfloat));
    if (*textureCoord == NULL)
    {
        free(*vertexCoord);
        return VLC_ENOMEM;
    }
    *indices = vlc_alloc(*nbIndices, sizeof(GLushort));
    if (*indices == NULL)
    {
        free(*textureCoord);
        free(*vertexCoord);
        return VLC_ENOMEM;
    }

    static const GLfloat coord[] = {
        -1.0,    1.0,    -1.0f, // front
        -1.0,    -1.0,   -1.0f,
        1.0,     1.0,    -1.0f,
        1.0,     -1.0,   -1.0f,

        -1.0,    1.0,    1.0f, // back
        -1.0,    -1.0,   1.0f,
        1.0,     1.0,    1.0f,
        1.0,     -1.0,   1.0f,

        -1.0,    1.0,    -1.0f, // left
        -1.0,    -1.0,   -1.0f,
        -1.0,     1.0,    1.0f,
        -1.0,     -1.0,   1.0f,

        1.0f,    1.0,    -1.0f, // right
        1.0f,   -1.0,    -1.0f,
        1.0f,   1.0,     1.0f,
        1.0f,   -1.0,    1.0f,

        -1.0,    -1.0,    1.0f, // bottom
        -1.0,    -1.0,   -1.0f,
        1.0,     -1.0,    1.0f,
        1.0,     -1.0,   -1.0f,

        -1.0,    1.0,    1.0f, // top
        -1.0,    1.0,   -1.0f,
        1.0,     1.0,    1.0f,
        1.0,     1.0,   -1.0f,
    };

    memcpy(*vertexCoord, coord, *nbVertices * 3 * sizeof(GLfloat));

    for (unsigned p = 0; p < nbPlanes; ++p)
    {
        float width = right[p] - left[p];
        float height = bottom[p] - top[p];

        float col[] = {left[p],
                       left[p] + width * 1.f/3,
                       left[p] + width * 2.f/3,
                       left[p] + width};

        float row[] = {top[p],
                       top[p] + height * 1.f/2,
                       top[p] + height};

        const GLfloat tex[] = {
            col[1] + padW, row[1] + padH, // front
            col[1] + padW, row[2] - padH,
            col[2] - padW, row[1] + padH,
            col[2] - padW, row[2] - padH,

            col[3] - padW, row[1] + padH, // back
            col[3] - padW, row[2] - padH,
            col[2] + padW, row[1] + padH,
            col[2] + padW, row[2] - padH,

            col[2] - padW, row[0] + padH, // left
            col[2] - padW, row[1] - padH,
            col[1] + padW, row[0] + padH,
            col[1] + padW, row[1] - padH,

            col[0] + padW, row[0] + padH, // right
            col[0] + padW, row[1] - padH,
            col[1] - padW, row[0] + padH,
            col[1] - padW, row[1] - padH,

            col[0] + padW, row[2] - padH, // bottom
            col[0] + padW, row[1] + padH,
            col[1] - padW, row[2] - padH,
            col[1] - padW, row[1] + padH,

            col[2] + padW, row[0] + padH, // top
            col[2] + padW, row[1] - padH,
            col[3] - padW, row[0] + padH,
            col[3] - padW, row[1] - padH,
        };

        memcpy(*textureCoord + p * *nbVertices * 2, tex,
               *nbVertices * 2 * sizeof(GLfloat));
    }

    const GLushort ind[] = {
        0, 1, 2,       2, 1, 3, // front
        6, 7, 4,       4, 7, 5, // back
        10, 11, 8,     8, 11, 9, // left
        12, 13, 14,    14, 13, 15, // right
        18, 19, 16,    16, 19, 17, // bottom
        20, 21, 22,    22, 21, 23, // top
    };

    memcpy(*indices, ind, *nbIndices * sizeof(GLushort));

    return VLC_SUCCESS;
}

static int BuildRectangle(unsigned nbPlanes,
                          GLfloat **vertexCoord, GLfloat **textureCoord, unsigned *nbVertices,
                          GLushort **indices, unsigned *nbIndices,
                          const float *left, const float *top,
                          const float *right, const float *bottom)
{
    *nbVertices = 4;
    *nbIndices = 6;

    *vertexCoord = vlc_alloc(*nbVertices * 3, sizeof(GLfloat));
    if (*vertexCoord == NULL)
        return VLC_ENOMEM;
    *textureCoord = vlc_alloc(nbPlanes * *nbVertices * 2, sizeof(GLfloat));
    if (*textureCoord == NULL)
    {
        free(*vertexCoord);
        return VLC_ENOMEM;
    }
    *indices = vlc_alloc(*nbIndices, sizeof(GLushort));
    if (*indices == NULL)
    {
        free(*textureCoord);
        free(*vertexCoord);
        return VLC_ENOMEM;
    }

    static const GLfloat coord[] = {
       -1.0,    1.0,    -1.0f,
       -1.0,    -1.0,   -1.0f,
       1.0,     1.0,    -1.0f,
       1.0,     -1.0,   -1.0f
    };

    memcpy(*vertexCoord, coord, *nbVertices * 3 * sizeof(GLfloat));

    for (unsigned p = 0; p < nbPlanes; ++p)
    {
        const GLfloat tex[] = {
            left[p],  top[p],
            left[p],  bottom[p],
            right[p], top[p],
            right[p], bottom[p]
        };

        memcpy(*textureCoord + p * *nbVertices * 2, tex,
               *nbVertices * 2 * sizeof(GLfloat));
    }

    const GLushort ind[] = {
        0, 1, 2,
        2, 1, 3
    };

    memcpy(*indices, ind, *nbIndices * sizeof(GLushort));

    return VLC_SUCCESS;
}

static int SetupCoords(vout_display_opengl_t *vgl,
                       const float *left, const float *top,
                       const float *right, const float *bottom)
{
    const struct vlc_gl_interop *interop = vgl->prgm.tc->interop;

    GLfloat *vertexCoord, *textureCoord;
    GLushort *indices;
    unsigned nbVertices, nbIndices;

    int i_ret;
    switch (vgl->fmt.projection_mode)
    {
    case PROJECTION_MODE_RECTANGULAR:
        i_ret = BuildRectangle(interop->tex_count,
                               &vertexCoord, &textureCoord, &nbVertices,
                               &indices, &nbIndices,
                               left, top, right, bottom);
        break;
    case PROJECTION_MODE_EQUIRECTANGULAR:
        i_ret = BuildSphere(interop->tex_count,
                            &vertexCoord, &textureCoord, &nbVertices,
                            &indices, &nbIndices,
                            left, top, right, bottom);
        break;
    case PROJECTION_MODE_CUBEMAP_LAYOUT_STANDARD:
        i_ret = BuildCube(interop->tex_count,
                          (float)vgl->fmt.i_cubemap_padding / vgl->fmt.i_width,
                          (float)vgl->fmt.i_cubemap_padding / vgl->fmt.i_height,
                          &vertexCoord, &textureCoord, &nbVertices,
                          &indices, &nbIndices,
                          left, top, right, bottom);
        break;
    default:
        i_ret = VLC_EGENERIC;
        break;
    }

    if (i_ret != VLC_SUCCESS)
        return i_ret;

    for (unsigned j = 0; j < interop->tex_count; j++)
    {
        vgl->vt.BindBuffer(GL_ARRAY_BUFFER, vgl->texture_buffer_object[j]);
        vgl->vt.BufferData(GL_ARRAY_BUFFER, nbVertices * 2 * sizeof(GLfloat),
                           textureCoord + j * nbVertices * 2, GL_STATIC_DRAW);
    }

    vgl->vt.BindBuffer(GL_ARRAY_BUFFER, vgl->vertex_buffer_object);
    vgl->vt.BufferData(GL_ARRAY_BUFFER, nbVertices * 3 * sizeof(GLfloat),
                       vertexCoord, GL_STATIC_DRAW);

    vgl->vt.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vgl->index_buffer_object);
    vgl->vt.BufferData(GL_ELEMENT_ARRAY_BUFFER, nbIndices * sizeof(GLushort),
                       indices, GL_STATIC_DRAW);

    free(textureCoord);
    free(vertexCoord);
    free(indices);

    vgl->nb_indices = nbIndices;

    return VLC_SUCCESS;
}

static void DrawWithShaders(vout_display_opengl_t *vgl, struct prgm *prgm)
{
    opengl_tex_converter_t *tc = prgm->tc;
    const struct vlc_gl_interop *interop = tc->interop;
    tc->pf_prepare_shader(tc, vgl->tex_width, vgl->tex_height, 1.0f);

    for (unsigned j = 0; j < interop->tex_count; j++) {
        assert(vgl->texture[j] != 0);
        vgl->vt.ActiveTexture(GL_TEXTURE0+j);
        vgl->vt.BindTexture(interop->tex_target, vgl->texture[j]);

        vgl->vt.BindBuffer(GL_ARRAY_BUFFER, vgl->texture_buffer_object[j]);

        assert(prgm->aloc.MultiTexCoord[j] != -1);
        vgl->vt.EnableVertexAttribArray(prgm->aloc.MultiTexCoord[j]);
        vgl->vt.VertexAttribPointer(prgm->aloc.MultiTexCoord[j], 2, GL_FLOAT,
                                     0, 0, 0);
    }

    vgl->vt.BindBuffer(GL_ARRAY_BUFFER, vgl->vertex_buffer_object);
    vgl->vt.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, vgl->index_buffer_object);
    vgl->vt.EnableVertexAttribArray(prgm->aloc.VertexPosition);
    vgl->vt.VertexAttribPointer(prgm->aloc.VertexPosition, 3, GL_FLOAT, 0, 0, 0);

    const GLfloat *tm = NULL;
    if (interop->ops && interop->ops->get_transform_matrix)
        tm = interop->ops->get_transform_matrix(interop);
    if (!tm)
        tm = identity;

    vgl->vt.UniformMatrix4fv(prgm->uloc.TransformMatrix, 1, GL_FALSE, tm);

    vgl->vt.UniformMatrix4fv(prgm->uloc.OrientationMatrix, 1, GL_FALSE,
                             prgm->var.OrientationMatrix);
    vgl->vt.UniformMatrix4fv(prgm->uloc.ProjectionMatrix, 1, GL_FALSE,
                             prgm->var.ProjectionMatrix);
    vgl->vt.UniformMatrix4fv(prgm->uloc.ViewMatrix, 1, GL_FALSE,
                             prgm->var.ViewMatrix);
    vgl->vt.UniformMatrix4fv(prgm->uloc.ZoomMatrix, 1, GL_FALSE,
                             prgm->var.ZoomMatrix);

    vgl->vt.DrawElements(GL_TRIANGLES, vgl->nb_indices, GL_UNSIGNED_SHORT, 0);
}


static void GetTextureCropParamsForStereo(unsigned i_nbTextures,
                                          const float *stereoCoefs,
                                          const float *stereoOffsets,
                                          float *left, float *top,
                                          float *right, float *bottom)
{
    for (unsigned i = 0; i < i_nbTextures; ++i)
    {
        float f_2eyesWidth = right[i] - left[i];
        left[i] = left[i] + f_2eyesWidth * stereoOffsets[0];
        right[i] = left[i] + f_2eyesWidth * stereoCoefs[0];

        float f_2eyesHeight = bottom[i] - top[i];
        top[i] = top[i] + f_2eyesHeight * stereoOffsets[1];
        bottom[i] = top[i] + f_2eyesHeight * stereoCoefs[1];
    }
}

static void TextureCropForStereo(vout_display_opengl_t *vgl,
                                 float *left, float *top,
                                 float *right, float *bottom)
{
    const struct vlc_gl_interop *interop = vgl->prgm.tc->interop;

    float stereoCoefs[2];
    float stereoOffsets[2];

    switch (vgl->fmt.multiview_mode)
    {
    case MULTIVIEW_STEREO_TB:
        // Display only the left eye.
        stereoCoefs[0] = 1; stereoCoefs[1] = 0.5;
        stereoOffsets[0] = 0; stereoOffsets[1] = 0;
        GetTextureCropParamsForStereo(interop->tex_count,
                                      stereoCoefs, stereoOffsets,
                                      left, top, right, bottom);
        break;
    case MULTIVIEW_STEREO_SBS:
        // Display only the left eye.
        stereoCoefs[0] = 0.5; stereoCoefs[1] = 1;
        stereoOffsets[0] = 0; stereoOffsets[1] = 0;
        GetTextureCropParamsForStereo(interop->tex_count,
                                      stereoCoefs, stereoOffsets,
                                      left, top, right, bottom);
        break;
    default:
        break;
    }
}

int
vlc_gl_renderer_Draw(struct vlc_gl_renderer *r)
{

}
