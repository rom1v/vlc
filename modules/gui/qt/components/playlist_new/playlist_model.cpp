#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "playlist_model.hpp"

PlaylistModel::PlaylistModel(vlc_playlist_t *rawPlaylist, QObject *parent)
    : QAbstractListModel(parent)
    , playlist(rawPlaylist)
{
    connect(&playlist, &Playlist::playlistCleared,
            this, &PlaylistModel::onPlaylistCleared);
    connect(&playlist, &Playlist::playlistItemsAdded,
            this, &PlaylistModel::onPlaylistItemsAdded);
    connect(&playlist, &Playlist::playlistItemsRemoved,
            this, &PlaylistModel::onPlaylistItemsRemoved);
    connect(&playlist, &Playlist::playlistItemUpdated,
            this, &PlaylistModel::onPlaylistItemUpdated);
    connect(&playlist, &Playlist::playlistPlaybackRepeatChanged,
            this, &PlaylistModel::onPlaylistPlaybackRepeatChanged);
    connect(&playlist, &Playlist::playlistPlaybackOrderChanged,
            this, &PlaylistModel::onPlaylistPlaybackOrderChanged);
    connect(&playlist, &Playlist::playlistCurrentItemChanged,
            this, &PlaylistModel::onPlaylistCurrentItemChanged);
    connect(&playlist, &Playlist::playlistHasNextChanged,
            this, &PlaylistModel::onPlaylistHasNextChanged);
    connect(&playlist, &Playlist::playlistHasPrevChanged,
            this, &PlaylistModel::onPlaylistHasPrevChanged);
}

void
PlaylistModel::onPlaylistCleared()
{

}

void
PlaylistModel::onPlaylistItemsAdded(size_t index, QVector<PlaylistItem> items)
{

}

void
PlaylistModel::onPlaylistItemsRemoved(size_t index, QVector<PlaylistItem> items)
{

}

void
PlaylistModel::onPlaylistItemUpdated(size_t index, PlaylistItem item)
{

}

void
PlaylistModel::onPlaylistPlaybackRepeatChanged(
                                    enum vlc_playlist_playback_repeat repeat)
{

}

void
PlaylistModel::onPlaylistPlaybackOrderChanged(
                                    enum vlc_playlist_playback_order order)
{

}

void
PlaylistModel::onPlaylistCurrentItemChanged(ssize_t index, PlaylistItem)
{

}

void
PlaylistModel::onPlaylistHasNextChanged(bool hasNext)
{

}

void
PlaylistModel::onPlaylistHasPrevChanged(bool hasPrev)
{

}

QHash<int, QByteArray>
PlaylistModel::roleNames() const
{
    return {
        { TitleRole, "title" },
    };
}

int
PlaylistModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return items.size();
}

QVariant
PlaylistModel::data(const QModelIndex &index, int role) const
{
    switch (role)
    {
        case TitleRole:
            return items[index.row()].getMedia()->psz_name;
        default:
            return {};
    }
}


