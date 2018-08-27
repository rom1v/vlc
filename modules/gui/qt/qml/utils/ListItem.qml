/*****************************************************************************
 * ListItem.qml : Item displayed inside a list view
 ****************************************************************************
 * Copyright (C) 2006-2011 VideoLAN and AUTHORS
 * $Id$
 *
 * Authors: Maël Kervella <dev@maelkervella.eu>
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

import QtQuick 2.0
import QtQuick.Layouts 1.1
import "qrc:///style/"

Rectangle {
    id: root
    signal playClicked
    signal addToPlaylistClicked
    signal itemClicked(int keys, int modifier)

    property bool hovered: false

    property Component cover: Item {}
    property alias line1: line1_text.text
    property alias line2: line2_text.text
    color: "transparent"

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            root.itemClicked(mouse.buttons, mouse.modifiers);
        }
    }

    RowLayout {
        anchors.fill: parent
        Loader {
            Layout.preferredWidth: VLCStyle.icon_normal
            Layout.preferredHeight: VLCStyle.icon_normal
            sourceComponent: root.cover
        }
        Column {
            Text{
                id: line1_text
                font.bold: true
                elide: Text.ElideRight
                color: VLCStyle.textColor
                font.pixelSize: VLCStyle.fontSize_normal
                enabled: text !== ""
            }
            Text{
                id: line2_text
                text: ""
                elide: Text.ElideRight
                color: VLCStyle.textColor
                font.pixelSize: VLCStyle.fontSize_xsmall
                enabled: text !== ""
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Image {
            id: add_to_playlist_icon

            width: VLCStyle.icon_small
            height: VLCStyle.icon_small
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: VLCStyle.icon_small
            Layout.preferredHeight: VLCStyle.icon_small

            visible: mouse.containsMouse
            source: "qrc:///buttons/playlist/playlist_add.svg"
            MouseArea {
                anchors.fill: parent
                onClicked: root.playClicked()
            }
        }

        /* The icon to add to playlist and play */
        Image {
            id: add_and_play_icon

            width: VLCStyle.icon_small
            height: VLCStyle.icon_small
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: VLCStyle.icon_small
            Layout.preferredHeight: VLCStyle.icon_small
            Layout.rightMargin: VLCStyle.margin_large
            visible: mouse.containsMouse
            source: "qrc:///toolbar/play_b.svg"
            MouseArea {
                anchors.fill: parent
                onClicked: root.addToPlaylistClicked()
            }
        }
    }
}
