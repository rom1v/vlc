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
#include "components/playlist/standardpanel.hpp"  /* MainView */
#include "components/playlist/selector.hpp"       /* PLSelector */
#include "components/playlist/playlist_model.hpp" /* PLModel */
#include "components/interface_widgets.hpp"       /* CoverArtLabel */
#include "components/playlist_new/playlist_model.hpp"
#include <vlc_playlist_new.h>

#include "components/mediacenter/mcmedialib.hpp"

#include "components/mediacenter/mcmedialib.hpp"
#include "components/mediacenter/mlqmltypes.hpp"
#include "components/mediacenter/mlalbummodel.hpp"
#include "components/mediacenter/mlartistmodel.hpp"
#include "components/mediacenter/mlalbumtrackmodel.hpp"
#include "components/mediacenter/mlgenremodel.hpp"

#include "components/video_overlay.hpp"

#include "util/searchlineedit.hpp"

#include "input_manager.hpp"                      /* art signal */
#include "main_interface.hpp"                     /* DropEvent TODO remove this*/

#include <QMenu>
#include <QSignalMapper>
#include <QSlider>
#include <QStackedWidget>
#include <QtQml/QQmlContext>

/**********************************************************************
 * Playlist Widget. The embedded playlist
 **********************************************************************/

PlaylistWidget::PlaylistWidget( intf_thread_t *_p_i, QWidget *_par )
               : QWidget( _par ), p_intf ( _p_i )
{

    setContentsMargins( 0, 3, 0, 3 );

    QVBoxLayout *mainLayout = new QVBoxLayout( this );

    /* Initiailisation of the MediaCenter view */

    mediacenterView = new QQuickWidget(this);

    /* Create a Container for the Art Label
       in order to have a beautiful resizing for the selector above it */
    QQmlContext *rootCtx = mediacenterView->rootContext();
    MCMediaLib *medialib = new MCMediaLib(_p_i, mediacenterView, mediacenterView);

    rootCtx->setContextProperty( "medialib", medialib );

    qRegisterMetaType<MLParentId>();
    qmlRegisterType<MLAlbumModel>( "org.videolan.medialib", 0, 1, "MLAlbumModel" );
    qmlRegisterType<MLArtistModel>( "org.videolan.medialib", 0, 1, "MLArtistModel" );
    qmlRegisterType<MLAlbumTrackModel>( "org.videolan.medialib", 0, 1, "MLAlbumTrackModel" );
    qmlRegisterType<MLGenreModel>( "org.videolan.medialib", 0, 1, "MLGenreModel" );
    //expose base object, they aren't instanciable from QML side
    qmlRegisterType<MLAlbum>();
    qmlRegisterType<MLArtist>();
    qmlRegisterType<MLAlbumTrack>();
    qmlRegisterType<MLGenre>();

    mediacenterView->setSource( QUrl ( QStringLiteral("qrc:/qml/MainInterface.qml") ) );
    mediacenterView->setResizeMode( QQuickWidget::SizeRootObjectToView );

    /*******************
     * Right           *
     *******************/

    QWidget* playlistWidget = new QWidget(this);

    QVBoxLayout *playlistLayout = new QVBoxLayout();
    playlistWidget->setLayout( playlistLayout );
    playlistLayout->setMargin( 0 ); playlistLayout->setSpacing( 0 );

    /* Initialisation of the playlist */
    playlist_t * p_playlist = THEPL;
    PL_LOCK;
    playlist_item_t *p_root = p_playlist->p_playing;
    PL_UNLOCK;

    setMinimumWidth( 400 );

    PLModel *model = PLModel::getPLModel( p_intf );

    mainView = new StandardPLPanel( this, p_intf, p_root, model );

    QHBoxLayout *topbarLayout = new QHBoxLayout();
    topbarLayout->setSpacing( 10 );
    playlistLayout->addLayout( topbarLayout );

    /* Location Bar */
    locationBar = new LocationBar( model );
    locationBar->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
    topbarLayout->addWidget( locationBar );
    CONNECT( locationBar, invoked( const QModelIndex & ),
             mainView, browseInto( const QModelIndex & ) );

    /* Button to switch views */
    QToolButton *viewButton = new QToolButton( this );
    viewButton->setIcon( style()->standardIcon( QStyle::SP_FileDialogDetailedView ) );
    viewButton->setToolTip( qtr("Change playlistview") );
    topbarLayout->addWidget( viewButton );

    viewButton->setMenu( StandardPLPanel::viewSelectionMenu( mainView ));
    CONNECT( viewButton, clicked(), mainView, cycleViews() );

    /* Search */
    searchEdit = new SearchLineEdit( this );
    searchEdit->setMaximumWidth( 250 );
    searchEdit->setMinimumWidth( 80 );
    searchEdit->setToolTip( qtr("Search the playlist") );
    topbarLayout->addWidget( searchEdit );
    CONNECT( searchEdit, textChanged( const QString& ),
             mainView, search( const QString& ) );
    CONNECT( searchEdit, searchDelayedChanged( const QString& ),
             mainView, searchDelayed( const QString & ) );

    CONNECT( mainView, viewChanged( const QModelIndex& ),
             this, changeView( const QModelIndex &) );

    mainView->setRootItem( p_root, false );
    playlistLayout->addWidget( mainView );

    /* */
    split = new QSplitter( this );

    /* Add the two sides of the QSplitter */
    split->addWidget( mediacenterView );
    split->addWidget( playlistWidget );

    QList<int> sizeList;
    sizeList << 420 << 180;
    split->setSizes( sizeList );
    split->setStretchFactor( 0, 0 );
    split->setStretchFactor( 1, 3 );
    split->setCollapsible( 1, false );

    mainLayout->addWidget( split );

    /* In case we want to keep the splitter information */
    // components shall never write there setting to a fixed location, may infer
    // with other uses of the same component...
    getSettings()->beginGroup("Playlist");
    split->restoreState( getSettings()->value("splitterSizes").toByteArray());
    getSettings()->endGroup();

    setAcceptDrops( true );
    setWindowTitle( qtr( "Playlist" ) );
    setWindowRole( "vlc-playlist" );
    setWindowIcon( QApplication::windowIcon() );

    videoOverlay = new VideoOverlay(this);
}

