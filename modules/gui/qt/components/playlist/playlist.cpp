/*****************************************************************************
 * playlist.cpp : Custom widgets for the playlist
 ****************************************************************************
 * Copyright © 2007-2010 the VideoLAN team
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "components/playlist/playlist.hpp"
#include "components/playlist_new/playlist_model.hpp"
#include <vlc_playlist_new.h>

#include "components/mediacenter/mcmedialib.hpp"

#include "components/mediacenter/mcmedialib.hpp"
#include "components/mediacenter/mlqmltypes.hpp"
#include "components/mediacenter/mlalbummodel.hpp"
#include "components/mediacenter/mlartistmodel.hpp"
#include "components/mediacenter/mlalbumtrackmodel.hpp"
#include "components/mediacenter/mlgenremodel.hpp"
#include "components/mediacenter/mlvideomodel.hpp"

#include "components/mediacenter/navigation_history.hpp"

#include "components/video_overlay.hpp"
#include "components/playlist_new/playlist_model.hpp"

#include "util/searchlineedit.hpp"

#include "input_manager.hpp"                      /* art signal */
#include "main_interface.hpp"                     /* DropEvent TODO remove this*/
#include "qml_main_context.hpp"

#include <QMenu>
#include <QSignalMapper>
#include <QSlider>
#include <QStackedWidget>
#include <QtQml/QQmlContext>

using  namespace vlc::playlist;

/**********************************************************************
 * Playlist Widget. The embedded playlist
 **********************************************************************/

PlaylistWidget::PlaylistWidget( intf_thread_t *_p_i, QWidget *_par )
               : QWidget( _par ), p_intf ( _p_i )
{

    setContentsMargins( 0, 3, 0, 3 );

    QVBoxLayout *mainLayout = new QVBoxLayout( this );

    /* Initiailisation of the MediaCenter view */


    /* Create a Container for the Art Label
       in order to have a beautiful resizing for the selector above it */
    if ( vlc_ml_instance_get( p_intf ) != nullptr )
    {
        mediacenterView = new QQuickWidget(this);
        QQmlContext *rootCtx = mediacenterView->rootContext();

        MCMediaLib *medialib = new MCMediaLib(_p_i, mediacenterView, mediacenterView);
        rootCtx->setContextProperty( "medialib", medialib );
        qRegisterMetaType<MLParentId>();
        qmlRegisterType<MLAlbumModel>( "org.videolan.medialib", 0, 1, "MLAlbumModel" );
        qmlRegisterType<MLArtistModel>( "org.videolan.medialib", 0, 1, "MLArtistModel" );
        qmlRegisterType<MLAlbumTrackModel>( "org.videolan.medialib", 0, 1, "MLAlbumTrackModel" );
        qmlRegisterType<MLGenreModel>( "org.videolan.medialib", 0, 1, "MLGenreModel" );
        qmlRegisterType<MLVideoModel>( "org.videolan.medialib", 0, 1, "MLVideoModel" );
        //expose base object, they aren't instanciable from QML side
        qmlRegisterType<MLAlbum>();
        qmlRegisterType<MLArtist>();
        qmlRegisterType<MLAlbumTrack>();
        qmlRegisterType<MLGenre>();
        qmlRegisterType<MLVideo>();


        qmlRegisterUncreatableType<NavigationHistory>("org.videolan.medialib", 0, 1, "History", "Type of global variable history" );
        NavigationHistory* navigation_history = new NavigationHistory(this);
        rootCtx->setContextProperty( "history", navigation_history );

        //auto mainplaylistptr = new PlaylistPtr(p_intf->p_sys->p_playlist, mediacenterView);
        //qRegisterMetaType<PlaylistPtr>();
        qRegisterMetaType<PlaylistPtr>();
        qmlRegisterType<PlaylistListModel>( "org.videolan.vlc", 0, 1, "PlaylistListModel" );
        qmlRegisterType<PlaylistControlerModel>( "org.videolan.vlc", 0, 1, "PlaylistControlerModel" );

        QmlMainContext* mainCtx = new QmlMainContext(_p_i, this);
        rootCtx->setContextProperty( "mainctx", mainCtx);

        mediacenterView->setSource( QUrl ( QStringLiteral("qrc:/qml/MainInterface.qml") ) );
        mediacenterView->setResizeMode( QQuickWidget::SizeRootObjectToView );
        mainLayout->addWidget( mediacenterView );
    }
    else
        mediacenterView = nullptr;

    setAcceptDrops( true );
    setWindowTitle( qtr( "Playlist" ) );
    setWindowRole( "vlc-playlist" );
    setWindowIcon( QApplication::windowIcon() );

    videoOverlay = new VideoOverlay(this);
}

PlaylistWidget::~PlaylistWidget()
{
    msg_Dbg( p_intf, "Playlist Destroyed" );
}

void PlaylistWidget::dropEvent( QDropEvent *event )
{
    if( p_intf->p_sys->p_mi )
        p_intf->p_sys->p_mi->dropEventPlay( event, false );
}
void PlaylistWidget::dragEnterEvent( QDragEnterEvent *event )
{
    event->acceptProposedAction();
}

void PlaylistWidget::closeEvent( QCloseEvent *event )
{
    if( THEDP->isDying() )
    {
        p_intf->p_sys->p_mi->playlistVisible = true;
        event->accept();
    }
    else
    {
        p_intf->p_sys->p_mi->playlistVisible = false;
        hide();
        event->ignore();
    }
}

void PlaylistWidget::forceHide()
{
    mediacenterView->hide();
    updateGeometry();
}

void PlaylistWidget::forceShow()
{
    mediacenterView->show();
    updateGeometry();
}
