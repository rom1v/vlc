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
#include <algorithm>
#include <assert.h>

namespace vlc {
  namespace playlist {

PlaylistModel::PlaylistModel(Playlist *playlist, QObject *parent)
    : QAbstractListModel(parent)
    , playlist(playlist)
{
    /* Do not use a Qt::AutoConnection, because in case the changes have been
     * requested from the Qt UI thread, the slot will be executed directly, like
     * with a Qt::DirectConnection, which would possibly break the order in
     * which the events received by the core playlist are handled. In that case,
     * the indices provided by the events would be invalid. */
    connect(playlist, &Playlist::playlistItemsReset,
            this, &PlaylistModel::onPlaylistItemsReset,
            Qt::QueuedConnection);
    connect(playlist, &Playlist::playlistItemsAdded,
            this, &PlaylistModel::onPlaylistItemsAdded,
            Qt::QueuedConnection);
    connect(playlist, &Playlist::playlistItemsMoved,
            this, &PlaylistModel::onPlaylistItemsMoved,
            Qt::QueuedConnection);
    connect(playlist, &Playlist::playlistItemsRemoved,
            this, &PlaylistModel::onPlaylistItemsRemoved,
            Qt::QueuedConnection);
    connect(playlist, &Playlist::playlistItemsUpdated,
            this, &PlaylistModel::onPlaylistItemsUpdated,
            Qt::QueuedConnection);
    connect(playlist, &Playlist::playlistCurrentItemChanged,
            this, &PlaylistModel::onPlaylistCurrentItemChanged,
            Qt::QueuedConnection);
}

void
PlaylistModel::notifyItemsChanged(int idx, int count, const QVector<int> &roles)
{
    QModelIndex first = index(idx, 0);
    QModelIndex last = index(idx + count - 1);
    emit dataChanged(first, last, roles);
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
PlaylistModel::onPlaylistItemsMoved(size_t index, size_t count, size_t target)
{
    size_t qtTarget = target;
    if (qtTarget > index)
        /* Qt interprets the target index as the index of the insertion _before_
         * the move, while the playlist core interprets it as the new index of
         * the slice _after_ the move. */
        qtTarget += count;

    beginMoveRows({}, index, index + count - 1, {}, qtTarget);
    if (index < target)
        std::rotate(items.begin() + index,
                    items.begin() + index + count,
                    items.begin() + target + count);
    else
        std::rotate(items.begin() + target,
                    items.begin() + index,
                    items.begin() + index + count);
    endMoveRows();
}

void
PlaylistModel::onPlaylistItemsRemoved(size_t index, size_t count)
{
    beginRemoveRows({}, index, index + count - 1);
    items.remove(index, count);
    endRemoveRows();
}

void
PlaylistModel::onPlaylistItemsUpdated(size_t index,
                                      QVector<PlaylistItem> updated)
{
    int count = updated.size();
    for (int i = 0; i < count; ++i)
    {
        assert(items[index + i].raw() == updated[i].raw());
        items[index + i] = updated[i]; /* sync metadata */
    }
    notifyItemsChanged(index, count);
}

void
PlaylistModel::onPlaylistCurrentItemChanged(ssize_t index)
{
    ssize_t oldCurrent = current;
    current = index;
    if (oldCurrent != -1)
        notifyItemsChanged(oldCurrent, 1, {IsCurrentRole});
    if (index != -1)
        notifyItemsChanged(index, 1, {IsCurrentRole});
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

const PlaylistItem &
PlaylistModel::itemAt(int index) const
{
    return items[index];
}

int
PlaylistModel::count() const
{
    return rowCount();
}

QVariant
PlaylistModel::data(const QModelIndex &index, int role) const
{
    switch (role)
    {
        case Qt::DisplayRole:
            // in QML, it will use custom roles for "columns" content
            // but for now, in Qt widgets, it use DisplayRole + column index
            // (that's stupid, the same model may not work both in
            // QListView/QTreeView and in QML components)
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
