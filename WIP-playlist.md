# New playlist

For VLC 4.0, we would like a new playlist, containing a simple _list_ of items
(not a _tree_) feeding a player, without additional unrelated responsibilities
(e.g. manage services discovery, etc.).

Here is a draft documentation of an API and implementation proposal. Feedbacks
welcome.


## Principles

The main playlist, and possibly other instances (e.g. for VLM), are intended to
be created in the _core_. They may be exposed and modified by any number of
interfaces (typically one, but there may be several simultaneously).

The API is intended to be simple, UI-friendly and allow for an implementation
both correct (no race conditions) and performant for common use cases.


### UI-friendly

UI frameworks typically use _list models_ / _adapters_ to provide a list of
items to a _list view_ component.

A _list model_ require to implement functions to:
 - return the total number of items,
 - return the item at a given index.

In addition, it must notify the view when changes occur:
 - when items are inserted (providing _index_ and _count_),
 - when items are removed (providing _index_ and _count_),
 - when items are moved (providing _index_, _count_, and _target index_),
 - when items are updated (providing _index_ and _count_),
 - when the model is reset (the whole content should be considered changed).

For example, Qt list views use [`QAbstractItemModel`]/[`QAbstractListModel`] and
Android recycler view use [`RecyclerView.Adapter`].

[`QAbstractItemModel`]: http://doc.qt.io/qt-5/qabstractitemmodel.html
[`QAbstractListModel`]: http://doc.qt.io/qt-5/qabstractlistmodel.html
[`RecyclerView.Adapter`]: https://developer.android.com/reference/android/support/v7/widget/RecyclerView.Adapter

To make it easy-to-use and performant for clients (UI), the playlist API should
expose similar callbacks.


### Desynchronization

The main playlist is owned by the core, and may be modified from any thread
(with appropriate synchronization). In VLC, each UI typically lives in a
separate thread (the UI thread).

**The core playlist may not be used as a direct data source for a _list
model_.** In other word, a _list model_ functions must not delegate the calls to
the playlist. This would require locking the playlist individually for each call
to get the count and retrieve each item (which is, in itself, not a good idea
for UI responsiveness), and would not be sufficient to guarantee correctness:
the playlist content could change between view calls so that a request to
retrieve an item at a specific index could be invalid (which would break the
_list model_ expected behavior).

As a consequence, the UI playlist should be considered as a **remote out-of-sync
view** of the core playlist. This implies that the UI needs to keep a copy of
the playlist content (there is nothing surprising here, it's like the current
playlist).

Note that the copy must not limited to the list of playlist items (pointers)
themselves, but also to the items content which is displayed and susceptible to
change asynchronously (e.g. media metadata, like title or duration). **The UI
should never lock a media (input item) for rendering a playlist item**;
otherwise, the content could be changed (and exposed) before the _list model_
notified the view of this change (which, again, would break the _list model_
expected behavior).


### Synchronization

The core playlist and UI playlist(s) are out-of-sync. So we need to
"synchronize" them:
 - the changes on the core playlist must be reflected to the UI views,
 - UI may request changes to the core playlist (to add, move or remove items).


#### Core to UI

The core playlist is the _source of truth_.

Every change to the UI playlist must occur in the UI thread, yet the core
playlist notification handlers are executed in any thread. Therefore, in the UI,
every callback handler must "post" the events to the UI thread event loop, and
execute suitable actions from the UI thread. From there, **the core playlist
is out-of-sync, so it would be incorrect to read the core playlist**.

The events forwarded to the UI thread event loop are still **in order**.
Therefore, if the UI playlist content is only changed on core playlist events,
**the indices notified by the core playlist are necessarily valid in the context
of the _list model_ in the UI thread**.

This means for example that providing only the range of a removal is sufficient
for the core playlist: the range can be removed safely by the _list model_,
**without any lookup** (which is great for simplicity and performance).

For that purpose, **the _list model_ must be read-only.** In other words, it is
incorrect to add or remove items directly in the _list model_ (only the core
playlist callbacks handlers can modify the model).

For performance reasons, the number of triggered events should be minimized.
For example, when several contiguous items are inserted (e.g. an item has been
expanded, or another interface inserted items), only one event should be
triggered, to avoid inserting (i.e. shifting the following items) several
times.


#### UI to core

The other direction is less simple.

Suppose the user selects items 10 to 20, and drag&drop to move them to
index 42: once the user "drops" the items, we take the playlist lock,
and want to apply these changes to the core playlist.

The problem is, before we successfully acquired the lock, another client may
have changed the list (e.g. cleared or shuffled it, or removed items 5 to 15,
moved item at index 42 to index 10, etc.).

Therefore, we cannot really apply the "move" request as is, because it was
created from a previous state.

To solve the issue, we decided to adapt the request to make it fit the current
playlist state (in other words, resolve conflicts): find the items if they had
been moved, ignore the items not found for removal, etc.

