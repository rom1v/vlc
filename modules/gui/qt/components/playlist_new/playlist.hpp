#ifndef VLC_QT_PLAYLIST_HPP_
#define VLC_QT_PLAYLIST_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QObject>
#include <QVector>
#include <vlc_playlist_new.h>
#include "playlist_item.hpp"

class Playlist : public QObject
{
    Q_OBJECT

public:
    Playlist(vlc_playlist_t *playlist, QObject *parent = nullptr);
    ~Playlist();

    vlc_playlist_t *raw();

signals:
    void playlistCleared();
    void playlistItemsAdded(size_t index, QVector<PlaylistItem>);
    void playlistItemsRemoved(size_t index, QVector<PlaylistItem>);
    void playlistItemUpdated(size_t index, PlaylistItem);
    void playlistPlaybackRepeatChanged(enum vlc_playlist_playback_repeat);
    void playlistPlaybackOrderChanged(enum vlc_playlist_playback_order);
    void playlistCurrentItemChanged(ssize_t index, PlaylistItem);
    void playlistHasNextChanged(bool hasNext);
    void playlistHasPrevChanged(bool hasPrev);

private:
    vlc_playlist_t *playlist;
    vlc_playlist_listener_id *listener;
};

#endif
