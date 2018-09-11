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

template <typename T, typename H, typename R, H HOLD, R RELEASE>
class vlcptr {
    T *ptr;
public:
    vlcptr(T *ptr = nullptr) : ptr(ptr)
    {
        if (ptr)
            HOLD(ptr);
    }

    vlcptr(const vlcptr &other) : vlcptr(other.ptr) {}
    vlcptr(vlcptr &&other) noexcept : ptr(std::exchange(other.ptr, nullptr)) {}
    ~vlcptr()
    {
        if (ptr)
            RELEASE(ptr);
    }
    vlcptr &operator=(const vlcptr &other)
    {
        if (other.ptr)
            /* hold before in case ptr == other.ptr */
            HOLD(other.ptr);
        if (ptr)
            RELEASE(ptr);
        ptr = other.ptr;
        return *this;
    }
    vlcptr &operator=(vlcptr &&other) noexcept
    {
        ptr = std::exchange(other.ptr, nullptr);
        return *this;
    }
    bool operator==(const vlcptr &other) { return ptr == other.ptr; }
    bool operator!=(const vlcptr &other) { return !(*this == other); }
    operator bool() const { return ptr; }
    T &operator*() { return *ptr; }
    const T &operator*() const { return ptr; }
    T *operator->() { return ptr; }
    const T *operator->() const { return *ptr; }
    T *raw() { return ptr;}
    const T *raw() const { return ptr; }
};

namespace vlc {
  namespace playlist {

using PlaylistItemPtr = vlcptr<vlc_playlist_item_t,
                               decltype(&vlc_playlist_item_Hold),
                               decltype(&vlc_playlist_item_Release),
                               &vlc_playlist_item_Hold,
                               &vlc_playlist_item_Release>;

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

    vlc_playlist_item_t *raw() {
        return d ? d->item.raw() : nullptr;
    }

    const vlc_playlist_item_t *raw() const {
        return d ? d->item.raw() : nullptr;
    }

    QString getTitle() const
    {
        return d->title;
    }

    void sync() {
        input_item_t *media = vlc_playlist_item_GetMedia(d->item.raw());
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
