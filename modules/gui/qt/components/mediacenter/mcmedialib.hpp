/*****************************************************************************
 * mcmedialib.hpp: Medialibrary object
 ****************************************************************************
 * Copyright (C) 2006-2018 VideoLAN and AUTHORS
 *
 * Authors: Maël Kervella <dev@maelkervella.eu>
 *          Pierre Lamot <pierre@videolabs.io>
 *          Hugo Beauzée-Luyssen <hugo@beauzee.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#pragma once

#include <Qt>
#include <QAbstractListModel>
#include <QVariant>
#include <QHash>
#include <QByteArray>
#include <QList>
#include <QQuickWidget>
#include <QQuickItem>
#include <QMetaObject>
#include <QMetaMethod>
#include <QQmlEngine>

#include <memory>

#include "qt.hpp"
#include "mlqmltypes.hpp"

class MCMediaLib : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool gridView READ isGridView WRITE setGridView NOTIFY gridViewChanged)

public:
    MCMediaLib(intf_thread_t* _intf,
        QQuickWidget* _qml_item,
        QObject* _parent = nullptr
    );

    Q_INVOKABLE void addToPlaylist(const MLParentId &itemId);
    Q_INVOKABLE void addAndPlay(const MLParentId &itemId);

    vlc_medialibrary_t* vlcMl();

signals:
    void gridViewChanged();
    void discoveryStarted();
    void discoveryProgress( QString entryPoint );
    void discoveryCompleted();
    void progressUpdated( quint32 percent );

private:
    bool isGridView() const;
    void setGridView(bool);
    void invokeQML(const char *func );
    static void onMediaLibraryEvent( void* data, const vlc_ml_event_t* event );

private:
    intf_thread_t* m_intf;
    QQuickWidget *m_qmlItem;

    bool m_gridView;

    /* Medialibrary */
    vlc_medialibrary_t* m_ml;
    std::unique_ptr<vlc_ml_event_callback_t, std::function<void(vlc_ml_event_callback_t*)>> m_event_cb;

};
