import QtQuick 2.0
import QtQuick.Controls 2.0

import "qrc:///utils/" as Utils
import "qrc:///style/"

ListView {
    id: artistListView
    property var onItemClicked

    spacing: 2
    delegate : Utils.ListItem {
        height: VLCStyle.icon_normal
        width: parent.width

        cover: Image {
            id: cover_obj
            fillMode: Image.PreserveAspectFit
            source: model.cover || VLCStyle.noArtCover
        }
        line1: model.name || qsTr("Unknown artist")


        onItemClicked: {
            artistListView.onItemClicked( model.id )
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

    ScrollBar.vertical: ScrollBar { }
}
