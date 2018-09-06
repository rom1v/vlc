#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "playlist_model.hpp"

PlaylistModel::PlaylistModel(Playlist *playlist, QObject *parent)
    : QAbstractListModel(parent)
    , playlist(playlist)
{
    connect(playlist, &Playlist::playlistCleared,
            this, &PlaylistModel::onPlaylistCleared);
    connect(playlist, &Playlist::playlistItemsAdded,
            this, &PlaylistModel::onPlaylistItemsAdded);
    connect(playlist, &Playlist::playlistItemsRemoved,
            this, &PlaylistModel::onPlaylistItemsRemoved);
    connect(playlist, &Playlist::playlistItemUpdated,
            this, &PlaylistModel::onPlaylistItemUpdated);
    connect(playlist, &Playlist::playlistCurrentItemChanged,
            this, &PlaylistModel::onPlaylistCurrentItemChanged);
}

void
PlaylistModel::onPlaylistCleared()
{
    beginResetModel();
    items.clear();
    endResetModel();
}

void
PlaylistModel::onPlaylistItemsAdded(size_t index, QVector<PlaylistItem *> added)
{
    int len = added.size();
    beginInsertRows({}, index, index + len - 1);
    items.insert(index, len, nullptr);
    std::move(added.cbegin(), added.cend(), items.begin() + index);
    endInsertRows();
}

void
PlaylistModel::onPlaylistItemsRemoved(size_t index, size_t count)
{

}

void
PlaylistModel::onPlaylistItemUpdated(size_t index, PlaylistItem item)
{

}

void
PlaylistModel::onPlaylistCurrentItemChanged(ssize_t index)
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
            return items[index.row()]->getTitle();
        default:
            return {};
    }
}


