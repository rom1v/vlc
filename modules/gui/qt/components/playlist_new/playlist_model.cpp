/*****************************************************************************
 * playlist_model.cpp
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

#include "playlist_model.hpp"

namespace vlc {
  namespace playlist {

PlaylistModel::PlaylistModel(Playlist *playlist, QObject *parent)
    : QAbstractListModel(parent)
    , playlist(playlist)
{
    connect(playlist, &Playlist::playlistItemsReset,
            this, &PlaylistModel::onPlaylistItemsReset);
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
PlaylistModel::notifyItemChanged(int idx, const QVector<int> &roles)
{
    QModelIndex modelIndex = index(idx, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
}

void
PlaylistModel::onPlaylistItemsReset(QVector<PlaylistItem> newContent)
{
    beginResetModel();
    items.swap(newContent);
    endResetModel();
}

void
PlaylistModel::onPlaylistItemsAdded(size_t index, QVector<PlaylistItem> added)
{
    int count = added.size();
    beginInsertRows({}, index, index + count - 1);
    items.insert(index, count, nullptr);
    std::move(added.cbegin(), added.cend(), items.begin() + index);
    endInsertRows();
}

void
PlaylistModel::onPlaylistItemsRemoved(size_t index, size_t count)
{
    beginRemoveRows({}, index, index + count - 1);
    items.remove(index, count);
    endRemoveRows();
}

void
PlaylistModel::onPlaylistItemUpdated(size_t index, PlaylistItem item)
{
    assert(items[index].raw() == item.raw());
    items[index] = item; /* sync metadata */
    notifyItemChanged(index);
}

void
PlaylistModel::onPlaylistCurrentItemChanged(ssize_t index)
{
    ssize_t oldCurrent = current;
    current = index;
    if (oldCurrent != -1)
        notifyItemChanged(oldCurrent, {IsCurrentRole});
    if (index != -1)
        notifyItemChanged(index, {IsCurrentRole});
}

QHash<int, QByteArray>
PlaylistModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { IsCurrentRole, "isCurrent" },
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
            return items[index.row()].getTitle();
        case IsCurrentRole:
            return index.row() != -1 && index.row() == current;
        default:
            return {};
    }
}

  } // namespace playlist
} // namespace vlc
