# LibVLC playlist


## Purpose

We want a LibVLC API in addition to the core API because:
 1. clients do not link against _libvlccore_
 2. we want to be able to change the core API while keeping the LibVLC API
    unchanged (no breaking changes in libvlc)

Currently, the playlist component in _LibVLC_ ([`libvlc_media_list`] and
[`libvlc_media_list_player`]) is totally independant of the core playlist: it's
a separate API and implementation.

[`libvlc_media_list`]: include/vlc/libvlc_media_list.h
[`libvlc_media_list_player`]: include/vlc/libvlc_media_list_player.h

Instead, we would like the same playlist behavior (sort, random…) on all
platforms without reimplementing everything. So the implementation has to be
shared in some way.



## Constraints

In order to keep the LibVLC API stable (even on core API changes), we cannot
directly expose the core playlist types (`vlc_playlist_t`,
`vlc_playlist_item_t`…) or functions.

We could not even expose them as opaque types in LibVLC, since we need to store
LibVLC-specific data (and we may need to store more data in the future to
preserve the LibVLC behavior on core API changes).

So we need new types.


## API

For now, the idea is to get the very same API ([`libvlc_playlist.h`]) as the
core playlist ([`vlc_playlist.h`]), but with LibVLC-specific types. Concretely,
we just want new names:

 - replace `vlc_playlist` by `libvlc_playlist`
 - replace `vlc_playlist_item` by `libvlc_playlist_item`
 - replace `input_item_t` by `libvlc_media_t`
 - replace `libvlc_int_t` by `libvlc_instance_t`
 - …

[`libvlc_playlist.h`]: include/vlc/libvlc_playlist.h
[`vlc_playlist.h`]: include/vlc_playlist.h


## Possible solutions


### Copy-paste

We could copy-paste the core playlist, change the names, and _voilà_, we get a
LibVLC playlist implementation.

Of course, this is not good for maintenability (every bugfix in the core
implementation would need to be reported in the LibVLC implementation).

As an improvement, we could try to share the implementation in some way, but C
is not very helpful here. It has no templates, and writing the whole playlist
implementation as macros so that we can use different types is probably not a
good solution. Using `void *` would not be user-friendly (and it would create
strict aliasing problems, for example `type **` and `void **` are
"incompatible").

In addition, note that such an "independant" implementation would only work for
a playlist local to LibVLC: it would not allow a LibVLC client and a module to
use the same playlist (for example, an Android client enabling a remote control
provided by an HTTP module). _(Do we really need this anyway?)_


### Synchronization

The alternative is to provide a "LibVLC view" of a core playlist.

Concretely, a `vlc_playlist_t` contains a list of `vlc_playlist_item_t`, each
containing an `input_item_t`. It also owns a `vlc_player_t`.

A LibVLC client sees a `libvlc_playlist_t` containing a list of
`libvlc_playlist_item_t`, each containing a `libvlc_media_t`. It also owns a
`libvlc_media_player_t`. Every instance wraps the core version.

Concretely, the LibVLC playlist maintains a whole copy of the core playlist
items (a list of playlist items wrappers, each containing a `libvlc_media_t`
wrapping the `input_item_t` contained in the core playlist item), reacting and
forwarding the playlist events.


## Implementation

I implemented this second solution: [source][playlist_new] and
[tests][test_playlist].

[playlist_new]: lib/playlist_new.c
[test_playlist]: test/libvlc/playlist.c

But even if it works, it seems a bit insane/overcomplicated to me. Moreover, it
allocates a lot of wrappers. But I don't have a less worst solution.

Here is a summary of how it works.


### Principle

On creation, it instantiates a core playlist (with its player), creates a
LibVLC player wrapping the player.

The player and playlist ownership must be tracked. For example, a LibVLC player
may be created alone (so it owns its core player), but it can also wrap an
existing instance (the one from the core playlist). On delete, we must delete
the core component only if we own it.

The playlist registers a listener to react to the core playlist changes.


#### Core-to-LibVLC

When the core playlist notifies a change (for example `on_items_added`), the
LibVLC playlist must wrap all new items and reflect the changes in its local
items list. Then it notifies its own listeners of the changes.

However, wrapping implies allocation, which may fail. In that case, we cannot
simply ignore some events: the sequence of core playlist events must be _all_
applied, and _in order_. Otherwise, the behavior is undefined (a further event
could point to an index that does not exist locally, leading to a segfault).

On allocation error, we don't care if we lose some data, but we should not crash
(in theory).

We cannot just clear the core playlist content either, because the allocation
can fail from a callback. If we change the playlist content from there, another
listener will receive an event with an inconsistent playlist content, so it may
try to access invalid data, and segfaults.

Therefore, the solution I implemented is to "desynchronize" the LibVLC and the
core playlist on allocation error: in that case the LibVLC playlist exposes an
empty list to its clients, and the resynchronization may occur on a further
event (if all allocations work). A flag `must_resync` stores the current state.


### LibVLC-to-core

When a LibVLC client requests to insert items, it will pass `libvlc_media_t`
instances to `libvlc_playlist_(Request)Insert`. We just need to unwrap the
medias, and pass `input_item_t` instances and delegate the request to the core
playlist.

In return, the core playlist will notify the actual changes via callbacks. Since
we already do what is necessary in the callbacks, we could think it's
sufficient.

But it's not: the client requests to insert some `libvlc_media_t` instances, but
it will actually get rewrapped instances of the underlying `input_item_t`. This
is an unexpected behavior from the user API.

To avoid the problem, on client requests, before delegating to the core
playlist, we keep a pointer to the user-provided `libvlc_media_t` array
instances (`user_media`), so that we could reuse them on callbacks instead of
rewrapping.

Note that the actual changes may differ from the requested changes (cf details
about [playlist synchronization]), so it's not always straightforward, but it
works.

Concretely, we first check whether we already know a user-provided
`libvlc_media_t` instance for each item received from the core playlist; if we
don't, we wrap it (it may also happen, the playlist content can change
indepedantly of the LibVLC client).

On some operations (like shuffling), the list of `libvlc_media_t` instances must
be copied (and all media refcount incremented), because they generate the
`on_items_reset` event, which will release all existing items.


[playlist synchronization]: https://blog.rom1v.com/2019/05/a-new-core-playlist-for-vlc-4/#synchronization


## Comments

It works (or it seems to work). But IMO it's too much work (both at runtime and
to maintain) to synchronize the playlists. But I don't know what we could do
instead. So I'm interested in your feedbacks.
