#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "components/media_browser/media_browser_model.hpp"

#include <vlc_media_browser.h>

MediaBrowserModel::MediaBrowserModel( intf_thread_t *p_intf,
                                      media_browser_t *p_media_browser,
                                      QObject *parent )
    : QAbstractListModel( parent )
    , p_intf( p_intf )
    , p_media_browser( p_media_browser )
{
}

QHash<int, QByteArray> MediaBrowserModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { DurationRole, "duration" },
    };
}

int MediaBrowserModel::rowCount( const QModelIndex &parent ) const
{
    VLC_UNUSED( parent );
    return 0;
}

QVariant MediaBrowserModel::data( const QModelIndex &index, int role ) const
{
    return {};
}
