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
};

}

MLNetworkModel::MLNetworkModel(QObject *parent)
    : QAbstractListModel( parent )
    , m_sd( nullptr, &vlc_sd_Destroy )
    , m_entryPoints( nullptr, &vlc_ml_entry_point_list_release )
    , m_ctx( nullptr )
{
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
            return QVariant::fromValue( item.selected );
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
    };
}

int MLNetworkModel::rowCount(const QModelIndex& parent) const
{
    if ( parent.isValid() )
        return 0;
    assert( m_items.size() < INT32_MAX );
    return static_cast<int>( m_items.size() );
}

QmlMainContext*MLNetworkModel::getMainContext() const
{
    return m_ctx;
}

void MLNetworkModel::setMainContext( QmlMainContext* ctx )
{
    assert( m_ctx == nullptr );
    m_ctx = ctx;
    initializeKnownEntrypoints();
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
        return;
    }
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
    assert( m_items[idx.row()].selected != enabled );
    auto mrl = m_items[idx.row()].mrl.c_str();
    int res;
    if ( enabled )
        res = vlc_ml_add_folder( ml, mrl );
    else
        res = vlc_ml_remove_folder( ml, mrl );
    m_items[idx.row()].selected = enabled;
    return res == VLC_SUCCESS;
}

bool MLNetworkModel::initializeKnownEntrypoints()
{
    assert( m_ctx != nullptr );
    auto ml = vlc_ml_instance_get( m_ctx->getIntf() );
    assert( ml != nullptr );
    vlc_ml_entry_point_list_t *entryPoints;
    if ( vlc_ml_list_folder( ml, &entryPoints ) != VLC_SUCCESS )
        return false;
    m_entryPoints.reset( entryPoints );
    return true;
}

void MLNetworkModel::onItemAdded( input_item_t* parent, input_item_t* p_item,
                                  const char* )
{
    if ( parent != nullptr )
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
        i.selected = false;
        if ( m_entryPoints != nullptr )
        {
            for ( const auto& ep : ml_range_iterate<vlc_ml_entry_point_t>( m_entryPoints ) )
            {
                if ( ep.b_present && strcasecmp( ep.psz_mrl, p_item->psz_uri ) == 0 )
                {
                    i.selected = true;
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
