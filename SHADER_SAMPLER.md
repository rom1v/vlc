# OpenGL chroma converters injection

We first implemented chroma converters as OpenGL filters (`vlc_gl_filter`),
which were automatically inserted to the filters list by the core as necessary.

This design has a major drawback: the chroma conversion is executed in a
separate step, introducing an additional copy.

The purpose of this patchset is to inject chroma conversion in the filter
program instead, by injecting code in its shader and initializing uniforms and
attributes as needed.

It is less general than merging arbitrary filters together, but it's simpler,
and it still allows to avoid an additional buffer copy in case we don't use any
explicit OpenGL filter (so that there is only chroma conversion and rendering).

## Principle

We introduce/replace the module capacity `"opengl chroma converter"`.

On loading, its role is to initialize a `vlc_gl_shader_sampler` object, that
will be used by the OpenGL filters modules:

```c
int Open(struct vlc_gl_chroma_converter *converter,
         video_format_t *fmt_in,
         video_format_t *fmt_out,
         struct vlc_gl_shader_sampler *sampler_out);
```

After this initialization, the `struct vlc_gl_shader_sampler` must contain:
 1. the source code to inject into the fragment shader of the filter;
 2. the implementation of functions to initialize and load/unload attributes and
    uniforms accordingly.

The OpenGL filters must then explicitly call the shader sampler functions to
manage attributes and uniforms.

There are several reasons why we let the filters call them explicitly (instead
of automatically calling them from the core):
 - the filter manages the OpenGL program (it might even use several programs
   internally)
 - calling them from the core would require to split the filters function into
   several steps, adding unnecessary complexity


## Chroma converter implementation

A chroma converter must provide a shader sampler.


### Fragment shader code

A chroma converter must generate fragment shader code to expose a function:

```glsl
vec4 vlc_texture(vec2 coords);
```

This function must return the color components in a single vector, as if they
were always stored in a single plane.

The components values must correspond to the requested output video format. For
example, if `fmt_out->i_chroma == VLC_CODEC_RGBA` then the returned vector
contains `(r, g, b, a)` values. On the other hand, if `fmt_out->i_chroma ==
VLC_CODEC_NV12`, it contains `(y, u, v, 1.0)`.

OpenGL filters must call `vlc_texture(coords)` instead of the built-in:

```glsl
vec4 texture2D(sampler2D sampler, vec2 coords)
```

so that they can retrieve the texels in the expected format, without knowing the
actual input format.

For illustration purpose, here is a possible fragment shader code in a
I420-to-RGB chroma converter:

```c
static const char *const FRAGMENT_CODE =
    "const mat3 conversion_matrix = mat3(\n"
    "    1.0,           1.0,          1.0,\n"
    "    0.0,          -0.21482,      2.12798,\n"
    "    1.28033,      -0.38059,      0.0\n"
    ");\n";
    "uniform sampler2D planes[3];\n"
    "vec4 vlc_texture(vec2 coords) {\n"
    "  vec3 v = conversion_matrix * vec3(\n"
    "              texture2D(planes[0], coords).x,\n"
    "              texture2D(planes[1], coords).x - 0.5,\n"
    "              texture2D(planes[2], coords).x - 0.5);\n"
    "  return vec4(v, 1.0);\n"
    "}\n";
```

### Preparing

The shader sampler may implement a `prepare()` function:

```c
int
(*prepare)(const struct vlc_gl_shader_program *program, void *userdata);
```

It will be called once, after the filter program (containing the injected shader
code) is compiled and linked.

Its purpose is typically to retrieve uniforms and attributes locations (in
particular, the location of the `sampler2D` uniforms, where the input texture is
stored).

### Loading

The shader sampler should implement a `load()` function:

```c
int
(*load)(const struct vlc_gl_picture *pic, void *userdata);
```

It will be called explicitly by the OpenGL filters, for every picture.

Its purpose is to load attributes and uniforms. Typically, it will bind the
picture textures and load the `sampler2D` uniforms (unless it does not use the
input picture):

```c
vt->ActiveTexture(GL_TEXTURE0);
vt->BindTexture(GL_TEXTURE_2D, pic->textures[0]);
vt->Uniform1i(sys->planes[0], 0);
```


### Unloading

The shader sampler may implement an `unload()` function:

```c
void
(*unload)(const struct vlc_gl_picture *pic, void *userdata);
```

It will be called explicitly by the OpenGL filters, for every picture.

Its purpose is to unbind textures:

```c
vt->ActiveTexture(GL_TEXTURE0);
vt->BindTexture(GL_TEXTURE_2D, 0);
```

_(Is it really necessary?)_



## OpenGL filters changes

### A new function `prepare()`

Initially, an OpenGL filter module typically compiled its program (containing
its vertex and fragment shaders) from its `Open()` function.

But `Open()` is also used to request an input format to the core, by overwriting
`fmt_in->i_chroma`. Since the shader code to inject will depend on the requested
chroma, the program cannot be compiled in `Open()` anymore.

For that purpose, a separate function must be implemented:

```c
int
(*prepare)(struct vlc_gl_filter *filter,
           const struct vlc_gl_shader_sampler *sampler);
```

The filter must compile and link its program from this function, retrieve its
attributes and uniforms locations, then call:

```c
int ret = vlc_gl_shader_sampler_Prepare(sampler, program);
```

to (typically) retrieve the attributes and uniforms locations in the shader
sampler.


### Loading data from the shader sampler

In its `filter()` function, the filter must call:

```c
int ret = vlc_gl_shader_sampler_Load(sampler, &input->picture);
```

(in addition to loading its own attributes and uniforms data), then draw, then
call:

```c
int ret = vlc_gl_shader_sampler_Unload(sampler, &input->picture);
```


## Identity transforms

### Identity chroma converter

A shader sampler must be provided even when the input and output chroma are the
same (a `vlc_texture()` function must still be injected so that filters can
retrieve texels).

For that purpose, an [`identity`][identity_cc] "chroma converter" with the
highest priority is implemented.

[identity_cc]: modules/video_output/opengl/chroma_converters/identity.c


### Identity filter

If a filter must blend with the current framebuffer using a different chroma, we
must convert chroma before blending.

This is done via an [`identity`][identity_filter] OpenGL filter, which just
draws the input picture. Since it uses the shader sampler, the necessary chroma
conversion will be injected automatically.

Similarly, if the blending filter is the first filter of the chain, there is no
framebuffer to blend with, so an identity filter must be inserted.

[identity_filter]: modules/video_output/opengl/filters/identity.c


## PoC

For now, an [I420] converter is implemented, and [`triangle_mask`] has been
adapted.

[I420]: modules/video_output/opengl/chroma_converters/i420.c
[`triangle_mask`]: modules/video_output/opengl/filters/triangle_mask.c

To test:

```bash
./vlc -Idummy --dec-dev=none --gl-filters=triangle_mask video.mkv
```

If we use the same filter twice, the chroma is still correct (it uses the
identity chroma converter between the first and the second):
```bash
./vlc -Idummy --dec-dev=none --gl-filters=triangle_mask:triangle_mask video.mkv
```
