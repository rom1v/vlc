/*****************************************************************************
 * mlnetworkmodel.cpp: Model providing a list of indexable network shares
 ****************************************************************************
 * Copyright (C) 2018 VideoLAN and AUTHORS
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

#include "mlnetworkmodel.hpp"

#include "mlhelper.hpp"

namespace {

enum Role {
    NETWORK_NAME = Qt::UserRole + 1,
    NETWORK_MRL,
    NETWORK_INDEXED,
    NETWORK_CANINDEX,
    NETWORK_ISDIR,
};

}

MLNetworkModel::MLNetworkModel( QmlMainContext* ctx, QString parentMrl, QObject* parent )
    : QAbstractListModel( parent )
    , m_sd( nullptr, &vlc_sd_Destroy )
    , m_input( nullptr, &input_Close )
    , m_entryPoints( nullptr, &vlc_ml_entry_point_list_release )
    , m_ctx( ctx )
    , m_parentMrl( parentMrl )
{
    initializeKnownEntrypoints();
    if ( parentMrl.isEmpty() )
        initializeDeviceDiscovery();
    else
        initializeFolderDiscovery();
}

QVariant MLNetworkModel::data( const QModelIndex& index, int role ) const
{
    auto idx = index.row();
    if ( idx < 0 || (size_t)idx >= m_items.size() )
        return {};
    const auto& item = m_items[idx];
    switch ( role )
    {
        case NETWORK_NAME:
            return QVariant::fromValue( QString::fromStdString( item.name ) );
        case NETWORK_MRL:
            return QVariant::fromValue( QString::fromStdString( item.mrl ) );
        case NETWORK_INDEXED:
            return QVariant::fromValue( item.indexed );
        case NETWORK_CANINDEX:
            return QVariant::fromValue( item.canBeIndexed );
        case NETWORK_ISDIR:
            return QVariant::fromValue( item.isDir );
        default:
            return {};
    }
}

QHash<int, QByteArray> MLNetworkModel::roleNames() const
{
    return {
        { NETWORK_NAME, "name" },
        { NETWORK_MRL, "mrl" },
        { NETWORK_INDEXED, "indexed" },
        { NETWORK_CANINDEX, "can_index" },
        { NETWORK_ISDIR, "is_dir" }
    };
}

int MLNetworkModel::rowCount(const QModelIndex& parent) const
{
    if ( parent.isValid() )
        return 0;
    assert( m_items.size() < INT32_MAX );
    return static_cast<int>( m_items.size() );
}

Qt::ItemFlags MLNetworkModel::flags( const QModelIndex& idx ) const
{
    return QAbstractListModel::flags( idx ) | Qt::ItemIsEditable;
}

bool MLNetworkModel::setData( const QModelIndex& idx, const QVariant& value, int role )
{
    if ( role != NETWORK_INDEXED )
        return false;
    auto ml = vlc_ml_instance_get( m_ctx->getIntf() );
    assert( ml != nullptr );
    auto enabled = value.toBool();
    assert( m_items[idx.row()].indexed != enabled );
    auto mrl = m_items[idx.row()].mrl.c_str();
    int res;
    if ( enabled )
        res = vlc_ml_add_folder( ml, mrl );
    else
        res = vlc_ml_remove_folder( ml, mrl );
    m_items[idx.row()].indexed = enabled;
    return res == VLC_SUCCESS;
}

bool MLNetworkModel::initializeKnownEntrypoints()
{
    auto ml = vlc_ml_instance_get( m_ctx->getIntf() );
    assert( ml != nullptr );
    vlc_ml_entry_point_list_t *entryPoints;
    if ( vlc_ml_list_folder( ml, &entryPoints ) != VLC_SUCCESS )
        return false;
    m_entryPoints.reset( entryPoints );
    return true;
}

bool MLNetworkModel::initializeDeviceDiscovery()
{
    static const services_discovery_callbacks cbs = {
        .item_added = &MLNetworkModel::onItemAdded,
        .item_removed = &MLNetworkModel::onItemRemoved,
    };
    services_discovery_owner_t owner = {
        .cbs = &cbs,
        .sys = this
    };
    m_sd.reset( vlc_sd_Create( VLC_OBJECT( m_ctx->getIntf() ), "dsm-sd", &owner ) );
    if ( !m_sd )
    {
        msg_Warn( m_ctx->getIntf(), "Failed to instantiate SD" );
        return false;
    }
    return true;
}

bool MLNetworkModel::initializeFolderDiscovery()
{
    std::unique_ptr<input_item_t, decltype(&input_item_Release)> inputItem{
        input_item_New( qtu( m_parentMrl ), NULL ),
        &input_item_Release
    };
    inputItem->i_preparse_depth = 1;
    if ( inputItem == nullptr )
        return false;
    m_input.reset( input_CreatePreparser( VLC_OBJECT( m_ctx->getIntf() ),
                                          &MLNetworkModel::onInputEvent,
                                          this, inputItem.get() ) );
    if ( m_input == nullptr )
        return false;
    input_Start( m_input.get() );
    return true;
}

void MLNetworkModel::onItemAdded( input_item_t* parent, input_item_t* p_item,
                                  const char* )
{
    if ( ( parent == nullptr ) != m_parentMrl.isEmpty() )
        return;
    input_item_Hold( p_item );
    callAsync([this, p_item]() {
        auto it = std::find_if( begin( m_items ), end( m_items ), [p_item](const Item& i) {
            return i.mrl == p_item->psz_uri;
        });
        if ( it != end( m_items ) )
        {
            input_item_Release( p_item );
            return;
        }
        Item i;
        i.mrl = p_item->psz_uri;
        i.name = p_item->psz_name;
        i.indexed = false;
        i.canBeIndexed = true;
        i.isDir = true;
        if ( m_entryPoints != nullptr )
        {
            for ( const auto& ep : ml_range_iterate<vlc_ml_entry_point_t>( m_entryPoints ) )
            {
                if ( ep.b_present && strcasecmp( ep.psz_mrl, p_item->psz_uri ) == 0 )
                {
                    i.indexed = true;
                    break;
                }
            }
        }

        input_item_Release( p_item );

        beginInsertRows( {}, m_items.size(), m_items.size() );
        m_items.push_back( std::move( i ) );
        endInsertRows();
    });
}

void MLNetworkModel::onItemRemoved( input_item_t* p_item )
{
    input_item_Hold( p_item );
    callAsync([this, p_item]() {
        auto it = std::find_if( begin( m_items ), end( m_items ), [p_item](const Item& i) {
            return i.mrl == p_item->psz_uri;
        });
        input_item_Release( p_item );
        assert( it != end( m_items ) );
        auto idx = std::distance( begin( m_items ), it );
        beginRemoveRows({}, idx, idx );
        m_items.erase( it );
        endRemoveRows();
    });
}

void MLNetworkModel::onInputEvent( input_thread_t*, const vlc_input_event* event )
{
    if ( event->type != INPUT_EVENT_SUBITEMS )
        return;
    auto isIndexed = false;
    if ( m_entryPoints != nullptr )
    {
        for ( const auto& ep : ml_range_iterate<vlc_ml_entry_point_t>( m_entryPoints ) )
        {
            if ( ep.b_present && strncasecmp( ep.psz_mrl, event->subitems->p_item->psz_uri,
                                              strlen(ep.psz_mrl) ) == 0 )
            {
                isIndexed = true;
                break;
            }
        }
    }
    std::vector<Item> items;
    for ( auto i = 0; i < event->subitems->i_children; ++i )
    {
        auto it = event->subitems->pp_children[i]->p_item;
        items.push_back( Item{ it->psz_name, it->psz_uri, isIndexed,
                            it->i_type == ITEM_TYPE_DIRECTORY,
                            it->i_type == ITEM_TYPE_DIRECTORY
                        } );
    }
    callAsync([this, items = std::move(items)]() {
        beginInsertRows( {}, m_items.size(), m_items.size() + items.size() - 1 );
        std::move( begin( items ), end( items ), std::back_inserter( m_items ) );
        endInsertRows();
    });
}

void MLNetworkModel::onItemAdded( services_discovery_t* sd, input_item_t* parent,
                                  input_item_t* p_item, const char* psz_cat )
{
    MLNetworkModel* self = static_cast<MLNetworkModel*>( sd->owner.sys );
    self->onItemAdded( parent, p_item, psz_cat );
}

void MLNetworkModel::onItemRemoved( services_discovery_t* sd,
                                    input_item_t* p_item )
{
    MLNetworkModel* self = static_cast<MLNetworkModel*>( sd->owner.sys );
    self->onItemRemoved( p_item );
}

void MLNetworkModel::onInputEvent( input_thread_t* input,
                                   const vlc_input_event* event, void* data )
{
    MLNetworkModel* self = static_cast<MLNetworkModel*>( data );
    self->onInputEvent( input, event );
}