Nevertheless, in practice, it is very likely that the request actually applies
exactly to the current state of the playlist (typically, when we use only one
interface). In that case, we would like to get computational complexity similar
to a "direct" range move or removal (concretely, we would like to avoid a linear
lookup for every item).

For that purpose, the UI should provide an _index hint_ when requesting a
change. Once the playlist lock is acquired, the item is first searched according
the _index hint_, which should (hopefully) almost always match.


### Random playback

Random playback does not change the items location; instead, it does not play
the items sequentially.

The random playback must never select an item already played (unless all have
already been played). In other words, in an item containing _n_ items, if _n_
items are played, all must be selected exactly once.

In random playback mode, the "previous" button must go back to the previous
items played.

In addition, we should be able to insert/move/remove items in the playlist at
any time, the previous rule must still apply.



## API and implementation

The API is provided by [`include/vlc_playlist_new.h`][vlc_playlist_new.h], and
implemented by [`src/playlist/playlist.c`][playlist.c].

[vlc_playlist_new.h]: include/vlc_playlist_new.h
[playlist.c]: src/playlist/playlist.c

### Overview

A playlist contains a list of _playlist items_. Currently, a _playlist item_
contains only a media (`input_item_t`), but there could be
playlist-item-specific data in the future, which should not be associated to the
input item itself.

A media (`input_item_t`) instance must be unique in the playlist, and should not
be used anywhere else (e.g. in services discovery); otherwise its properties
might change (due to an external preparsing) without the playlist being
notified. To avoid this, an existing media should be copied.

The clients may listen to playlist events by registering a listener
(`vlc_playlist_AddListener()`).

In particular, events matching the _list model_ events are exposed:
 - `on_items_reset`
 - `on_items_added`
 - `on_items_moved`
 - `on_items_removed`
 - `on_items_updated`

The playlist creates and owns its _player_.

Most of the functions require to lock the playlist.

In the functions `Insert()`, `Move()` and `Remove()`, the items are selected by
index, which must be valid (in range).

