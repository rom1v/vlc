#ifndef VLC_QT_MEDIA_BROWSER_MODEL_HPP_
#define VLC_QT_MEDIA_BROWSER_MODEL_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QAbstractListModel>
#include <QList>
#include <QMutex>
#include <memory>
#include "components/media_tree/input_item.hpp"

typedef struct intf_thread_t intf_thread_t;
typedef struct media_tree_t media_tree_t;
typedef struct media_node_t media_node_t;

class MediaTreeModel : public QAbstractListModel
{
    Q_OBJECT
    Q_ENUMS(Roles)

public:
    enum Roles
    {
        TitleRole = Qt::UserRole,
        DurationRole,
    };

    MediaTreeModel( intf_thread_t *, media_tree_t *, QObject *parent );
    ~MediaTreeModel();

    QHash<int, QByteArray> roleNames() const override;
    int rowCount( const QModelIndex &parent = {} ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;

signals:
    void inputItemAdded( InputItem parent, InputItem );
    void inputItemRemoved( InputItem parent, InputItem );
    void inputItemUpdated( InputItem parent, InputItem );

private slots:
    void onInputItemAdded( InputItem parent, InputItem );
    void onInputItemRemoved( InputItem parent, InputItem );
    void onInputItemUpdated( InputItem parent, InputItem );

private:
    class Node {
    public:
        Node() = default;
        Node( const InputItem & );
        Node( InputItem && );

        Node *find( const InputItem &input );
        void addChild( const InputItem &input );
        bool removeChild( const InputItem &input );

    private:
        Node *find( Node &node, const InputItem &input );

        InputItem input;
        Node *parent = nullptr;
        QList<Node> children;

        friend MediaTreeModel;
    };

    intf_thread_t *intf;
    media_tree_t *mediaTree; // source tree, accessed by many threads (but never by the MediaTreeModel thread)
    Node root; // live in MediaTreeModel thread
    Node &currentNode = root;
};

#endif