PlaylistWidget::~PlaylistWidget()
{
    getSettings()->beginGroup("Playlist");
    getSettings()->setValue( "splitterSizes", split->saveState() );
    getSettings()->endGroup();
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
    mainView->hide();
    updateGeometry();
}

void PlaylistWidget::forceShow()
{
    mainView->show();
    updateGeometry();
}

void PlaylistWidget::changeView( const QModelIndex& index )
{
    locationBar->setIndex( index );
}

void PlaylistWidget::setSearchFieldFocus()
{
    searchEdit->setFocus();
}

#include <QSignalMapper>
#include <QMenu>
#include <QPainter>
LocationBar::LocationBar( VLCModel *m )
{
    setModel( m );
    mapper = new QSignalMapper( this );
    CONNECT( mapper, mapped( int ), this, invoke( int ) );

    btnMore = new LocationButton( "...", false, true, this );
    menuMore = new QMenu( this );
    btnMore->setMenu( menuMore );
}

void LocationBar::setIndex( const QModelIndex &index )
{
    qDeleteAll( buttons );
    buttons.clear();
    qDeleteAll( actions );
    actions.clear();

    QModelIndex i = index;
    bool first = true;

    while( true )
    {
        QString text = model->getTitle( i );

        QAbstractButton *btn = new LocationButton( text, first, !first, this );
        btn->setSizePolicy( QSizePolicy::Maximum, QSizePolicy::Fixed );
        buttons.append( btn );

        QAction *action = new QAction( text, this );
        actions.append( action );
        CONNECT( btn, clicked(), action, trigger() );

        mapper->setMapping( action, model->itemId( i ) );
        CONNECT( action, triggered(), mapper, map() );

        first = false;

        if( i.isValid() ) i = i.parent();
        else break;
    }

    QString prefix;
    for( int a = actions.count() - 1; a >= 0 ; a-- )
    {
        actions[a]->setText( prefix + actions[a]->text() );
        prefix += QString("  ");
    }

    if( isVisible() ) layOut( size() );
}

