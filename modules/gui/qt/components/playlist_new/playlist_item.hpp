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

#ifndef VLC_QT_PLAYLIST_ITEM_HPP_
#define VLC_QT_PLAYLIST_ITEM_HPP_

#include <vlc_input_item.h>
#include <vlc_playlist_new.h>
#include <QSharedData>

namespace vlc {
  namespace playlist {

class PlaylistItemPtr
{
public:
    PlaylistItemPtr(vlc_playlist_item_t *ptr = nullptr)
        : ptr(ptr)
    {
        if (ptr)
            vlc_playlist_item_Hold(ptr);
    }

    PlaylistItemPtr(const PlaylistItemPtr &other)
        : PlaylistItemPtr(other.ptr) {}

    PlaylistItemPtr(PlaylistItemPtr &&other) noexcept
        : ptr(std::exchange(other.ptr, nullptr)) {}

    ~PlaylistItemPtr()
    {
        if (ptr)
            vlc_playlist_item_Release(ptr);
    }

    PlaylistItemPtr &operator=(const PlaylistItemPtr &other)
    {
        if (other.ptr)
            /* hold before in case ptr == other.ptr */
            vlc_playlist_item_Hold(other.ptr);
        if (ptr)
            vlc_playlist_item_Release(ptr);
        ptr = other.ptr;
        return *this;
    }

    PlaylistItemPtr &operator=(PlaylistItemPtr &&other) noexcept
    {
        ptr = std::exchange(other.ptr, nullptr);
        return *this;
    }

    bool operator==(const PlaylistItemPtr &other)
    {
        return ptr == other.ptr;
    }

    bool operator!=(const PlaylistItemPtr &other)
    {
        return !(*this == other);
    }

    operator bool() const
    {
        return ptr;
    }

    vlc_playlist_item_t *raw() const
    {
        return ptr;
    }

    input_item_t *getMedia() const
    {
        return vlc_playlist_item_GetMedia(ptr);
    }

private:
    /* vlc_playlist_item_t is opaque, no need for * and -> */
    vlc_playlist_item_t *ptr = nullptr;
};

/* PlaylistItemPtr + with cached data */
class PlaylistItem
{
public:
    PlaylistItem(vlc_playlist_item_t *item = nullptr)
    {
        if (item)
        {
            d = new Data();
            d->item = item;
            sync();
        }
    }

    operator bool() const
    {
        return d;
    }

    vlc_playlist_item_t *raw() const {
        return d ? d->item.raw() : nullptr;
    }

    QString getTitle() const
    {
        return d->title;
    }

    void sync() {
        input_item_t *media = d->item.getMedia();
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

    QSharedDataPointer<Data> d;
};

/* PlaylistItem has the same size as a raw pointer */
static_assert(sizeof(PlaylistItem) == sizeof(void *));

  } // namespace playlist
} // namespace vlc

#endif
