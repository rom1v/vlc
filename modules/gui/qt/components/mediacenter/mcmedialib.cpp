/*****************************************************************************
 * mcmedialib.cpp: Medialibrary object
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

#include "mcmedialib.hpp"
#include "mlhelper.hpp"
#include "recents.hpp"

#include <vlc_playlist.h>
#include <vlc_input_item.h>

MCMediaLib::MCMediaLib(intf_thread_t *_intf,
        QQuickWidget *_qml_item,
        QObject *_parent)
    : QObject( _parent )
    , m_intf( _intf )
    , m_qmlItem( _qml_item )
    , m_gridView( true )
    , m_ml( vlcMl() )
    , m_event_cb( nullptr, [this](vlc_ml_event_callback_t* cb ) {
        vlc_ml_event_unregister_callback( m_ml, cb );
      })
{
    m_event_cb.reset( vlc_ml_event_register_callback( m_ml, MCMediaLib::onMediaLibraryEvent,
                                                      this ) );
}

// Should the items be displayed as a grid or as list ?
bool MCMediaLib::isGridView() const
{
    return m_gridView;
}

void MCMediaLib::setGridView(bool state)
{
    m_gridView = state;
    emit gridViewChanged();
}


void MCMediaLib::openMRLFromMedia(const vlc_ml_media_t& media, bool start )
{
    if (!media.p_files)
        return;
    for ( const vlc_ml_file_t& mediafile: ml_range_iterate<vlc_ml_file_t>(media.p_files) )
    {
        if (mediafile.psz_mrl)
            Open::openMRL(m_intf, mediafile.psz_mrl, start);
        start = false;
    }
}

void MCMediaLib::addToPlaylist(const QString& mrl)
{
    Open::openMRL(m_intf, qtu(mrl), false);
}

// A specific item has been asked to be added to the playlist
void MCMediaLib::addToPlaylist(const MLParentId & itemId)
{
    //invalid item
    if (itemId.id == 0)
        return;
    if (itemId.type == VLC_ML_PARENT_UNKNOWN)
    {
        ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media( m_ml, itemId.id ));
        if ( media )
            openMRLFromMedia(*media, false);
    }
    else
    {
        vlc_ml_query_params_t query;
        memset(&query, 0, sizeof(vlc_ml_query_params_t));
        ml_unique_ptr<vlc_ml_media_list_t> media_list(vlc_ml_list_media_of( m_ml, &query, itemId.type, itemId.id));
        for( const vlc_ml_media_t& media: ml_range_iterate<vlc_ml_media_t>( media_list ) )
            openMRLFromMedia(media, false);
    }
}

void MCMediaLib::addToPlaylist(const QVariantList& itemIdList)
{
    printf("MCMediaLib::addToPlaylist\n");
    for (const QVariant& varValue: itemIdList)
    {
        if (varValue.canConvert<QString>())
        {
            auto mrl = varValue.value<QString>();
            Open::openMRL(m_intf, qtu(mrl), false);
        }
        else if (varValue.canConvert<MLParentId>())
        {
            MLParentId itemId = varValue.value<MLParentId>();
            addToPlaylist(itemId);
        }
    }
}

// A specific item has been asked to be played,
// so it's added to the playlist and played
void MCMediaLib::addAndPlay(const MLParentId & itemId )
{
    if (itemId.id == 0)
        return;
    if (itemId.type == VLC_ML_PARENT_UNKNOWN)
    {
        ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media( m_ml, itemId.id ));
        if ( media )
            openMRLFromMedia(*media, true);
    }
    else
    {
        bool b_start = true;
        vlc_ml_query_params_t query;
        memset(&query, 0, sizeof(vlc_ml_query_params_t));

        ml_unique_ptr<vlc_ml_media_list_t> media_list(vlc_ml_list_media_of( m_ml, &query, itemId.type, itemId.id));
        for( const vlc_ml_media_t& media: ml_range_iterate<vlc_ml_media_t>( media_list ) )
        {
            openMRLFromMedia(media, b_start);
            b_start = false;
        }
    }
}

void MCMediaLib::addAndPlay(const QString& mrl)
{
    Open::openMRL(m_intf, qtu(mrl), true);
}


void MCMediaLib::addAndPlay(const QVariantList& itemIdList)
{
    printf("MCMediaLib::addAndPlay\n");
    bool b_start = true;
    for (const QVariant& varValue: itemIdList)
    {
        if (varValue.canConvert<QString>())
        {
            auto mrl = varValue.value<QString>();
            Open::openMRL(m_intf, qtu(mrl), b_start);
            b_start = false;
        }
        else if (varValue.canConvert<MLParentId>())
        {
            printf("MCMediaLib::converted\n");
            MLParentId itemId = varValue.value<MLParentId>();
            if (b_start)
                addAndPlay(itemId);
            else
                addToPlaylist(itemId);
            b_start = false;
        }
    }
}

vlc_medialibrary_t* MCMediaLib::vlcMl()
{
    return vlc_ml_instance_get( m_intf );
}

// Invoke a given QML function (used to notify the view part of a change)
void MCMediaLib::invokeQML( const char* func ) {
    QQuickItem *root = m_qmlItem->rootObject();
    int methodIndex = root->metaObject()->indexOfMethod(func);
    QMetaMethod method = root->metaObject()->method(methodIndex);
    method.invoke(root);
}

void MCMediaLib::onMediaLibraryEvent( void* data, const vlc_ml_event_t* event )
{
    MCMediaLib* self = static_cast<MCMediaLib*>( data );
    switch ( event->i_type )
    {
        case VLC_ML_EVENT_PARSING_PROGRESS_UPDATED:
            self->emit progressUpdated( event->parsing_progress.i_percent );
            break;
        case VLC_ML_EVENT_DISCOVERY_STARTED:
            self->emit discoveryStarted();
            break;
        case VLC_ML_EVENT_DISCOVERY_PROGRESS:
            self->emit discoveryProgress( event->discovery_progress.psz_entry_point );
            break;
        case VLC_ML_EVENT_DISCOVERY_COMPLETED:
            self->emit discoveryCompleted();
            break;
        default:
            break;
    }
}
