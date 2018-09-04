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

class Playlist : public QObject
{
    Q_OBJECT

public:
    Playlist(vlc_playlist_t *playlist, QObject *parent = nullptr);
    ~Playlist();

    vlc_playlist_t *raw();

    void lock();
    void unlock();

    void requestAddItems(size_t index, QVector<Media>);

signals:
    void playlistItemsReset(QVector<PlaylistItem>);
    void playlistItemsAdded(size_t index, QVector<PlaylistItem>);
    void playlistItemsRemoved(size_t index, size_t count);
    void playlistItemUpdated(size_t index, PlaylistItem);
    void playlistPlaybackRepeatChanged(enum vlc_playlist_playback_repeat);
    void playlistPlaybackOrderChanged(enum vlc_playlist_playback_order);
    void playlistCurrentItemChanged(ssize_t index);
    void playlistHasPrevChanged(bool hasPrev);
    void playlistHasNextChanged(bool hasNext);

private:
    vlc_playlist_t *playlist;
    vlc_playlist_listener_id *listener;
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
