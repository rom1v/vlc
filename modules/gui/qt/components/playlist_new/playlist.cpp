/*****************************************************************************
 * playlist.cpp
 *****************************************************************************
 * Copyright (C) 2018 VLC authors and VideoLAN
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

#include "playlist.hpp"

namespace vlc {
  namespace playlist {

static QVector<PlaylistItem> toVec(vlc_playlist_item_t *const items[],
                                   size_t len)
{
    QVector<PlaylistItem> vec;
    for (size_t i = 0; i < len; ++i)
        vec.push_back(items[i]);
    return vec;
}

extern "C" { // for C callbacks

static void
on_playlist_items_reset(vlc_playlist_t *playlist,
                        vlc_playlist_item_t *const items[],
                        size_t len, void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistItemsReset(toVec(items, len));
}

static void
on_playlist_items_added(vlc_playlist_t *playlist, size_t index,
                        vlc_playlist_item_t *const items[], size_t len,
                        void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistItemsAdded(index, toVec(items, len));
}

static void
on_playlist_items_removing(vlc_playlist_t *playlist, size_t index,
                           size_t count, void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    /* we don't care the distinction between removing and removed */
    emit this_->playlistItemsRemoved(index, count);
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
                                 void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistCurrentItemChanged(index);
}

static void
on_playlist_has_prev_changed(vlc_playlist_t *playlist, bool has_prev,
                             void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistHasPrevChanged(has_prev);
}

static void
on_playlist_has_next_changed(vlc_playlist_t *playlist, bool has_next,
                             void *userdata)
{
    VLC_UNUSED(playlist);
    Playlist *this_ = static_cast<Playlist *>(userdata);
    emit this_->playlistHasNextChanged(has_next);
}

} // extern "C"

static const struct vlc_playlist_callbacks playlist_callbacks = {
    /* C++ (before C++20) does not support designated initializers */
    on_playlist_items_reset,
    on_playlist_items_added,
    on_playlist_items_removing,
    on_playlist_item_updated,
    on_playlist_playback_repeat_changed,
    on_playlist_playback_order_changed,
    on_playlist_current_item_changed,
    on_playlist_has_prev_changed,
    on_playlist_has_next_changed,
};

Playlist::Playlist(vlc_playlist_t *playlist, QObject *parent)
    : QObject(parent)
    , playlist(playlist)
{
    listener = vlc_playlist_AddListener(playlist, &playlist_callbacks, this);
    if (!listener)
        throw std::bad_alloc();
}

void Playlist::lock()
{
    vlc_playlist_Lock(playlist);
}

void Playlist::unlock()
{
    vlc_playlist_Unlock(playlist);
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

void
Playlist::requestAddItems(size_t index, QVector<Media> media)
{
    PlaylistLocker locker(this);

    size_t playlistSize = vlc_playlist_Count(playlist);
    if (index < playlistSize)
        index = playlistSize;

    int count = media.size();
    QScopedArrayPointer<input_item_t *> rawMedia(new input_item_t*[count]);

    for (int i = 0; i < count; ++i)
        rawMedia[i] = media[i].raw();

    int ret = vlc_playlist_InsertAll(playlist, index, rawMedia.data(), count);
    if (ret != VLC_SUCCESS)
        throw std::bad_alloc();
}

  } // namespace playlist
} // namespace vlc
