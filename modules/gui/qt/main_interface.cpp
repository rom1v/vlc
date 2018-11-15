/*****************************************************************************
 * main_interface.cpp : Main interface
 ****************************************************************************
 * Copyright (C) 2006-2011 VideoLAN and AUTHORS
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Ilkka Ollakka <ileoo@videolan.org>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include "main_interface.hpp"
#include "input_manager.hpp"                    // Creation
#include "managers/renderer_manager.hpp"

#include "util/customwidgets.hpp"               // qtEventToVLCKey, QVLCStackedWidget
#include "util/qt_dirs.hpp"                     // toNativeSeparators
#include "util/imagehelper.hpp"

#include "components/interface_widgets.hpp"     // bgWidget, videoWidget
#include "components/controller.hpp"            // controllers
#include "components/playlist/playlist.hpp"     // plWidget
#include "dialogs/firstrun.hpp"                 // First Run
#include "dialogs/playlist.hpp"                 // PlaylistDialog


#include "components/playlist/playlist.hpp"
#include "components/playlist_new/playlist_model.hpp"
#include <vlc_playlist_new.h>

#include "components/mediacenter/mcmedialib.hpp"
#include "components/mediacenter/mlqmltypes.hpp"
#include "components/mediacenter/mlalbummodel.hpp"
#include "components/mediacenter/mlartistmodel.hpp"
#include "components/mediacenter/mlalbumtrackmodel.hpp"
#include "components/mediacenter/mlgenremodel.hpp"
#include "components/mediacenter/mlvideomodel.hpp"
#include "components/mediacenter/mlnetworkmodel.hpp"

#include "components/mediacenter/navigation_history.hpp"

#include "components/video_overlay.hpp"
#include "components/playlist_new/playlist_model.hpp"

#include "components/playlist/qml_main_context.hpp"



#include "menus.hpp"                            // Menu creation
#include "recents.hpp"                          // RecentItems when DnD

#include <QCloseEvent>
#include <QKeyEvent>

#include <QUrl>
#include <QSize>
#include <QDate>
#include <QMimeData>

#include <QWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QStackedWidget>
#include <QScreen>
#include <QStackedLayout>
#ifdef _WIN32
#include <QFileInfo>
#endif

#if ! HAS_QT510 && defined(QT5_HAS_X11)
# include <QX11Info>
# include <X11/Xlib.h>
#endif

#include <QTimer>
#include <QtQml/QQmlContext>


#include <vlc_actions.h>                    /* Wheel event */
#include <vlc_vout_window.h>                /* VOUT_ events */

using  namespace vlc::playlist;

// #define DEBUG_INTF

