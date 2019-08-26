OpenGL filters leverage the module system of VLC. Writing an OpenGL filter
consists of writing a VLC module which has the capability `opengl filter` and
implement the `vlc_gl_filter` API in the object given during the `Open`
function. Like any other module, implementors must define the mandatory
function pointers so as to have a valid module initialization and have their
module be considered as valid. A non-valid module might have undefined
behaviour, and will probably fail on assertions.

## Writing an opengl filter

An OpenGL filter must initialize the `filter` pointer function in the
`vlc_gl_filter` object so as to be considered as a valid module.  This function
is called each time a new filter input is to be processed by the module.

A filter input consists of the textures for the main video picture and the other
region created from the subpicture units (SPU). It also contains usual variables
which could be used for rendering, namely:
+ OrientationMatrix: describe texture coords change according to the rotation of
  the display.
+ ProjectionMatrix: classical projection matrix describing the projection on the
  screen.
+ ZoomMatrix: scaling matrix.
+ ViewMatrix: classical view matrix describing the camera transform.

These matrices are subject to change and might be removed later.

An OpenGL filter may be using an OpenGL shader program, which may be initialized
in the Open. If the shader is mandatory for the good behaviour of the filter,
the filter Open callback must return with a `VLC_EGENERIC` and free all its
resources. It should display an error message describing what prevented the
filter from working. If the shader program is optional and failed to build, it
may still return a `VLC_SUCCESS` code but should warn why the loading of the
filter failed and what consequences it has on the filter itself.

So as to load a program, the implementors may use their own method, but the
`vlc_gl_shader_builder` API might help so as to implement the previous
behaviour.

A shader builder is created through the `vlc_gl_shader_builder_Create` and must
be released through the `vlc_gl_shader_builder_Release`. Shader parts, like
vertex shader or fragment shader, can be defined through the use of
`vlc_gl_shader_AttachShaderSource`.
The final shader program can be obtained through `vlc_gl_shader_program_Create`
by giving the shader builder object.

The filters can work in two differents modes, which should be initialized during
the `Open` callback. It can either create its own new picture from the original
picture or the previous one, which is the default behaviour, or can also set the
`info.blend` flag to `true` in its structure to write directly on the previous
image result.

The `triangle` example [1] shows a basic OpenGL filter. This filter only blend a
triangle on the previous image, running a custom shader for colors.

[1]: see modules/video_output/opengl/filters/triangle.c

## Using filter parameters

OpenGL filters are getting temporary reading access to as `config_chain_t`
object, which allows to map the filter parameter to the filter object variables.

The main usage template for this `config_chain_t` is like the following snippet:

```
#define MYFILTER_CFG_PREFIX filter-
static const char **filter_options = { "option1", "option2", NULL };

/* ... */

static int Open(struct vlc_gl_filter *filter, config_chain_t *config)
{
    /* ... */
    config_ChainParse(filter, MYFILTER_CFG_PREFIX, filter_options, config);
    /* ... */
}

/* ... */

vlc_module_begin()
    /* ... */
    add_string(MYFILTER_CFG_PREFIX "option1", /* ... */)
    add_integer(MYFILTER_CFG_PREFIX "option2", /* ... */)
vlc_module_end()
```

This mechanism allow the user (be it the end user or another module using the
implementors's one) to set a global option, and then override it filter by
filter.

## Managing input textures

Textures are embedded into Opengl regions, which are also describing:
+ texture size.
+ texture rendering rectangle according to VLC core.


