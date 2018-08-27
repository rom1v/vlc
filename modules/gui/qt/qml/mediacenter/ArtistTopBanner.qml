import QtQuick 2.0
import QtQuick.Controls 2.0
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.3
import QtGraphicalEffects 1.0

import org.videolan.medialib 0.1

import "qrc:///utils/" as Utils
import "qrc:///style/"

Rectangle {
    id: root
    property var artist: null
    color: VLCStyle.bgColor

    Image {
        id: artistImage
        source: artist.cover || VLCStyle.noArtCover

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left

        width: VLCStyle.cover_small
        height: VLCStyle.cover_small

        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: artistImage.width
                height: artistImage.height
                radius: artistImage.width
            }
        }
    }

    Text {
        id: main_artist
        text: artist.name
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: VLCStyle.margin_large
        anchors.left: artistImage.right
        font.pixelSize: VLCStyle.fontSize_xxxlarge
        font.bold: true
        color: VLCStyle.textColor
    }

    states: [
        State {
            name: "full"
            PropertyChanges {
                target: artistImage
                width: VLCStyle.cover_small
                height: VLCStyle.cover_small
            }
            PropertyChanges {
                target: main_artist
                font.pixelSize: VLCStyle.fontSize_xxxlarge
            }
            when: root.height >= VLCStyle.heightBar_xlarge
        },
        State {
            name: "small"
            PropertyChanges {
                target: artistImage
                width: VLCStyle.icon_normal
                height: VLCStyle.icon_normal
            }
            PropertyChanges {
                target: main_artist
                font.pixelSize: VLCStyle.fontSize_large
                anchors.leftMargin: VLCStyle.margin_small
            }
            when: root.height < VLCStyle.heightBar_xlarge
        }
    ]

}
