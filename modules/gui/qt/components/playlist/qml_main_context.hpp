/*****************************************************************************
 * qml_main_context.hpp
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
#ifndef QML_MAIN_CONTEXT_HPP
#define QML_MAIN_CONTEXT_HPP

#include "qt.hpp"

#include <QObject>
#include <components/playlist_new/playlist_common.hpp>

/**
 * @brief The QmlMainContext class
 */
class QmlMainContext : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PlaylistPtr playlist READ getPlaylist CONSTANT)

public:
    explicit QmlMainContext(intf_thread_t *intf,  QObject *parent = nullptr);

    PlaylistPtr getPlaylist() const;

private:
    PlaylistPtr m_playlist;
};

#endif // QML_MAIN_CONTEXT_HPP