/* Callback prototypes */
static int PopupMenuCB( vlc_object_t *p_this, const char *psz_variable,
                        vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfShowCB( vlc_object_t *p_this, const char *psz_variable,
                       vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfBossCB( vlc_object_t *p_this, const char *psz_variable,
                       vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfRaiseMainCB( vlc_object_t *p_this, const char *psz_variable,
                           vlc_value_t old_val, vlc_value_t new_val,
                           void *param );

const QEvent::Type MainInterface::ToolbarsNeedRebuild =
        (QEvent::Type)QEvent::registerEventType();

MainInterface::MainInterface( intf_thread_t *_p_intf ) : QVLCMW( _p_intf )
{
    /* Variables initialisation */
    videoWidget          = NULL;
    stackCentralOldWidget= NULL;
    lastWinScreen        = NULL;
    sysTray              = NULL;
    cryptedLabel         = NULL;

    b_hideAfterCreation  = false; // --qt-start-minimized
    playlistVisible      = false;
    b_interfaceFullScreen= false;
    b_hasPausedWhenMinimized = false;
    i_kc_offset          = false;
    b_maximizedView      = false;
    b_isWindowTiled      = false;

    /* Ask for Privacy */
    FirstRun::CheckAndRun( this, p_intf );

    /**
     *  Configuration and settings
     *  Pre-building of interface
     **/
    /* Main settings */
    setFocusPolicy( Qt::StrongFocus );
    setAcceptDrops( true );
    setWindowRole( "vlc-main" );
    setWindowIcon( QApplication::windowIcon() );
    setWindowOpacity( var_InheritFloat( p_intf, "qt-opacity" ) );

    /* Does the interface resize to video size or the opposite */
    b_autoresize = var_InheritBool( p_intf, "qt-video-autoresize" );

    /* Are we in the enhanced always-video mode or not ? */
    b_minimalView = var_InheritBool( p_intf, "qt-minimal-view" );

    /* Do we want anoying popups or not */
    i_notificationSetting = var_InheritInteger( p_intf, "qt-notification" );

    /* */
    b_pauseOnMinimize = var_InheritBool( p_intf, "qt-pause-minimized" );

    /* Set the other interface settings */
    settings = getSettings();

    /* */
    b_plDocked = getSettings()->value( "MainWindow/pl-dock-status", true ).toBool();

    /* Should the UI stays on top of other windows */
    b_interfaceOnTop = var_InheritBool( p_intf, "video-on-top" );

#ifdef QT5_HAS_WAYLAND
    b_hasWayland = QGuiApplication::platformName()
        .startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
#endif

    /**************************
     *  UI and Widgets design
     **************************/
    setVLCWindowsTitle();

    createMainWidget( settings );

    /*********************************
     * Create the Systray Management *
     *********************************/
    initSystray();

    /*************************************************************
     * Connect the input manager to the GUI elements it manages  *
     * Beware initSystray did some connects on input manager too *
     *************************************************************/
    /**
     * Connects on nameChanged()
     * Those connects are different because options can impeach them to trigger.
     **/
    /* Main Interface statusbar */
    /* and title of the Main Interface*/
    if( var_InheritBool( p_intf, "qt-name-in-title" ) )
    {
        connect( THEMIM, &InputManager::nameChanged, this, &MainInterface::setVLCWindowsTitle );
    }
    connect( THEMIM, &InputManager::inputChanged, this, &MainInterface::onInputChanged );

    /* END CONNECTS ON IM */

    /* VideoWidget connects for asynchronous calls */
    b_videoFullScreen = false;
    connect( this, SIGNAL(askGetVideo(struct vout_window_t*, unsigned, unsigned, bool, bool*)),
             this, SLOT(getVideoSlot(struct vout_window_t*, unsigned, unsigned, bool, bool*)),
             Qt::BlockingQueuedConnection );
    connect( this, SIGNAL(askReleaseVideo( void )),
             this, SLOT(releaseVideoSlot( void )),
             Qt::BlockingQueuedConnection );
    connect( this, &MainInterface::askVideoOnTop, this, &MainInterface::setVideoOnTop );

    if( videoWidget )
    {
        if( b_autoresize )
        {
            connect( videoWidget, &VideoWidget::sizeChanged, this, &MainInterface::videoSizeChanged );
        }
        connect( this, &MainInterface::askVideoToResize, this, &MainInterface::setVideoSize );

    }

    connect( THEDP, &DialogsProvider::toolBarConfUpdated, this, &MainInterface::toolBarConfUpdated );
    installEventFilter( this );

    connect( this, &MainInterface::askToQuit, THEDP, &DialogsProvider::quit );

    connect( this, &MainInterface::askBoss, this, &MainInterface::setBoss );
    connect( this, &MainInterface::askRaise, this, &MainInterface::setRaise );


    connect( THEDP, &DialogsProvider::releaseMouseEvents, this, &MainInterface::voutReleaseMouseEvents ) ;
    /** END of CONNECTS**/


    /************
     * Callbacks
     ************/
    var_AddCallback( pl_Get(p_intf), "intf-toggle-fscontrol", IntfShowCB, p_intf );
    var_AddCallback( pl_Get(p_intf), "intf-boss", IntfBossCB, p_intf );
    var_AddCallback( pl_Get(p_intf), "intf-show", IntfRaiseMainCB, p_intf );

    /* Register callback for the intf-popupmenu variable */
    var_AddCallback( pl_Get(p_intf), "intf-popupmenu", PopupMenuCB, p_intf );


    QVLCTools::restoreWidgetPosition( settings, this, QSize(600, 420) );

    b_interfaceFullScreen = isFullScreen();

    setVisible( !b_hideAfterCreation );

    computeMinimumSize();
}

MainInterface::~MainInterface()
{
    if( videoWidget )
        releaseVideoSlot();

    RendererManager::killInstance();

    /* Save states */

    settings->beginGroup("MainWindow");
    settings->setValue( "pl-dock-status", b_plDocked );

    /* Save playlist state */
    settings->setValue( "playlist-visible", playlistVisible );

    settings->setValue( "status-bar-visible", b_statusbarVisible );

    /* Save the stackCentralW sizes */
    settings->endGroup();

    /* Save this size */
    QVLCTools::saveWidgetPosition(settings, this);

    /* Unregister callbacks */
    var_DelCallback( pl_Get(p_intf), "intf-boss", IntfBossCB, p_intf );
    var_DelCallback( pl_Get(p_intf), "intf-show", IntfRaiseMainCB, p_intf );
    var_DelCallback( pl_Get(p_intf), "intf-toggle-fscontrol", IntfShowCB, p_intf );
    var_DelCallback( pl_Get(p_intf), "intf-popupmenu", PopupMenuCB, p_intf );

    p_intf->p_sys->p_mi = NULL;
}

void MainInterface::computeMinimumSize()
{
    int minWidth = 80;
    setMinimumWidth( minWidth );
}

/*****************************
 *   Main UI handling        *
 *****************************/

void MainInterface::reloadPrefs()
{
    i_notificationSetting = var_InheritInteger( p_intf, "qt-notification" );
    b_pauseOnMinimize = var_InheritBool( p_intf, "qt-pause-minimized" );
}


void MainInterface::onInputChanged( bool hasInput )
{
    if( hasInput == false )
        return;
    int autoRaise = var_InheritInteger( p_intf, "qt-auto-raise" );
    if ( autoRaise == MainInterface::RAISE_NEVER )
        return;
    if( THEMIM->hasVideoOutput() == true )
    {
        if( ( autoRaise & MainInterface::RAISE_VIDEO ) == 0 )
            return;
    }
    else if ( ( autoRaise & MainInterface::RAISE_AUDIO ) == 0 )
        return;
    emit askRaise();
}

void MainInterface::createMainWidget( QSettings *creationSettings )
{
    QWidget* mainWidget = new QWidget(this);
    QStackedLayout *stackedLayout = new QStackedLayout;
    stackedLayout->setStackingMode(QStackedLayout::StackAll);
    mainWidget->setLayout(stackedLayout);
    setCentralWidget( mainWidget );

    /* Create the main Widget and the mainLayout */
    videoWidget = new VideoWidget( p_intf, mainWidget );

    mediacenterView = new QQuickWidget( mainWidget);
    QQmlContext *rootCtx = mediacenterView->rootContext();

    MCMediaLib *medialib = new MCMediaLib(p_intf, mediacenterView, mediacenterView);
    rootCtx->setContextProperty( "medialib", medialib );
    qRegisterMetaType<MLParentId>();
    qmlRegisterType<MLAlbumModel>( "org.videolan.medialib", 0, 1, "MLAlbumModel" );
    qmlRegisterType<MLArtistModel>( "org.videolan.medialib", 0, 1, "MLArtistModel" );
    qmlRegisterType<MLAlbumTrackModel>( "org.videolan.medialib", 0, 1, "MLAlbumTrackModel" );
    qmlRegisterType<MLGenreModel>( "org.videolan.medialib", 0, 1, "MLGenreModel" );
    qmlRegisterType<MLVideoModel>( "org.videolan.medialib", 0, 1, "MLVideoModel" );
    qmlRegisterUncreatableType<MLNetworkModel>( "org.videolan.medialib", 0, 1,
        "MLNetworkModel", "Use the model factory to create this type" );
    rootCtx->setContextProperty( "networkModelFactory", new MLNetworkModelFactory(this) );
    //expose base object, they aren't instanciable from QML side
    qmlRegisterType<MLAlbum>();
    qmlRegisterType<MLArtist>();
    qmlRegisterType<MLAlbumTrack>();
    qmlRegisterType<MLGenre>();
    qmlRegisterType<MLVideo>();

    qmlRegisterUncreatableType<NavigationHistory>("org.videolan.medialib", 0, 1, "History", "Type of global variable history" );
    NavigationHistory* navigation_history = new NavigationHistory(this);
    rootCtx->setContextProperty( "history", navigation_history );


    qmlRegisterUncreatableType<TrackListModel>("org.videolan.vlc", 0, 1, "TrackListModel", "FIXME doc" );
    qmlRegisterUncreatableType<TitleListModel>("org.videolan.vlc", 0, 1, "TitleListModel", "FIXME doc" );
    qmlRegisterUncreatableType<ChapterListModel>("org.videolan.vlc", 0, 1, "ChapterListModel", "FIXME doc" );
    qmlRegisterUncreatableType<ProgramListModel>("org.videolan.vlc", 0, 1, "ProgramListModel", "FIXME doc" );
    qmlRegisterUncreatableType<VLCVarChoiceModel>("org.videolan.vlc", 0, 1, "VLCVarChoiceModel", "FIXME doc" );
    qmlRegisterUncreatableType<InputManager>("org.videolan.vlc", 0, 1, "PlayerControler", "FIXME doc" );

    rootCtx->setContextProperty( "player", p_intf->p_sys->p_mainPlayerControler );

    qRegisterMetaType<PlaylistPtr>();
    qmlRegisterUncreatableType<PlaylistItem>("org.videolan.vlc", 0, 1, "PlaylistItem", "");
    qmlRegisterType<PlaylistListModel>( "org.videolan.vlc", 0, 1, "PlaylistListModel" );
    qmlRegisterType<PlaylistControlerModel>( "org.videolan.vlc", 0, 1, "PlaylistControlerModel" );

    QmlMainContext* mainCtx = new QmlMainContext(p_intf, this);
    rootCtx->setContextProperty( "mainctx", mainCtx);

    mediacenterView->setSource( QUrl ( QStringLiteral("qrc:/qml/MainInterface.qml") ) );
    mediacenterView->setResizeMode( QQuickWidget::SizeRootObjectToView );

    mediacenterView->setClearColor(Qt::transparent);
    mediacenterView->setAttribute(Qt::WA_AlwaysStackOnTop);

    QWidget      *front_wrapper = new QWidget;
    QHBoxLayout  *front_wrapper_layout = new QHBoxLayout(front_wrapper);
    front_wrapper_layout->addWidget(mediacenterView);

    front_wrapper->setAttribute(Qt::WA_NativeWindow);
    front_wrapper->setAttribute(Qt::WA_DontCreateNativeAncestors);

    stackedLayout->addWidget(front_wrapper);
    stackedLayout->addWidget(videoWidget);

    //QVBoxLayout *layout = new QVBoxLayout();
    //layout->addWidget(mediacenterView);
    //videoWidget->layout()->addWidget(mediacenterView);

    //setCentralWidget( mediacenterView );

    /* Enable the popup menu in the MI */
    if ( b_interfaceOnTop )
        setWindowFlags( windowFlags() | Qt::WindowStaysOnTopHint );
}

inline void MainInterface::initSystray()
{
    bool b_systrayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    bool b_systrayWanted = var_InheritBool( p_intf, "qt-system-tray" );

    if( var_InheritBool( p_intf, "qt-start-minimized") )
    {
        if( b_systrayAvailable )
        {
            b_systrayWanted = true;
            b_hideAfterCreation = true;
        }
        else
            msg_Err( p_intf, "cannot start minimized without system tray bar" );
    }

    if( b_systrayAvailable && b_systrayWanted )
        createSystray();
}

/**********************************************************************
 * Handling of sizing of the components
 **********************************************************************/

void MainInterface::debug()
{
}


/****************************************************************************
 * Video Handling
 ****************************************************************************/

/**
 * NOTE:
 * You must not change the state of this object or other Qt UI objects,
 * from the video output thread - only from the Qt UI main loop thread.
 * All window provider queries must be handled through signals or events.
 * That's why we have all those emit statements...
 */
bool MainInterface::getVideo( struct vout_window_t *p_wnd,
                              unsigned int i_width, unsigned int i_height,
                              bool fullscreen )
{
    bool result;

    /* This is a blocking call signal. Results are stored directly in the
     * vout_window_t and boolean pointers. Beware of deadlocks! */
    emit askGetVideo( p_wnd, i_width, i_height, fullscreen, &result );
    return result;
}

void MainInterface::getVideoSlot( struct vout_window_t *p_wnd,
                                  unsigned i_width, unsigned i_height,
                                  bool fullscreen, bool *res )
{
    /* Hidden or minimized, activate */
    if( isHidden() || isMinimized() )
        toggleUpdateSystrayMenu();

    /* Request the videoWidget */
    if ( !videoWidget )
    {
        videoWidget = new VideoWidget( p_intf, this );
    }
    *res = videoWidget->request( p_wnd );
    if( *res ) /* The videoWidget is available */
    {
        /* Ask videoWidget to resize correctly, if we are in normal mode */
        if( b_autoresize ) {
#if HAS_QT56
            qreal factor = videoWidget->devicePixelRatioF();

            i_width = qRound( (qreal) i_width / factor );
            i_height = qRound( (qreal) i_height / factor );
#endif

            videoWidget->setSize( i_width, i_height );
        }
    }
}

/* Asynchronous call from the WindowClose function */
void MainInterface::releaseVideo( void )
{
    emit askReleaseVideo();
}

/* Function that is CONNECTED to the previous emit */
void MainInterface::releaseVideoSlot( void )
{
    /* This function is called when the embedded video window is destroyed,
     * or in the rare case that the embedded window is still here but the
     * Qt interface exits. */
    assert( videoWidget );
    videoWidget->release();
    setVideoOnTop( false );
}

// The provided size is in physical pixels, coming from the core.
void MainInterface::setVideoSize( unsigned int w, unsigned int h )
{
    videoWidget->setSize( videoWidget->width(), videoWidget->height() );
}

void MainInterface::videoSizeChanged( int w, int h )
{
    //FIXME
}

/* Slot to change the video always-on-top flag.
 * Emit askVideoOnTop() to invoke this from other thread. */
void MainInterface::setVideoOnTop( bool on_top )
{
    //don't apply changes if user has already sets its interface on top
    if ( b_interfaceOnTop )
        return;

    Qt::WindowFlags oldflags = windowFlags(), newflags;

    if( on_top )
        newflags = oldflags | Qt::WindowStaysOnTopHint;
    else
        newflags = oldflags & ~Qt::WindowStaysOnTopHint;
    if( newflags != oldflags && !b_videoFullScreen )
    {
        setWindowFlags( newflags );
        show(); /* necessary to apply window flags */
    }
}

void MainInterface::setInterfaceAlwaysOnTop( bool on_top )
{
    b_interfaceOnTop = on_top;
    Qt::WindowFlags oldflags = windowFlags(), newflags;

    if( on_top )
        newflags = oldflags | Qt::WindowStaysOnTopHint;
    else
        newflags = oldflags & ~Qt::WindowStaysOnTopHint;
    if( newflags != oldflags && !b_videoFullScreen )
    {
        setWindowFlags( newflags );
        show(); /* necessary to apply window flags */
    }
}

/* Asynchronous call from WindowControl function */
int MainInterface::controlVideo( int i_query, va_list args )
{
    switch( i_query )
    {
    case VOUT_WINDOW_SET_SIZE:
    {
        unsigned int i_width  = va_arg( args, unsigned int );
        unsigned int i_height = va_arg( args, unsigned int );

        emit askVideoToResize( i_width, i_height );
        return VLC_SUCCESS;
    }
    case VOUT_WINDOW_SET_STATE:
    {
        unsigned i_arg = va_arg( args, unsigned );
        unsigned on_top = i_arg & VOUT_WINDOW_STATE_ABOVE;

        emit askVideoOnTop( on_top != 0 );
        return VLC_SUCCESS;
    }
    case VOUT_WINDOW_SET_FULLSCREEN:
        emit askVideoSetFullScreen( true );
        return VLC_SUCCESS;
    case VOUT_WINDOW_UNSET_FULLSCREEN:
        emit askVideoSetFullScreen( false );
        return VLC_SUCCESS;
    default:
        msg_Warn( p_intf, "unsupported control query" );
        return VLC_EGENERIC;
    }
}

const Qt::Key MainInterface::kc[10] =
{
    Qt::Key_Up, Qt::Key_Up,
    Qt::Key_Down, Qt::Key_Down,
    Qt::Key_Left, Qt::Key_Right, Qt::Key_Left, Qt::Key_Right,
    Qt::Key_B, Qt::Key_A
};

/**
 * Give the decorations of the Main Window a correct Name.
 * If nothing is given, set it to VLC...
 **/
void MainInterface::setVLCWindowsTitle( const QString& aTitle )
{
    if( aTitle.isEmpty() )
    {
        setWindowTitle( qtr( "VLC media player" ) );
    }
    else
    {
        setWindowTitle( aTitle + " - " + qtr( "VLC media player" ) );
    }
}

void MainInterface::showBuffering( float f_cache )
{
    QString amount = QString("Buffering: %1%").arg( (int)(100*f_cache) );
    statusBar()->showMessage( amount, 1000 );
}

/*****************************************************************************
 * Systray Icon and Systray Menu
 *****************************************************************************/
/**
 * Create a SystemTray icon and a menu that would go with it.
 * Connects to a click handler on the icon.
 **/
void MainInterface::createSystray()
{
    QIcon iconVLC;
    if( QDate::currentDate().dayOfYear() >= QT_XMAS_JOKE_DAY && var_InheritBool( p_intf, "qt-icon-change" ) )
        iconVLC = QIcon::fromTheme( "vlc-xmas", QIcon( ":/logo/vlc128-xmas.png" ) );
    else
        iconVLC = QIcon::fromTheme( "vlc", QIcon( ":/logo/vlc256.png" ) );
    sysTray = new QSystemTrayIcon( iconVLC, this );
    sysTray->setToolTip( qtr( "VLC media player" ));

    systrayMenu = new QMenu( qtr( "VLC media player" ), this );
    systrayMenu->setIcon( iconVLC );

    VLCMenuBar::updateSystrayMenu( this, p_intf, true );
    sysTray->show();

    connect( sysTray, &QSystemTrayIcon::activated,
             this, &MainInterface::handleSystrayClick );

    /* Connects on nameChanged() */
    connect( THEMIM, &InputManager::nameChanged,
             this, &MainInterface::updateSystrayTooltipName );
    /* Connect PLAY_STATUS on the systray */
    connect( THEMIM, &InputManager::playingStateChanged,
             this, &MainInterface::updateSystrayTooltipStatus );
}

void MainInterface::toggleUpdateSystrayMenuWhenVisible()
{
    hide();
}

void MainInterface::resizeWindow(int w, int h)
{
#if ! HAS_QT510 && defined(QT5_HAS_X11)
    if( QX11Info::isPlatformX11() )
    {
#if HAS_QT56
        qreal dpr = devicePixelRatioF();
#else
        qreal dpr = devicePixelRatio();
#endif
        QSize size(w, h);
        size = size.boundedTo(maximumSize()).expandedTo(minimumSize());
        /* X11 window managers are not required to accept geometry changes on
         * the top-level window.  Unfortunately, Qt < 5.10 assumes that the
         * change will succeed, and resizes all sub-windows unconditionally.
         * By calling XMoveResizeWindow directly, Qt will not see our change
         * request until the ConfigureNotify event on success
         * and not at all if it is rejected. */
        XResizeWindow( QX11Info::display(), winId(),
                       (unsigned int)size.width() * dpr, (unsigned int)size.height() * dpr);
        return;
    }
#endif
    resize(w, h);
}

/**
 * Updates the Systray Icon's menu and toggle the main interface
 */
void MainInterface::toggleUpdateSystrayMenu()
{
    /* If hidden, show it */
    if( isHidden() )
    {
        show();
        activateWindow();
    }
    else if( isMinimized() )
    {
        /* Minimized */
        showNormal();
        activateWindow();
    }
    else
    {
        /* Visible (possibly under other windows) */
        toggleUpdateSystrayMenuWhenVisible();
    }
    if( sysTray )
        VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* First Item of the systray menu */
void MainInterface::showUpdateSystrayMenu()
{
    if( isHidden() )
        show();
    if( isMinimized() )
        showNormal();
    activateWindow();

    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* First Item of the systray menu */
void MainInterface::hideUpdateSystrayMenu()
{
    hide();
    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* Click on systray Icon */
void MainInterface::handleSystrayClick(
                                    QSystemTrayIcon::ActivationReason reason )
{
    switch( reason )
    {
        case QSystemTrayIcon::Trigger:
        case QSystemTrayIcon::DoubleClick:
#ifdef Q_OS_MAC
            VLCMenuBar::updateSystrayMenu( this, p_intf );
#else
            toggleUpdateSystrayMenu();
#endif
            break;
        case QSystemTrayIcon::MiddleClick:
            sysTray->showMessage( qtr( "VLC media player" ),
                    qtr( "Control menu for the player" ),
                    QSystemTrayIcon::Information, 3000 );
            break;
        default:
            break;
    }
}

/**
 * Updates the name of the systray Icon tooltip.
 * Doesn't check if the systray exists, check before you call it.
 **/
void MainInterface::updateSystrayTooltipName( const QString& name )
{
    if( name.isEmpty() )
    {
        sysTray->setToolTip( qtr( "VLC media player" ) );
    }
    else
    {
        sysTray->setToolTip( name );
        if( ( i_notificationSetting == NOTIFICATION_ALWAYS ) ||
            ( i_notificationSetting == NOTIFICATION_MINIMIZED && (isMinimized() || isHidden()) ) )
        {
            sysTray->showMessage( qtr( "VLC media player" ), name,
                    QSystemTrayIcon::NoIcon, 3000 );
        }
    }

    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/**
 * Updates the status of the systray Icon tooltip.
 * Doesn't check if the systray exists, check before you call it.
 **/
void MainInterface::updateSystrayTooltipStatus( InputManager::PlayingState i_status )
{
    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

void MainInterface::changeEvent(QEvent *event)
{
    if( event->type() == QEvent::WindowStateChange )
    {
        QWindowStateChangeEvent *windowStateChangeEvent = static_cast<QWindowStateChangeEvent*>(event);
        Qt::WindowStates newState = windowState();
        Qt::WindowStates oldState = windowStateChangeEvent->oldState();

        /* b_maximizedView stores if the window was maximized before entering fullscreen.
         * It is set when entering maximized mode, unset when leaving it to normal mode.
         * Upon leaving full screen, if b_maximizedView is set,
         * the window should be maximized again. */
        if( newState & Qt::WindowMaximized &&
            !( oldState & Qt::WindowMaximized ) )
            b_maximizedView = true;

        if( !( newState & Qt::WindowMaximized ) &&
            oldState & Qt::WindowMaximized &&
            !b_videoFullScreen )
            b_maximizedView = false;

        if( !( newState & Qt::WindowFullScreen ) &&
            oldState & Qt::WindowFullScreen &&
            b_maximizedView )
        {
            showMaximized();
            return;
        }

        if( newState & Qt::WindowMinimized )
        {
            b_hasPausedWhenMinimized = false;

            if( THEMIM->getPlayingState() == InputManager::PLAYING_STATE_PLAYING &&
                THEMIM->hasVideoOutput() && !THEMIM->hasAudioVisualization() &&
                b_pauseOnMinimize )
            {
                b_hasPausedWhenMinimized = true;
                THEMPL->pause();
            }
        }
        else if( oldState & Qt::WindowMinimized && !( newState & Qt::WindowMinimized ) )
        {
            if( b_hasPausedWhenMinimized )
            {
                THEMPL->play();
            }
        }
    }

    QWidget::changeEvent(event);
}

/************************************************************************
 * D&D Events
 ************************************************************************/
void MainInterface::dropEvent(QDropEvent *event)
{
    dropEventPlay( event, true );
}

/**
 * dropEventPlay
 *
 * Event called if something is dropped onto a VLC window
 * \param event the event in question
 * \param b_play whether to play the file immediately
 * \return nothing
 */
void MainInterface::dropEventPlay( QDropEvent *event, bool b_play )
{
    if( event->possibleActions() & ( Qt::CopyAction | Qt::MoveAction | Qt::LinkAction ) )
       event->setDropAction( Qt::CopyAction );
    else
        return;

    const QMimeData *mimeData = event->mimeData();

    /* D&D of a subtitles file, add it on the fly */
    if( mimeData->urls().count() == 1 && THEMIM->hasInput() )
    {
        if( !THEMIM->AddAssociatedMedia(SPU_ES, mimeData->urls()[0].toString(), true, true, true) )
        {
            event->accept();
            return;
        }
    }

    bool first = b_play;
    foreach( const QUrl &url, mimeData->urls() )
    {
        if( url.isValid() )
        {
            QString mrl = toURI( url.toEncoded().constData() );
#ifdef _WIN32
            QFileInfo info( url.toLocalFile() );
            if( info.exists() && info.isSymLink() )
            {
                QString target = info.symLinkTarget();
                QUrl url;
                if( QFile::exists( target ) )
                {
                    url = QUrl::fromLocalFile( target );
                }
                else
                {
                    url.setUrl( target );
                }
                mrl = toURI( url.toEncoded().constData() );
            }
#endif
            if( mrl.length() > 0 )
            {
                Open::openMRL( p_intf, mrl, first );
                first = false;
            }
        }
    }

    /* Browsers give content as text if you dnd the addressbar,
       so check if mimedata has valid url in text and use it
       if we didn't get any normal Urls()*/
    if( !mimeData->hasUrls() && mimeData->hasText() &&
        QUrl(mimeData->text()).isValid() )
    {
        QString mrl = toURI( mimeData->text() );
        Open::openMRL( p_intf, mrl, first );
    }
    event->accept();
}
void MainInterface::dragEnterEvent(QDragEnterEvent *event)
{
     event->acceptProposedAction();
}
void MainInterface::dragMoveEvent(QDragMoveEvent *event)
{
     event->acceptProposedAction();
}
void MainInterface::dragLeaveEvent(QDragLeaveEvent *event)
{
     event->accept();
}

/************************************************************************
 * Events stuff
 ************************************************************************/
void MainInterface::keyPressEvent( QKeyEvent *e )
{
    handleKeyPress( e );

    /* easter eggs sequence handling */
    if ( e->key() == kc[ i_kc_offset ] )
        i_kc_offset++;
    else
        i_kc_offset = 0;

    if ( i_kc_offset == (sizeof( kc ) / sizeof( Qt::Key )) )
    {
        i_kc_offset = 0;
        emit kc_pressed();
    }
}

void MainInterface::handleKeyPress( QKeyEvent *e )
{
    int i_vlck = qtEventToVLCKey( e );
    if( i_vlck > 0 )
    {
        var_SetInteger( p_intf->obj.libvlc, "key-pressed", i_vlck );
        e->accept();
    }
    else
        e->ignore();
}

void MainInterface::wheelEvent( QWheelEvent *e )
{
    int i_vlckey = qtWheelEventToVLCKey( e );
    var_SetInteger( p_intf->obj.libvlc, "key-pressed", i_vlckey );
    e->accept();
}

void MainInterface::closeEvent( QCloseEvent *e )
{
//  hide();
    emit askToQuit(); /* ask THEDP to quit, so we have a unique method */
    /* Accept session quit. Otherwise we break the desktop mamager. */
    e->accept();
}

bool MainInterface::eventFilter( QObject *obj, QEvent *event )
{
     //if ( event->type() == QEvent::Resize ) {
        return QObject::eventFilter( obj, event );
    //}
}

void MainInterface::toolBarConfUpdated()
{
    QApplication::postEvent( this, new QEvent( MainInterface::ToolbarsNeedRebuild ) );
}

void MainInterface::setInterfaceFullScreen( bool fs )
{
    if( fs )
        setWindowState( windowState() | Qt::WindowFullScreen );
    else
        setWindowState( windowState() & ~Qt::WindowFullScreen );
}
void MainInterface::toggleInterfaceFullScreen()
{
    b_interfaceFullScreen = !b_interfaceFullScreen;
    if( !b_videoFullScreen )
        setInterfaceFullScreen( b_interfaceFullScreen );
    emit fullscreenInterfaceToggled( b_interfaceFullScreen );
}

void MainInterface::emitBoss()
{
    emit askBoss();
}
void MainInterface::setBoss()
{
    THEMPL->pause();
    if( sysTray )
    {
        hide();
    }
    else
    {
        showMinimized();
    }
}

void MainInterface::emitRaise()
{
    emit askRaise();
}
void MainInterface::setRaise()
{
    activateWindow();
    raise();
}

void MainInterface::voutReleaseMouseEvents()
{
    if (videoWidget)
    {
        QPoint pos = QCursor::pos();
        QPoint localpos = videoWidget->mapFromGlobal(pos);
        int buttons = QApplication::mouseButtons();
        int i_button = 1;
        while (buttons != 0)
        {
            if ( (buttons & 1) != 0 )
            {
                QMouseEvent new_e( QEvent::MouseButtonRelease, localpos,
                                   (Qt::MouseButton)i_button, (Qt::MouseButton)i_button, Qt::NoModifier );
                QApplication::sendEvent(videoWidget, &new_e);
            }
            buttons >>= 1;
            i_button <<= 1;
        }

    }
}

/*****************************************************************************
 * PopupMenuCB: callback triggered by the intf-popupmenu playlist variable.
 *  We don't show the menu directly here because we don't want the
 *  caller to block for a too long time.
 *****************************************************************************/
static int PopupMenuCB( vlc_object_t *, const char *,
                        vlc_value_t, vlc_value_t new_val, void *param )
{
    intf_thread_t *p_intf = (intf_thread_t *)param;

    if( p_intf->pf_show_dialog )
    {
        p_intf->pf_show_dialog( p_intf, INTF_DIALOG_POPUPMENU,
                                new_val.b_bool, NULL );
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfShowCB: callback triggered by the intf-toggle-fscontrol libvlc variable.
 *****************************************************************************/
static int IntfShowCB( vlc_object_t *, const char *,
                       vlc_value_t, vlc_value_t, void *param )
{
    /* Show event */
     return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfRaiseMainCB: callback triggered by the intf-show-main libvlc variable.
 *****************************************************************************/
static int IntfRaiseMainCB( vlc_object_t *, const char *,
                            vlc_value_t, vlc_value_t, void *param )
{
    intf_thread_t *p_intf = (intf_thread_t *)param;
    p_intf->p_sys->p_mi->emitRaise();

    return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfBossCB: callback triggered by the intf-boss libvlc variable.
 *****************************************************************************/
static int IntfBossCB( vlc_object_t *, const char *,
                       vlc_value_t, vlc_value_t, void *param )
{
    intf_thread_t *p_intf = (intf_thread_t *)param;
    p_intf->p_sys->p_mi->emitBoss();

    return VLC_SUCCESS;
}
