#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "playlist.hpp"

extern "C" { // for C callbacks

static void
on_playlist_cleared(vlc_playlist_t *playlist, void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistCleared();
}

static QVector<PlaylistItem> toVec(vlc_playlist_item_t *items[], size_t len)
{
    QVector<PlaylistItem> vec;
    for (size_t i = 0; i < len; ++i)
        vec.push_back(items[i]);
    return vec;
}

static void
on_playlist_items_added(vlc_playlist_t *playlist, size_t index,
                        vlc_playlist_item_t *items[], size_t len, void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistItemsAdded(index, toVec(items, len));
}

static void
on_playlist_items_removed(vlc_playlist_t *playlist, size_t index,
                          vlc_playlist_item_t *items[], size_t len,
                          void *userdata)
{
    VLC_UNUSED(playlist);
    VLC_UNUSED(items);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistItemsRemoved(index, len);
}

static void
on_playlist_item_updated(vlc_playlist_t *playlist, size_t index,
                         vlc_playlist_item_t *item, void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistItemUpdated(index, item);
}

static void
on_playlist_playback_repeat_changed(vlc_playlist_t *playlist,
                                    enum vlc_playlist_playback_repeat repeat,
                                    void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistPlaybackRepeatChanged(repeat);
}

static void
on_playlist_playback_order_changed(vlc_playlist_t *playlist,
                                   enum vlc_playlist_playback_order order,
                                   void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistPlaybackOrderChanged(order);
}

static void
on_playlist_current_item_changed(vlc_playlist_t *playlist, ssize_t index,
                                 vlc_playlist_item_t *item, void *userdata)
{
    VLC_UNUSED(playlist);
    VLC_UNUSED(item);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistCurrentItemChanged(index);
}

static void
on_playlist_has_next_changed(vlc_playlist_t *playlist, bool has_next,
                             void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistHasNextChanged(has_next);
}

static void
on_playlist_has_prev_changed(vlc_playlist_t *playlist, bool has_prev,
                             void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistHasPrevChanged(has_prev);
}

} // extern "C"

static const struct vlc_playlist_callbacks playlist_callbacks = {
    /* C++ (before C++20) does not support designated initializers */
    on_playlist_cleared,
    on_playlist_items_added,
    on_playlist_items_removed,
    on_playlist_item_updated,
    on_playlist_playback_repeat_changed,
    on_playlist_playback_order_changed,
    on_playlist_current_item_changed,
    on_playlist_has_next_changed,
    on_playlist_has_prev_changed,
};

Playlist::Playlist(vlc_playlist_t *playlist, QObject *parent)
    : QObject(parent)
    , playlist(playlist)
{
    listener = vlc_playlist_AddListener(playlist, &playlist_callbacks, this);
    if (!listener)
        throw std::bad_alloc();
}

Playlist::~Playlist()
{
    vlc_playlist_RemoveListener(playlist, listener);
}

vlc_playlist_t *
Playlist::raw()
{
    return playlist;
}
