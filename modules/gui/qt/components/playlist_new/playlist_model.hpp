/*****************************************************************************
 * playlist_model.hpp
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

#ifndef VLC_QT_PLAYLIST_NEW_MODEL_HPP_
#define VLC_QT_PLAYLIST_NEW_MODEL_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QAbstractListModel>
#include <QVector>
#include "playlist.hpp"

namespace vlc {
  namespace playlist {

class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    Q_ENUMS(Roles)

public:
    enum Roles
    {
        TitleRole = Qt::UserRole,
        IsCurrentRole,
    };

    PlaylistModel(Playlist *playlist, QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;

    /* provided for convenience */
    const PlaylistItem &itemAt(int index) const;
    int count() const;

private slots:
    void onPlaylistItemsReset(QVector<PlaylistItem>);
    void onPlaylistItemsAdded(size_t index, QVector<PlaylistItem>);
    void onPlaylistItemsMoved(size_t index, size_t count, size_t target);
    void onPlaylistItemsRemoved(size_t index, size_t count);
    void onPlaylistItemsUpdated(size_t index, QVector<PlaylistItem>);
    void onPlaylistCurrentItemChanged(ssize_t index);

private:
    void notifyItemsChanged(int index, int count,
                            const QVector<int> &roles = {});

    Playlist *playlist;

    /* access only from the UI thread */
    QVector<PlaylistItem> items;
    ssize_t current = -1;
};

  } // namespace playlist
} // namespace vlc

#endif
