#ifndef VLC_QT_PLAYLIST_MODEL_HPP_
#define VLC_QT_PLAYLIST_MODEL_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QAbstractListModel>
#include <QVector>
#include "playlist.hpp"

class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    Q_ENUMS(Roles)

public:
    enum Roles
    {
        TitleRole = Qt::UserRole,
    };

    PlaylistModel(Playlist *playlist, QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;

private slots:
    void onPlaylistCleared();
    void onPlaylistItemsAdded(size_t index, QVector<PlaylistItem>);
    void onPlaylistItemsRemoved(size_t index, QVector<PlaylistItem>);
    void onPlaylistItemUpdated(size_t index, PlaylistItem);
    void onPlaylistCurrentItemChanged(ssize_t index, PlaylistItem);

private:
    Playlist *playlist;
    QVector<PlaylistItem> items; /* access only from the UI thread */
};

#endif
