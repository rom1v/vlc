/*****************************************************************************
 * playlist.hpp : Playlist Widgets
 ****************************************************************************
 * Copyright (C) 2006-2009 the VideoLAN team
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Rafaël Carré <funman@videolanorg>
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

#ifndef VLC_QT_PLAYLIST_HPP_
#define VLC_QT_PLAYLIST_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

//#include <vlc_playlist_legacy.h>

#include <QtWidgets/QStackedWidget>

#include <QSplitter>

#include <QPushButton>
#include <QSplitterHandle>
#include <QMouseEvent>

class VideoOverlay;
class QQuickWidget;

class PlaylistWidget : public QWidget
{
    Q_OBJECT
public:
    virtual ~PlaylistWidget();

    void forceHide();
    void forceShow();

    QQuickWidget         *mediacenterView;
    VideoOverlay         *videoOverlay;

    intf_thread_t *p_intf;

protected:
    PlaylistWidget( intf_thread_t *_p_i, QWidget * );
    void dropEvent( QDropEvent *) Q_DECL_OVERRIDE;
    void dragEnterEvent( QDragEnterEvent * ) Q_DECL_OVERRIDE;
    void closeEvent( QCloseEvent * ) Q_DECL_OVERRIDE;

    friend class PlaylistDialog;
};

#endif
