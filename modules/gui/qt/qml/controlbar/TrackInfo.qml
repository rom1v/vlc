import QtQuick 2.10
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3
import org.videolan.vlc 0.1

import "qrc:///style/"
import "qrc:///utils/" as Utils

Item {
    id: root

    PlaylistControlerModel {
        id: playlist
        playlistPtr: mainctx.playlist
    }

    RowLayout {
        id: rowLayout
        anchors.fill: parent

        Image {
            //color:"red"
            Layout.preferredWidth: VLCStyle.heightAlbumCover_small
            Layout.preferredHeight: VLCStyle.heightAlbumCover_small
            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
            source: (playlist.currentItem.artwork && playlist.currentItem.artwork.toString()) ? playlist.currentItem.artwork : VLCStyle.noArtCover
        }

        ColumnLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

                font.pixelSize: VLCStyle.fontSize_normal
                color: VLCStyle.colors.text
                font.bold: true

                text: playlist.currentItem.title
            }

            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

                font.pixelSize: VLCStyle.fontSize_small

                color: VLCStyle.colors.text
                text: playlist.currentItem.artist
            }

        }

    }

}
