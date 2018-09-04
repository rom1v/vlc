/*****************************************************************************
 * playlist.hpp
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

#ifndef VLC_QT_PLAYLIST_NEW_HPP_
#define VLC_QT_PLAYLIST_NEW_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QObject>
#include <QVector>
#include <vlc_playlist_new.h>
#include "media.hpp"
#include "playlist_item.hpp"

namespace vlc {
  namespace playlist {

/**
 * Core playlist wrapper.
 *
 * This wrapper redispatches the core playlist events as Qt signals, and
 * exposes functions to apply changes on the playlist.
 *
 * When a user requests to insert, move or remove items, before the core
 * playlist lock is successfully acquired, another client may have changed the
 * list. Therefore, this wrapper calls the vlc_playlist_Request*() functions
 * from the core playlist to solve conflicts automatically.
 *
 * The actual changes applied are notified through the callbacks (signals).
 */
class Playlist : public QObject
{
    Q_OBJECT

public:
    Playlist(vlc_playlist_t *playlist, QObject *parent = nullptr);
    ~Playlist();

    void attach();

    vlc_playlist_t *raw();

    void lock();
    void unlock();

    void append(const QVector<Media> &);
    void insert(size_t index, const QVector<Media> &);
    void move(const QVector<PlaylistItem> &, size_t target, ssize_t indexHint);
    void remove(const QVector<PlaylistItem> &, ssize_t indexHint);

    void shuffle();
    void sort(const QVector<vlc_playlist_sort_criterion> &);

signals:
    void playlistItemsReset(QVector<PlaylistItem>);
    void playlistItemsAdded(size_t index, QVector<PlaylistItem>);
    void playlistItemsMoved(size_t index, size_t count, size_t target);
    void playlistItemsRemoved(size_t index, size_t count);
    void playlistItemsUpdated(size_t index, QVector<PlaylistItem>);
    void playlistPlaybackRepeatChanged(enum vlc_playlist_playback_repeat);
    void playlistPlaybackOrderChanged(enum vlc_playlist_playback_order);
    void playlistCurrentItemChanged(ssize_t index);
    void playlistHasPrevChanged(bool hasPrev);
    void playlistHasNextChanged(bool hasNext);

private:
    vlc_playlist_t *playlist;
    vlc_playlist_listener_id *listener = nullptr;
};

class PlaylistLocker {
    Playlist *playlist;
public:
    PlaylistLocker(Playlist *playlist) : playlist(playlist)
    {
        playlist->lock();
    }
    ~PlaylistLocker()
    {
        playlist->unlock();
    }
};

  } // namespace playlist
} // namespace vlc

#endif
