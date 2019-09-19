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

The [`triangle`] example shows a basic OpenGL filter. This filter only blend a
triangle on the previous image, running a custom shader for colors.

[`triangle`]: modules/video_output/opengl/filters/triangle.c

## Using filter parameters

OpenGL filters are getting temporary reading access to as `config_chain_t`
object, which allows to map the filter parameter to the filter object variables.

The main usage template for this `config_chain_t` is like the following snippet:

```
#define MYFILTER_CFG_PREFIX filter-
static const char * const filter_options[] = { "option1", "option2", NULL };

/* ... */
static void Close(struct vlc_gl_filter *filter)
{
    var_Destroy(filter, MYFILTER_CFG_PREFIX "option1");
    var_Destroy(filter, MYFILTER_CFG_PREFIX "option2");
}

static int Open(struct vlc_gl_filter *filter, config_chain_t *config)
{
    /* ... */
    config_ChainParse(filter, MYFILTER_CFG_PREFIX, filter_options, config);
    /* ... */

    filter->close = Close;
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

The example [`triangle_rotate`] shows the previous triangle with a `rotate`
parameter taking an angle in radian, which rotates the triangle.

[`triangle_rotate`]: modules/video_output/opengl/filters/triangle_rotate.c

## Managing input textures

Textures are embedded into Opengl regions, which are also describing:
+ texture size.
+ texture rendering rectangle according to VLC core.

When `filter.info.blend` is `false`, the input video texture coming from the
previous filter will be transfered to the filter into the `filter_input`
parameter.

The filter implementor can ask a specific chroma layout, and the result will
be converted into this one before the filter can access the picture. Most
filters will probably want to stay in `RGB` or `RGBA` format.

When the `filter` function is called, the filter implementor gets this
`vlc_gl_filter_input` object and can use `glBindTexture` to bind to the
current opengl rendering context.

The example [`triangle_mask`] shows a basic `RGB` example where the input
picture is extracted and masked, only drawn on a triangle.

[`triangle_mask`]: modules/video_output/opengl/filters/triangle_mask.c

## Using time in filters

With the features presented above, any pixel transformation or deformation can
be written as a fitler. However, advanced filters might need a way to produce
effects based on precise timing while playing the video, with a reproductible
behaviour.

To achieve this, filter input exposes a `picture_date` which correspond to the
PTS of the current frame, and a `rendered_date` which corresponds to the time at
which the picture will be rendered if not late.

The user wanting reproductibility of its effects might want to configure the
processing of its filter on top of timestamp related to `picture_date`.

The example [`clock.c`] shows a basic example of filter using time. It draws a
triangle which turns around at the speed of one turn per minute, corresponding
to the current time of the video.

## Complex pipeline control using filters

We saw that filters could get parameters through the `config_chain_t` object.
But this doesn't allow complex setup of the processing, for example to
configure the timing of predefined effects.

In addition, adding one filter per effect might create a long enough pipeline
to raise performance issues. So the effects must be mutualized, with care
taken for the order of execution.

To achieve this, the most common use case would be to introduce a command
pattern that the client application can leverage through intermediate file or
primitives like pipes.

This section explains the creation of such filter [`commandblend.c`] using an
intermediate file.

Example of command configuration file:

```
r 0 0 1000 1000
c 100 0 0
r 1000 1000 2000 200

s 1000000
r 0 0 2000 2000
e 2000000

s 4000000
r 0 0 2000 2000
e 5000000
```
