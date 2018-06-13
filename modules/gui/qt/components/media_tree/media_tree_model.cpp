#include "components/media_tree/media_tree_model.hpp"
#include "components/media_tree/input_item.hpp"

#include <vlc_media_tree.h>

#include <QList>

extern "C" {

static void media_tree_node_added( media_tree_t *tree, const media_node_t *node, void *userdata )
{
    VLC_UNUSED( tree ); // it is already stored in model
    MediaTreeModel *model = static_cast<MediaTreeModel *>( userdata );
    emit model->inputItemAdded( InputItem( node->p_parent->p_input ), InputItem( node->p_input ) );
}

static void media_tree_node_removed( media_tree_t *tree, const media_node_t *node, void *userdata )
{
    VLC_UNUSED( tree ); // it is already stored in model
    MediaTreeModel *model = static_cast<MediaTreeModel *>( userdata );
    emit model->inputItemRemoved( InputItem( node->p_parent->p_input ), InputItem( node->p_input ) );
}

static void media_tree_input_updated( media_tree_t *tree, const media_node_t *node, void *userdata )
{
    VLC_UNUSED( tree ); // it is already stored in model
    MediaTreeModel *model = static_cast<MediaTreeModel *>( userdata );
    emit model->inputItemUpdated( InputItem( node->p_parent->p_input ), InputItem( node->p_input ) );
}

} // extern "C"

static const media_tree_callbacks_t media_tree_callbacks = {
    .pf_tree_connected = media_tree_connected_default,
    .pf_subtree_added = media_tree_subtree_added_default,
    .pf_node_added = media_tree_node_added,
    .pf_node_removed = media_tree_node_removed,
    .pf_input_updated = media_tree_input_updated,
};

MediaTreeModel::MediaTreeModel( intf_thread_t *intf,
                                media_tree_t *mediaTree,
                                QObject *parent )
    : QAbstractListModel( parent )
    , intf( intf )
    , mediaTree( mediaTree )
{
    connect( this, &MediaTreeModel::inputItemAdded, this, &MediaTreeModel::onInputItemAdded );
    connect( this, &MediaTreeModel::inputItemRemoved, this, &MediaTreeModel::onInputItemRemoved );
    connect( this, &MediaTreeModel::inputItemUpdated, this, &MediaTreeModel::onInputItemUpdated );

    media_tree_Hold( mediaTree );
    media_tree_Connect( mediaTree, &media_tree_callbacks, this );
}

MediaTreeModel::~MediaTreeModel()
{
    media_tree_Release( mediaTree );
}

QHash<int, QByteArray> MediaTreeModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { DurationRole, "duration" },
    };
}

int MediaTreeModel::rowCount( const QModelIndex &parent ) const
{
    VLC_UNUSED( parent );
    return 0;
}

QVariant MediaTreeModel::data( const QModelIndex &index, int role ) const
{
    return {};
}

MediaTreeModel::Node::Node( const InputItem &input )
    : input( input )
{
}

MediaTreeModel::Node::Node( InputItem &&input )
    : input( std::move( input ) )
{
}

auto MediaTreeModel::Node::find( Node &node, const InputItem &input) -> Node *
{
    if( node.input == input )
        return &node;

    for( Node &child : node.children )
    {
        Node *res = find( child, input );
        if( res )
            return res;
    }

    return nullptr;
}

auto MediaTreeModel::Node::find( const InputItem &input ) -> Node *
{
    return find( *this, input );
}

void MediaTreeModel::Node::addChild( const InputItem &input )
{
    children.append( input );
}

bool MediaTreeModel::Node::removeChild( const InputItem &input )
{
    for( int i = 0; i < children.size(); ++i )
    {
        if( children[i].input == input )
        {
            children.removeAt( i );
            return true;
        }
    }
    return false;
}

void MediaTreeModel::onInputItemAdded( InputItem parent, InputItem input )
{
    Node *parentNode = root.find( input );
    Q_ASSERT( parentNode );
    parentNode->addChild( input );
}

void MediaTreeModel::onInputItemRemoved( InputItem parent, InputItem input )
{
    Node *parentNode = root.find( input );
    Q_ASSERT( parentNode );
    bool removed = parentNode->removeChild( input );
#ifdef QT_DEBUG
    Q_ASSERT( removed );
#else
    Q_UNUSED( removed );
#endif
}

void MediaTreeModel::onInputItemUpdated( InputItem parent, InputItem input )
{

}
