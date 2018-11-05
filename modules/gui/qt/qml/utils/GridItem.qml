/*****************************************************************************
 * GridItem.qml : Item displayed inside a grid view
 ****************************************************************************
 * Copyright (C) 2006-2011 VideoLAN and AUTHORS
 * $Id$
 *
 * Authors: Maël Kervella <dev@maelkervella.eu>
 * Authors: Pierre Lamot <pierre@videolabs.io>
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

import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.1
import QtQml.Models 2.2
import QtGraphicalEffects 1.0
import org.videolan.medialib 0.1


import "qrc:///utils/" as Utils
import "qrc:///style/"

Item {
    id: root
    width: VLCStyle.cover_normal
    height: VLCStyle.cover_normal
            + VLCStyle.fontHeight_normal
            + VLCStyle.fontHeight_small
            + VLCStyle.margin_xsmall

    property url image: VLCStyle.noArtCover
    property string title: ""
    property string subtitle: ""
    property bool selected: false
    property int shiftX: 0

    signal playClicked
    signal addToPlaylistClicked
    signal itemClicked(int keys, int modifier)
    signal itemDoubleClicked(int keys, int modifier)

    Item {
        x: shiftX
        width: parent.width
        height: parent.height

        MouseArea {

            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked:  root.itemClicked(mouse.buttons, mouse.modifiers)
            onDoubleClicked: root.itemDoubleClicked(mouse.buttons, mouse.modifiers);

            ColumnLayout {
                anchors.fill: parent
                Item {
                    id: picture
                    width: VLCStyle.cover_normal
                    height: VLCStyle.cover_normal
                    property bool highlighted: selected || mouseArea.containsMouse

                    RectangularGlow {
                        visible: picture.highlighted
                        anchors.fill: cover
                        cornerRadius: 25
                        spread: 0.2
                        glowRadius: VLCStyle.margin_xsmall
                        color: VLCStyle.colors.getBgColor( selected, mouseArea.containsMouse, root.activeFocus )
                    }
                    Image {
                        id: cover
                        width: VLCStyle.cover_small
                        height: VLCStyle.cover_small
                        Behavior on width  { SmoothedAnimation { velocity: 100 } }
                        Behavior on height { SmoothedAnimation { velocity: 100 } }
                        anchors.centerIn: parent
                        source: image

                        Rectangle {
                            id: overlay
                            anchors.fill: parent
                            visible: mouseArea.containsMouse
                            color: "black" //darken the image below

                            RowLayout {
                                anchors.fill: parent
                                Item {
                                    Layout.fillHeight: true
                                    Layout.fillWidth: true
                                    /* A addToPlaylist button visible when hovered */
                                    Image {
                                        property int iconSize: VLCStyle.icon_normal
                                        Behavior on iconSize  { SmoothedAnimation { velocity: 100 } }
                                        Binding on iconSize {
                                            value: VLCStyle.icon_normal * 1.2
                                            when: mouseAreaAdd.containsMouse
                                        }

                                        //Layout.alignment: Qt.AlignCenter
                                        anchors.centerIn: parent

                                        height: iconSize
                                        width: iconSize
                                        fillMode: Image.PreserveAspectFit
                                        source: "qrc:///buttons/playlist/playlist_add.svg"
                                        MouseArea {
                                            id: mouseAreaAdd
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            propagateComposedEvents: true
                                            onClicked: root.addToPlaylistClicked()
                                        }
                                        ColorOverlay {
                                            anchors.fill: parent
                                            source: parent
                                            visible: mouseAreaAdd.containsMouse
                                            color: "#80FFFFFF"
                                        }
                                    }
                                }

                                /* A play button visible when hovered */
                                Item {
                                    Layout.fillHeight: true
                                    Layout.fillWidth: true

                                    Image {
                                        property int iconSize: VLCStyle.icon_normal
                                        Behavior on iconSize  {
                                            SmoothedAnimation { velocity: 100 }
                                        }
                                        Binding on iconSize {
                                            value: VLCStyle.icon_normal * 1.2
                                            when: mouseAreaPlay.containsMouse
                                        }

                                        anchors.centerIn: parent
                                        //Layout.alignment: Qt.AlignCenter
                                        height: iconSize
                                        width: iconSize
                                        fillMode: Image.PreserveAspectFit
                                        source: "qrc:///toolbar/play_b.svg"
                                        MouseArea {
                                            id: mouseAreaPlay
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: root.playClicked()
                                        }
                                        ColorOverlay {
                                            anchors.fill: parent
                                            source: parent
                                            visible: mouseAreaPlay.containsMouse
                                            color: "#80FFFFFF"
                                        }
                                    }
                                }
                            }
                        }
                        states: [
                            State {
                                name: "visible"
                                PropertyChanges { target: overlay; visible: true }
                                when: mouseArea.containsMouse
                            },
                            State {
                                name: "hidden"
                                PropertyChanges { target: overlay; visible: false }
                                when: !mouseArea.containsMouse
                            }
                        ]
                        transitions: [
                            Transition {
                                from: "hidden";  to: "visible"
                                NumberAnimation  {
                                    target: overlay
                                    properties: "opacity"
                                    from: 0; to: 0.8; duration: 300
                                }
                            }
                        ]
                    }

                    states: [
                        State {
                            name: "big"
                            when: picture.highlighted
                            PropertyChanges {
                                target: cover
                                width:  VLCStyle.cover_normal - 2 * VLCStyle.margin_xsmall
                                height: VLCStyle.cover_normal - 2 * VLCStyle.margin_xsmall
                            }
                        },
                        State {
                            name: "small"
                            when: !picture.highlighted
                            PropertyChanges {
                                target: cover
                                width:  VLCStyle.cover_normal - 2 * VLCStyle.margin_small
                                height: VLCStyle.cover_normal - 2 * VLCStyle.margin_small
                            }
                        }
                    ]
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: VLCStyle.margin_small
                    Layout.rightMargin: VLCStyle.margin_small

                    text: root.title

                    elide: Text.ElideRight
                    font.pixelSize: VLCStyle.fontSize_normal
                    font.bold: true
                    color: VLCStyle.colors.text
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: VLCStyle.margin_small
                    Layout.rightMargin: VLCStyle.margin_small

                    text : root.subtitle

                    elide: Text.ElideRight
                    font.pixelSize: VLCStyle.fontSize_small
                    color: VLCStyle.colors.text
                }
            }
        }
    }
}
