# OpenGL importers

In order to support GPU filters (which can be chained), we need to refactor the
OpenGL video output.


## Current implementation

On current `master`, the _video helper_ uses an `opengl_tex_converter_t' struct.

The converter instance is initialized by a `"glconv"` modules
(`converter_vaapi.c`, `converter_vdpau.c`, etc.).

The module is responsible to initialize the whole `opengl_tex_converter`:

 - it must provide callbacks to uploads pictures to OpenGL textures
 - it must generate the whole fragment shader, which handles:
   - the texture access
   - the plane swizzle
   - the orientation
   - the chroma conversion
   - the rendering
 - it binds the uniforms and attributes for drawing every frame/SPU

Some parts of this initialization is common to all converter modules, so every
one call `opengl_fragment_shader_init()` (except `converter_android.c` for now,
but in the future it should also use the common code).


## Refactor

The purpose of this refactor is to minimize the scope of converter modules, to
only initialize what is hardware-specific: the **importer** part.

An **importer** is responsible to upload picture planes to OpenGL
textures, and keep track of some metadata about the format and context. It is
specific to the input picture format (software, VDPAU, VAAPI, Android, etc.). It
is loaded via the VLC module mechanism.

The remaining parts, including the generation of the whole fragment shader code
and the callbacks to prepare the shader for drawing every picture, are kept in
the `opengl_tex_converter_t`. Its is now initialized by the "core" (the
OpenGL vout module), i.e. not by hardware-specific `"glconv"` modules anymore.

This paves the way to replace the fragment shader generation and loading by
modular components (chroma converters, filters, renderers…), similar to what we
did in our GPU filters branch.


## Current state

 - `opengl_tex_converter_t` now have a field `struct vlc_gl_importer *importer`.
 - importer-specific fields of `opengl_tex_converter_t` have been moved to
   `vlc_gl_importer`.
 - the `glconv` modules now receives a `struct vlc_gl_importer` VLC object
 - they don't initialize the fragment shader anymore
   (`opengl_fragment_shader_init()` has been split), they just call
   `opengl_importer_init()` instead as a helper to initialize texture
   configuration from the input chroma
 - the OpenGL module initializes the fragment shader (not the "glconv" modules
   anymore)

## TODO

 - move `video_format_t` from converter to importer
 - remove `vlc_object_t` from `opengl_tex_converter_t`
 - reorganize code (move importer functions from `fragment_shader.c` to
   `importers.c` or similar)
 - adapt cvpx converter
 - rewrite the Android importer
 - write a proper patchset (`git rebase -i`)

At this point, a patchset could be submitted for merging.


## Next next steps

The `importer_sw` could be a made `"glconv"` module like the others.

Then, we should replace the "fragment shader" generation/loading by something
more modular (like on our GPU filters branch), to combine contributions from
chroma converters, renderer, etc.

Then, we could add a filter API.
