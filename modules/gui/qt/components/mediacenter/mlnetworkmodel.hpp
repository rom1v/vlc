/*****************************************************************************
 * mlnetworkmodel.hpp: Model providing a list of indexable network shares
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

#ifndef MLNETWORKMODEL_HPP
#define MLNETWORKMODEL_HPP

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <QAbstractListModel>

#include <vlc_media_library.h>
#include <vlc_services_discovery.h>
#include <vlc_threads.h>
#include <vlc_cxx_helpers.hpp>

#include <components/playlist/qml_main_context.hpp>

#include <memory>

class MLNetworkModel : public QAbstractListModel
{
    Q_OBJECT

    struct Item
    {
        std::string name;
        std::string mrl;
        bool selected;
    };
public:
    MLNetworkModel( QmlMainContext* ctx, QString parentMrl, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex& parent) const override;

    Qt::ItemFlags flags( const QModelIndex& idx ) const override;
    bool setData( const QModelIndex& idx,const QVariant& value, int role ) override;

private:
    ///call function @a fun on object thread
    template <typename Fun>
    void callAsync(Fun&& fun)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
        QMetaObject::invokeMethod(this, std::forward<Fun>(fun), Qt::QueuedConnection, nullptr);
#else
        QObject src;
        QObject::connect(&src, &QObject::destroyed, q, std::forward<Fun>(fun), Qt::QueuedConnection);
#endif
    }

    bool initializeKnownEntrypoints();
    bool initializeDeviceDiscovery();
    bool initializeFolderDiscovery();
    void onItemAdded( input_item_t *parent, input_item_t *p_item,
                      const char *psz_cat );
    void onItemRemoved( input_item_t *p_item );
    void onInputEvent( input_thread_t* input, const vlc_input_event *event );

    static void onItemAdded( services_discovery_t *sd, input_item_t *parent,
                             input_item_t *p_item, const char *psz_cat );
    static void onItemRemoved( services_discovery_t *sd, input_item_t *p_item );

    static void onInputEvent( input_thread_t* input, const vlc_input_event *event,
                              void *data );

private:
    std::vector<Item> m_items;
    std::unique_ptr<services_discovery_t, decltype(&vlc_sd_Destroy)> m_sd;
    std::unique_ptr<input_thread_t, decltype(&input_Close)> m_input;
    std::unique_ptr<vlc_ml_entry_point_list_t, decltype(&vlc_ml_entry_point_list_release)> m_entryPoints;
    QmlMainContext* m_ctx;
    QString m_parentMrl;
};


struct MLNetworkModelFactory : public QObject
{
    Q_OBJECT
public:
    explicit MLNetworkModelFactory(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE MLNetworkModel* create( QmlMainContext* ctx, QString parentMrl )
    {
        return new MLNetworkModel( ctx, parentMrl );
    }
};


#endif // MLNETWORKMODEL_HPP
