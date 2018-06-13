#ifndef VLC_QT_MEDIA_BROWSER_MODEL_HPP_
#define VLC_QT_MEDIA_BROWSER_MODEL_HPP_

#include <QAbstractListModel>
#include <QList>

typedef struct intf_thread_t intf_thread_t;
typedef struct media_browser_t media_browser_t;

class MediaBrowserModel : public QAbstractListModel
{
    Q_OBJECT
    Q_ENUMS(Roles)

public:
    enum Roles
    {
        TitleRole = Qt::UserRole,
        DurationRole,
    };

    MediaBrowserModel( intf_thread_t *, media_browser_t *, QObject *parent );

    QHash<int, QByteArray> roleNames() const override;
    int rowCount( const QModelIndex &parent = {} ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;

private:
    intf_thread_t *p_intf;
    media_browser_t *p_media_browser;
};

#endif
