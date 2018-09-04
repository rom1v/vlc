/*****************************************************************************
 * media_item.hpp
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

#ifndef VLC_QT_MEDIA_HPP_
#define VLC_QT_MEDIA_HPP_

#include <vlc_common.h>
#include <vlc_input_item.h>
#include <QString>

namespace vlc {
  namespace playlist {

class Media
{
public:
    Media(input_item_t *media = nullptr)
    {
        if (media)
        {
            /* the media must be unique in the playlist */
            ptr = input_item_Copy(media);
            if (!ptr)
                throw std::bad_alloc();
        }
    }

    Media(QString uri, QString name)
    {
        auto uUri = uri.toUtf8();
        auto uName = name.toUtf8();
        const char *rawUri = uUri.isNull() ? nullptr : uUri.constData();
        const char *rawName = uName.isNull() ? nullptr : uName.constData();
        ptr = input_item_New(rawUri, rawName);
        if (!ptr)
            throw std::bad_alloc();
    }

    Media(const Media &other)
        : Media(other.ptr) {}

    Media(Media &&other) noexcept
        : ptr(std::exchange(other.ptr, nullptr)) {}

    ~Media()
    {
        if (ptr)
            input_item_Release(ptr);
    }

    Media &operator=(const Media &other)
    {
        if (other.ptr)
            /* hold before in case ptr == other.ptr */
            input_item_Hold(other.ptr);
        if (ptr)
            input_item_Release(ptr);
        ptr = other.ptr;
        return *this;
    }

    Media &operator=(Media &&other) noexcept
    {
        ptr = std::exchange(other.ptr, nullptr);
        return *this;
    }

    bool operator==(const Media &other)
    {
        return ptr == other.ptr;
    }

    bool operator!=(const Media &other)
    {
        return !(*this == other);
    }

    input_item_t &operator*()
    {
        return *ptr;
    }

    const input_item_t &operator*() const
    {
        return *ptr;
    }

    input_item_t *operator->()
    {
        return ptr;
    }

    const input_item_t *operator->() const
    {
        return ptr;
    }

    operator bool() const
    {
        return ptr;
    }

    input_item_t *raw()
    {
        return ptr;
    }

    const input_item_t *raw() const
    {
        return ptr;
    }

private:
    input_item_t *ptr = nullptr;
};

  } // namespace playlist
} // namespace vlc

#endif