If a client wants to apply more complex changes based on items (see
[synchronization](#synchronization)), they can lock the playlist, find the items
they want, apply their changes, then unlock the mutex.

For convenience, the API also exposes functions to request
[desynchronized](#desynchronization) changes:
 - `RequestInsert()`
 - `RequestMove()`
 - `RequestRemove()`

(`RequestAppend()` is not necessary, since conflicts are impossible, we can
always append items.)

The playlist also exposes "has previous", "has changed" and "current item"
properties, intended to update UI state accordingly (e.g. disable the "previous"
button).

Preparsing an item of the playlist must be requested via
`vlc_playlist_Preparse()`: on subitems detected, the media is _expanded_
directly in the playlist (i.e. replaced by its flatten subtree).


### Data structure

The items are stored in a contiguous dynamic array ([`vlc_vector`], [last
version][vector-github]).

This provides, for a playlist of _n_ items:
 - random access in 0(1),
 - appending in amortized O(1),
 - insertions of m items in O(n+m),
 - removal of m contiguous items by index in O(n) (or O(n*m) in random playback
   mode),
 - removal of m non-contiguous items in O(n*m).

[`vlc_vector`]: https://mailman.videolan.org/pipermail/vlc-devel/2018-September/121081.html
[vector-github]: https://github.com/rom1v/vlc/blob/vector/rfc6.2/include/vlc_vector.h


### Locking

To be able to combine multiple calls atomically, functions to lock and unlock
are exposed in the API. All callbacks are called with lock held, both to protect
the listeners and guarantee a consistent state during the execution of callback
handlers.

The playlist and its player are tightly coupled. For instance, the player
requests the next item to play via a _callback_/_provider_, and the playlist
controls the player to change its current media.

This poses a lock-order inversion problem:
 - on user actions, we need to lock the playlist, then (possibly) lock the
   player;
 - on player events, we need to lock the player, then the playlist.

Therefore, deadlocks might occur (thread A locked the playlist and waits for the
player lock, thread B locked the player and waits for the playlist lock).

To avoid the problem, the player and the playlist **share the same lock**.
Concretely, `vlc_playlist_Lock()` delegates to `vlc_player_Lock()`.


### Random playback

In addition to the playlist _vector_, we need to store the shuffled version of
the items, not to play the same item twice before all items have been played.

An helper ([`randomizer.h`]/[`randomizer.c`]) provides the expected random
playback rules (in a generic way, it handles `void *`, in practice the items
will be `playlist_item_t *`).

To be efficient, it does not shuffle the whole content in advance at once, but
executes only one step of the [Fish-Yates algorithm][Fisher-Yates] when a new
item is requested.

The benefit is not (mainly) to avoid to execute the whole algorithm, but rather
the fact that adding a new item to the playlist does not require a new shuffle,
and removing an item does not require any shifting in the _randomizer_ vector
(the item can be _swap-removed_), unless the item had already been played.

[Fisher-Yates]: https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle

It is in WIP state and is not plugged into the playlist yet.

[`randomizer.h`]: src/playlist/randomizer.h
[`randomizer.c`]: src/playlist/randomizer.c



### Sorting

_(For correctness, to sort the playlist according a media property, we must lock
all the media (input items), then sort, then unlock all.)_

Not implemented yet.


### Shuffling

This feature shuffles the items in the playlist: their location change, contrary
to the random playback.

Not implemented yet.

This will be implemented by the [Fisher-Yates algorithm][Fisher-Yates].



## UI

In order to test the core API, I implemented a Qt _list model_ bound to the core
playlist.


### Playlist proxy

The C++ playlist ([`playlist.hpp`]/[`playlist.cpp`]) wraps the core playlist to
redispatch the events as [Qt signals].

It also exposes methods to request appending, insertions, moves and removals
(see [UI to core](#ui-to-core)).

[`playlist.cpp`]: modules/gui/qt/components/playlist_new/playlist.cpp
[`playlist.hpp`]: modules/gui/qt/components/playlist_new/playlist.hpp
[Qt signals]: https://doc.qt.io/qt-5/signalsandslots.html



### List model

The playlist model ([`playlist_model.hpp`]/[`playlist_model.cpp`]), inheriting
[`QAbstractListModel`], stores a `QVector<PlaylistItem>` (the copy
[desynchronized](#desynchronization) from the core playlist).

It lives in the UI thread (assuming it is created from the UI thread), and
defines _slots_ that it connects to the `Playlist` signals, in which it updates
its items (see [core to UI](#core-to-ui)). **The items must never be updated
outside the handlers of the core playlist callbacks.**

`PlaylistItem` ([`playlist_item.hpp`]) is a wrapper to a core playlist item
containing:
 - the resource wrapper to the `vlc_laylist_item_t` instance for automatic
   memory management (see [`vlc_shared_data_ptr`]),
 - a copy of the displayed values read during the callback handler (see
   [desynchronization](#desynchronization)).

`PlaylistItem` is implemented so that [its size is that of a pointer][sizeof].
That way, inserting or removing items from the `QVector<PlaylistItem>` does not
generate unnecessary additional costs.

[`playlist_model.cpp`]: modules/gui/qt/components/playlist_new/playlist_model.cpp
[`playlist_model.hpp`]: modules/gui/qt/components/playlist_new/playlist_model.hpp
[`playlist_item.hpp`]: modules/gui/qt/components/playlist_new/playlist_item.hpp
[sizeof]: modules/gui/qt/components/playlist_new/playlist_item.hpp#L82-L83
[`vlc_shared_data_ptr`]: https://mailman.videolan.org/pipermail/vlc-devel/2018-September/121188.html


### Modifying the playlist

The playlist may be modified by the functions resolving conflicts on concurrent
changes (see [UI to core](#ui-to-core)). The _list model_ will be modified
indirectly via the core playlist callbacks.

Concretely, when a client (UI) requests _desynchronized_ changes to the core
playlist, it receives the changes actually applied via the playlist callbacks.
For example, if a user requests to remove the slice of items 5-6-7-8-9, and the
playlist has been shuffled by another client before this change is applied, the
UI will receive the actual changes as a sequence of callbacks; for instance:
 - `on_item_removed` with index=13 and count=2 (remove 13-14)
 - `on_item_removed` with index=9 and count=1 (remove 9)
 - `on_item_removed` with index =3 and count = 2 (remove 3-4)

That way, all clients are kept synchronized easily.

These methods can be called either from the UI thread or from a separate thread:
even if they lock the playlist, their execution should be quick
enough, even if they include the execution of all callback handlers. If it is
not the case, their execution may be moved to a worker thread.

*Warning:* If they are called from the UI thread, the core playlist callbacks
will be executed during the call. In that case, the events must be posted to the
event queue, even if the target thread is already the UI thread, to avoid
breaking the order of events. Concretely, in Qt, this requires to use
[`Qt::QueuedConnection`][ConnectionType] instead of the default
`Qt::AutoConnection` when connecting signals to slots (see
[`PlaylistModel`][queued]).


[`media.hpp`]: modules/gui/qt/components/playlist_new/media.hpp
[ConnectionType]: http://doc.qt.io/qt-5/qt.html#ConnectionType-enum
[queued]: modules/gui/qt/components/playlist_new/playlist_model.cpp#L34-L38


## Next steps

 - implement random playback
 - implement "repeat" mode
 - implement sort
 - implement shuffle
 - test the interaction with the player (and check if the API is sufficient,
   e.g.  should the playlist handle the playback state, or let the client do
   it?)
 - use the playlist model in the new UI
 - instantiate the main playlist in the core
