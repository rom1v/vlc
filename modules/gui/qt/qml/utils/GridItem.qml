/*****************************************************************************
 * GridItem.qml : Item displayed inside a grid view
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
import QtQuick.Layouts 1.3
import "qrc:///style/"

Rectangle {
    id: root

    property Component cover: Item {}
    property string name: ""
    property string date: ""
    property string infos: ""

    property bool hovered: mouseArea.containsMouse

    signal playClicked
    signal addToPlaylistClicked
    signal itemClicked(int keys, int modifier)

    color: "transparent"

    MouseArea {
        id: mouseArea

        anchors.fill: parent

        hoverEnabled: true
        propagateComposedEvents: true
        onClicked: {
            root.itemClicked(mouse.buttons, mouse.modifiers);
            mouse.accepted = false;
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        Layout.margins: VLCStyle.margin_xxxsmall
        spacing: VLCStyle.margin_xsmall

        /* The full cover component with all added elements */
        Loader {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.width
            Layout.preferredHeight: root.width
            /* The cover */
            sourceComponent: cover

            /* Some infos displayed in the corner of the cover */
            Rectangle {
                z: 1
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                height: dur_disp.implicitHeight + VLCStyle.margin_xsmall
                width: infos === "" ? 0 : dur_disp.implicitWidth + VLCStyle.margin_xsmall

                color: VLCStyle.bgColor

                Text {
                    id: dur_disp

                    anchors.centerIn: parent

                    text: infos
                    font.pixelSize: VLCStyle.fontSize_small
                    color: VLCStyle.textColor
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: root.hovered
                z: 1

                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.5; color: Qt.rgba(VLCStyle.vlc_orange.r,VLCStyle.vlc_orange.g,VLCStyle.vlc_orange.b, 0.6) }
                    GradientStop { position: 1.0; color: VLCStyle.vlc_orange }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: VLCStyle.margin_xsmall
                    spacing: VLCStyle.margin_xsmall

                    /* A addToPlaylist button visible when hovered */
                    Image {
                        z: 1
                        height: VLCStyle.icon_normal
                        width: VLCStyle.icon_normal
                        fillMode: Image.PreserveAspectFit


                        source: "qrc:///buttons/playlist/playlist_add.svg"

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.addToPlaylistClicked()
                        }
                    }

                    /* A play button visible when hovered */
                    Image {
                        z: 1
                        height: VLCStyle.icon_normal
                        width: VLCStyle.icon_normal
                        fillMode: Image.PreserveAspectFit

                        source: "qrc:///toolbar/play_b.svg"

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.playClicked()
                        }
                    }
                }
            }
        }

        /* A section with the infos about the album */
        RowLayout {
            id: info_disp

            Layout.preferredHeight: name_text.height
            Layout.preferredWidth: root.width
            Layout.alignment: Qt.AlignHCenter
            layoutDirection: Qt.RightToLeft

            /* The year of the album */
            Text {
                id: date_text

                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: VLCStyle.fontHeight_normal

                text: date
                font.pixelSize: VLCStyle.fontSize_normal
                color: VLCStyle.textColor
            }

            /* The title of the album elided */
            Text {
                id: name_text

                Layout.fillWidth: true
                Layout.preferredHeight: VLCStyle.fontHeight_normal

                elide: Text.ElideRight
                font.bold: true
                text: name
                font.pixelSize: VLCStyle.fontSize_normal
                color: VLCStyle.textColor

                ToolTipArea {
                    id: name_tooltip
                    anchors.fill: parent
                    text: name
                    activated: parent.truncated
                }

            }
        }
    }
}

