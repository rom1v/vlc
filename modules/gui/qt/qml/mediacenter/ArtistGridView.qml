import QtQuick 2.7
import QtQuick.Controls 2.0

import "qrc:///utils/" as Utils
import "qrc:///style/"

import org.videolan.medialib 0.1


GridView {
    id: artistGridView

    cellWidth: (VLCStyle.cover_normal) + VLCStyle.margin_small
    cellHeight: (VLCStyle.cover_normal + VLCStyle.fontHeight_normal)  + VLCStyle.margin_small
    clip: true
    ScrollBar.vertical: ScrollBar { }

    delegate : Utils.GridItem {
        id: gridItem
        width: VLCStyle.cover_normal
        height: VLCStyle.cover_normal + VLCStyle.fontHeight_normal

        cover: Utils.MultiCoverPreview {
            albums: MLAlbumModel {
                ml: medialib
                parentId: model.id
            }
        }
        name: model.name || "Unknown Artist"

        onItemClicked: {
            console.log('Clicked on details : '+model.name);
        }
        onPlayClicked: {
            console.log('Clicked on play : '+model.name);
            medialib.addAndPlay( model.id )
        }
        onAddToPlaylistClicked: {
            console.log('Clicked on addToPlaylist : '+model.name);
            medialib.addToPlaylist( model.id );
        }
    }
}