void LocationBar::setRootIndex()
{
    setIndex( QModelIndex() );
}

void LocationBar::invoke( int i_id )
{
    QModelIndex index = model->indexByPLID( i_id, 0 );
    emit invoked ( index );
}

void LocationBar::layOut( const QSize& size )
{
    menuMore->clear();
    widths.clear();

    int count = buttons.count();
    int totalWidth = 0;
    for( int i = 0; i < count; i++ )
    {
        int w = buttons[i]->sizeHint().width();
        widths.append( w );
        totalWidth += w;
        if( totalWidth > size.width() ) break;
    }

    int x = 0;
    int shown = widths.count();

    if( totalWidth > size.width() && count > 1 )
    {
        QSize sz = btnMore->sizeHint();
        btnMore->setGeometry( 0, 0, sz.width(), size.height() );
        btnMore->show();
        x = sz.width();
        totalWidth += x;
    }
    else
    {
        btnMore->hide();
    }
    for( int i = count - 1; i >= 0; i-- )
    {
        if( totalWidth <= size.width() || i == 0)
        {
            buttons[i]->setGeometry( x, 0, qMin( size.width() - x, widths[i] ), size.height() );
            buttons[i]->show();
            x += widths[i];
            totalWidth -= widths[i];
        }
        else
        {
            menuMore->addAction( actions[i] );
            buttons[i]->hide();
            if( i < shown ) totalWidth -= widths[i];
        }
    }
}

void LocationBar::resizeEvent ( QResizeEvent * event )
{
    layOut( event->size() );
}

QSize LocationBar::sizeHint() const
{
    return btnMore->sizeHint();
}

LocationButton::LocationButton( const QString &text, bool bold,
                                bool arrow, QWidget * parent )
  : QPushButton( parent ), b_arrow( arrow )
{
    QFont font;
    font.setBold( bold );
    setFont( font );
    setText( text );
}

#define PADDING 4

void LocationButton::paintEvent ( QPaintEvent * )
{
    QStyleOptionButton option;
    option.initFrom( this );
    option.state |= QStyle::State_Enabled;
    QPainter p( this );

    if( underMouse() )
    {
        p.save();
        p.setRenderHint( QPainter::Antialiasing, true );
        QColor c = palette().color( QPalette::Highlight );
        p.setPen( c );
        p.setBrush( c.lighter( 150 ) );
        p.setOpacity( 0.2 );
        p.drawRoundedRect( option.rect.adjusted( 0, 2, 0, -2 ), 5, 5 );
        p.restore();
    }

    QRect r = option.rect.adjusted( PADDING, 0, -PADDING - (b_arrow ? 10 : 0), 0 );

    QString str( text() );
    /* This check is absurd, but either it is not done properly inside elidedText(),
       or boundingRect() is wrong */
    if( r.width() < fontMetrics().size(Qt::TextHideMnemonic, text()).width() )
        str = fontMetrics().elidedText( text(), Qt::ElideRight, r.width(), Qt::TextHideMnemonic  );
    p.drawText( r, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextHideMnemonic , str );

    if( b_arrow )
    {
        option.rect.setWidth( 10 );
        option.rect.moveRight( rect().right() );
        style()->drawPrimitive( QStyle::PE_IndicatorArrowRight, &option, &p );
    }
}

QSize LocationButton::sizeHint() const
{
    QSize s( fontMetrics().size( Qt::TextHideMnemonic, text() ) );
    /* Add two pixels to width: font metrics are buggy, if you pass text through elidation
       with exactly the width of its bounding rect, sometimes it still elides */
    s.setWidth( s.width() + ( 2 * PADDING ) + ( b_arrow ? 10 : 0 ) + 2 );
    s.setHeight( s.height() + 2 * PADDING );
    return s;
}

#undef PADDING
