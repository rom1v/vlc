# Discussions about GPU deinterlacing


## Yadif

_The following explanations are based on my understanding based on the source
code. There are probably errors, please report them!_

In a deinterlaced video, the frames have been captured as follow (`t`=top,
`b`=bottom):

```
    +------+  +------+  +------+  +------+  +------+  +------+
    |tttttt|  |      |  |tttttt|  |      |  |tttttt|  |      |
    |      |  |bbbbbb|  |      |  |bbbbbb|  |      |  |bbbbbb|
    |tttttt|  |      |  |tttttt|  |      |  |tttttt|  |      |
    |      |  |bbbbbb|  |      |  |bbbbbb|  |      |  |bbbbbb|
    +------+  +------+  +------+  +------+  +------+  +------+
```

Both fields are stored in a single `picture_t`:

```
    +------+            +------+            +------+
    |tttttt|            |tttttt|            |tttttt|
    |bbbbbb|            |bbbbbb|            |bbbbbb|
    |tttttt|            |tttttt|            |tttttt|
    |bbbbbb|            |bbbbbb|            |bbbbbb|
    +------+            +------+            +------+
```

Yadif executes on 3 consecutive frames (if they exist):
 - the previous
 - the current
 - the next

The input picture of a specific filter call is the `next` picture. The history
provides the `current` and the `previous`. As a consequence, there is always 1
frame delay between the input and the output.

Yadif exists in two versions: normal (1x) and 2x.

The 1x version produces 1 output picture_t for 1 input picture_t (with 1 frame
delay). The framerate is the same as the input video.

```
    +------+            +------+            +------+
    |tttttt|            |tttttt|            |tttttt|
    |bbbbbb|            |bbbbbb|            |bbbbbb|  ...
    |tttttt|            |tttttt|            |tttttt|
    |bbbbbb|            |bbbbbb|            |bbbbbb|
    +------+            +------+            +------+
       ||                  ||                  ||
       \/                  \/                  \/
                        +------+            +------+            +------+
     first              |      |            |      |            |      |
     frame              |result|            |result|            |result|  ...
   undefined            |  1   |            |  2   |            |  3   |
                        |      |            |      |            |      |
                        +------+            +------+            +------+
```

The 2x version produces 2 output `picture_t` for 1 input `picture_t`. The
framerate is the double of the input video (and the same framerate as how the
video was captured):

```
    +------+            +------+            +------+
    |tttttt|            |tttttt|            |tttttt|
    |bbbbbb|            |bbbbbb|            |bbbbbb|  ...
    |tttttt|            |tttttt|            |tttttt|
    |bbbbbb|            |bbbbbb|            |bbbbbb|
    +------+            +------+            +------+
       ||                  ||                  ||
       \/                  \/                  \/
                        +------+  +------+  +------+  +------+
     first two          |      |  |      |  |      |  |      |
      frames            |result|  |result|  |result|  |result| ...
     undefined          |  1   |  |  2   |  |  3   |  |  4   |
                        |      |  |      |  |      |  |      |
                        +------+  +------+  +------+  +------+
```


## OpenGL filters

Currently (both on the _gpufilters_ branch and on my refactor on current
`master`), filters are managed by the OpenGL vout. Concretely, this means that
input pictures are received from the core via calls to `prepare()` and
`display()` callbacks on `vout_display_t`.

As a consequence, the core is responsible for presentation timing. The _vout
display_ displays the picture [as soon as possible][asap]. It may not delay or
produce new frames, with a different PTS.

[asap]: include/vlc_vout_display.h#L332

Therefore, as is, we can implement Yadif 1x, but not Yadif 2x.


## `filter_t`

To be able to implement an OpenGL filter producing a different framerate, a
solution could be to execute OpenGL filters from a `filter_t` (we would like to
be able to do that anyway). _Janni_ started [some work][filter_t_opengl] on the
subject.

[filter_t_opengl]: https://code.videolan.org/rom1v/vlc/-/commit/cae35480db22a7bd7e0cd9858a141561f4c6d26f

However, it means that the filter and the OpenGL vout will be independant (with
different OpenGL contexts).

It has advantages: we could apply an OpenGL filter without an OpenGL display
more easily (for _stream output_ for example). But it also means that once the
filter is applied in OpenGL, it must produce a `picture_t`, that will need to
be re-"imported" in the OpenGL vout.

Doesn't this invalidate all benefits from deinterlacing in GPU?
