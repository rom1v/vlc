# LibVLC playlist

I started to implement a LibVLC API of the playlist, and I'm not sure we should
do it that way.

The [LibVLC API][libvlc_playlist.h] is almost exactly the same as the [core
API][vlc_playlist_new.h]:

 - `s/vlc_playlist/libvlc_playlist/g`
 - `s/input_item_t/libvlc_media_t/g`
 - `s/libvlc_int_t/libvlc_instance_t/g`

[libvlc_playlist.h]: include/vlc/libvlc_playlist.h
[vlc_playlist_new.h]: include/vlc_playlist_new.h


## Purpose

We want a LibVLC API in addition to the core API because:
 1. clients do not link against _libvlccore_
 2. we want to be able to change the core API while keeping the LibVLC API
    unchanged (no breaking change in libvlc)

(2) implies that every structure (even opaque) of the core API must be wrapped
into a libvlc structure. Recursively.

For exemple, a `vlc_playlist_t` contains a list of `vlc_playlist_item_t`, each
containing an `input_item_t`.  A LibVLC client sees a `libvlc_playlist_t`
containing a list of `libvlc_playlist_item_t`, each containing a
`libvlc_media_t`.


## Store a copy in LibVLC?

The core playlist store the "official" playlist content. For [synchronization
purpose][desynchronization], every UI will store a copy of the playlist.

[desynchronization]: WIP-playlist.md#desynchronization

Consider the Android use case. There are several layers:

 - the core playlist
 - the LibVLC playlist (Android cannot access the core)
 - the Java/JNI bindings of the LibVLC playlist
 - the UI playlist (a copy)

If we decide to copy at every level, the Android application will always have 4
copies of the playlist and its items (and wrappers of everything for every
layer). Maybe this is not ideal.

On the other hand, we need to keep the wrapper instances at every level if we
want to identify the items by their instance. In other words, if we don't keep a
copy, `libvlc_playlist_Get(playlist, index)` (for example) will always return a
new instance of the wrapper.

I think we'll have the problem once anyway to map the Java playlist with the C
playlist. But here, we have it twice.


## Wrapping

I started the implementation ([`playlist_new.c`]), for now without storing a
copy of items (I create the wrappers on-the-fly on events, even if I think a
copy would be better).

[`playlist_new.c`]: https://github.com/rom1v/vlc/blob/playlist/wip/lib/playlist_new.c

To illustrate the wrapping, I implemented [`AddListener()`] and a callback.

[`AddListener()`]: lib/playlist_new.c#L247-L250

It wraps the `libvlc_playlist_t`, the `libvlc_playlist_callbacks` and `userdata`
provided by the client, so that we can retrieve the LibVLC version to delegate
on core playlist events (every callback function needs to be wrapped).

On core playlist events (e.g. [`on_items_reset()`]), we need to wrap the event
parameters to delegate to the LibVLC version of the callbacks.

[`on_items_reset()`]: lib/playlist_new.c#L226

Concretely, the core playlist notifies the list of items added (or reset), the
callback wrapper must create a LibVLC version copy of this list of items:

 - allocate a new array of `libvlc_playlist_item_t`;
 - allocate _count_ `libvlc_playlist_item_t` to wrap each `vlc_playlist_item_t`;
 - allocate _count_ `libvlc_media_t` to wrap each `input_item_t` contained in
   `vlc_playlist_item_t`;
 - call the LibVLC version of the callback;
 - destroy all the wrappers (or store them if we want to keep the wrappers
   instance).

By the way, this adds another problem: what to do on allocation error in the
core playlist callback? In that case, we cannot notify the client [correctly],
whereas the consistent state of the client version of the playlist requires to
handle all events in order.

[correctly]: lib/playlist_new.c#L235-L238

In addition to the runtime overhard (in memory and CPU), it adds a lot of
complexity, and modifying the core API will require a huge amount of work to
adapt all the layers. And this is just the playlist, the player must also be
exposed.

Could the wrappers be "generated" to simplify? I don't think so: for example, we
cannot deduce the [ownership] from the C source code. And if it's fully
automated, then the LibVLC API will necessarily match the core API, and the
additional LibVLC layer would have (almost) no purpose.

[ownership]: https://github.com/rom1v/vlc/blob/playlist/wip/lib/playlist_new.c#L33-L50

## Eat your own dog food

If we accept that the LibVLC API must match the core API (or more precisely be a
subset of the core API), then we should probably not duplicate it.

In theory, the playlist could be implemented in LibVLC, and the core would just
use it (it would be great to use the same API as the one exposed to external
applications). It would require to split VLC differently, because _vlccore_
cannot depend on _libvlc_ if _libvlc_ depends on _vlccore_ (maybe an additional
component used by both _vlccore_ and _libvlc_, or whatever).

What do you think?
