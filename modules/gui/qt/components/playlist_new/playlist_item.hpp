/*****************************************************************************
 * playlist_item.hpp
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

#ifndef VLC_QT_PLAYLIST_NEW_ITEM_HPP_
#define VLC_QT_PLAYLIST_NEW_ITEM_HPP_

#include <vlc_cxx_helpers.hpp>
#include <vlc_input_item.h>
#include <vlc_playlist_new.h>
#include <QExplicitlySharedDataPointer>

namespace vlc {
  namespace playlist {

using PlaylistItemPtr = vlc_shared_data_ptr_type(vlc_playlist_item_t,
                                                 vlc_playlist_item_Hold,
                                                 vlc_playlist_item_Release);

/**
 * Playlist item wrapper.
 *
 * It contains both the PlaylistItemPtr and cached data saved while the playlist
 * is locked, so that the fields may be read without synchronization or race
 * conditions.
 */
class PlaylistItem
{
public:
    PlaylistItem(vlc_playlist_item_t *item = nullptr)
    {
        if (item)
        {
            d = new Data();
            d->item.reset(item);
            sync();
        }
    }

    operator bool() const
    {
        return d;
    }

    vlc_playlist_item_t *raw() const {
        return d ? d->item.get() : nullptr;
    }

    QString getTitle() const
    {
        return d->title;
    }

    void sync() {
        input_item_t *media = vlc_playlist_item_GetMedia(d->item.get());
        vlc_mutex_lock(&media->lock);
        d->title = media->psz_name;
        vlc_mutex_unlock(&media->lock);
    }

private:
    struct Data : public QSharedData {
        PlaylistItemPtr item;

        /* cached values */
        QString title;
    };

    QExplicitlySharedDataPointer<Data> d;
};

/* PlaylistItem has the same size as a raw pointer */
static_assert(sizeof(PlaylistItem) == sizeof(void *));

  } // namespace playlist
} // namespace vlc

Q_DECLARE_METATYPE(vlc::playlist::PlaylistItem);

#endif
