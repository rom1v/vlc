/*****************************************************************************
 * MusicAlbumsGridExpandDelegate.qml : Item in the expanded zone displayed
 *     when an album is clicked
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

import QtQuick 2.2
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import org.videolan.medialib 0.1

import "qrc:///utils/" as Utils
import "qrc:///style/"

FocusScope {
    id: root
    property var model: []
    //color: VLCStyle.colors.bg
    implicitHeight: layout.height

    signal actionUp()
    signal actionDown()
    signal actionLeft()
    signal actionRight()
    signal actionCancel()

    Keys.onPressed: {
        var newIndex = -1
        if ( event.key === Qt.Key_Down || event.matches(StandardKey.MoveToNextLine) ||event.matches(StandardKey.SelectNextLine) )
            actionDown()
        else if ( event.key === Qt.Key_Up || event.matches(StandardKey.MoveToPreviousLine) ||event.matches(StandardKey.SelectPreviousLine) )
            actionUp()
        else if (event.key === Qt.Key_Right || event.matches(StandardKey.MoveToNextChar) )
            actionRight()
        else if (event.key === Qt.Key_Left || event.matches(StandardKey.MoveToPreviousChar) )
            actionLeft()
        else if ( event.matches(StandardKey.Back) || event.matches(StandardKey.Cancel))
            actionCancel()
        event.accepted = true
    }


    Row {
        id: layout
        spacing: VLCStyle.margin_xsmall
        width: parent.width
        height: Math.max(expand_infos_id.height, artAndControl.height)

        FocusScope {
            id: artAndControl
            //width: VLCStyle.cover_large + VLCStyle.margin_small * 2
            //height: VLCStyle.cover_xsmall + VLCStyle.cover_large
            width:  artAndControlLayout.implicitWidth
            height:  artAndControlLayout.implicitHeight

            Column {
                id: artAndControlLayout
                anchors.margins: VLCStyle.margin_small
                spacing: VLCStyle.margin_small

                Item {
                    //dummy item for margins
                    width: parent.width
                    height: 1
                }

                /* A bigger cover for the album */
                Image {
                    id: expand_cover_id
                    height: VLCStyle.cover_large
                    width: VLCStyle.cover_large
                    source: model.cover || VLCStyle.noArtCover
                }

                RowLayout {
                    anchors {
                        left: parent.left
                        right: parent.right
                    }

                    Button {
                        id: addButton
                        Layout.preferredWidth: VLCStyle.icon_normal
                        Layout.preferredHeight: VLCStyle.icon_normal
                        Layout.alignment: Qt.AlignHCenter
                        icon.source: "qrc:///buttons/playlist/playlist_add.svg"
                        KeyNavigation.right: playButton
                    }
                    Button {
                        id: playButton
                        Layout.preferredWidth: VLCStyle.icon_normal
                        Layout.preferredHeight: VLCStyle.icon_normal
                        Layout.alignment: Qt.AlignHCenter
                        icon.source: "qrc:///toolbar/play_b.svg"
                        KeyNavigation.right: likeButton
                    }
                    Button {
                        id: likeButton
                        Layout.preferredWidth: VLCStyle.icon_normal
                        Layout.preferredHeight: VLCStyle.icon_normal
                        Layout.alignment: Qt.AlignHCenter
                        text: "…"
                        font.bold: true

                        Keys.onRightPressed: {
                            expand_track_id.focus = true
                            event.accepted = true
                        }
                    }
                }

                Item {
                    //dummy item for margins
                    width: parent.width
                    height: 1
                }
            }
        }


        Column {
            id: expand_infos_id

            spacing: VLCStyle.margin_xsmall
            width: root.width - x

            /* The title of the albums */
            // Needs a rectangle too prevent the tracks from overlapping the title when scrolled
            Rectangle {
                id: expand_infos_titleRect_id
                height: expand_infos_title_id.implicitHeight
                anchors {
                    left: parent.left
                    right: parent.right
                    topMargin: VLCStyle.margin_small
                    leftMargin: VLCStyle.margin_small
                    rightMargin: VLCStyle.margin_small
                }
                color: "transparent"
                Text {
                    id: expand_infos_title_id
                    text: "<b>"+(model.title || qsTr("Unknown title") )+"</b>"
                    font.pixelSize: VLCStyle.fontSize_xxlarge
                    color: VLCStyle.colors.text
                }
            }

            Rectangle {
                id: expand_infos_subtitleRect_id
                height: expand_infos_subtitle_id.implicitHeight
                anchors {
                    left: parent.left
                    right: parent.right
                    topMargin: VLCStyle.margin_xxsmall
                    leftMargin: VLCStyle.margin_small
                    rightMargin: VLCStyle.margin_small
                }

                color: "transparent"
                Text {
                    id: expand_infos_subtitle_id
                    text: qsTr("By %1 - %2 - %3")
                    .arg(model.main_artist || qsTr("Unknown title"))
                    .arg(model.release_year || "")
                    .arg(model.duration || "")
                    font.pixelSize: VLCStyle.fontSize_large
                    color: VLCStyle.colors.text
                }
            }

            /* The list of the tracks available */
            MusicTrackListDisplay {
                id: expand_track_id

                height: expand_track_id.contentHeight
                anchors {
                    left: parent.left
                    right: parent.right
                    topMargin: VLCStyle.margin_xxsmall
                    leftMargin: VLCStyle.margin_small
                    rightMargin: VLCStyle.margin_small
                    bottomMargin: VLCStyle.margin_small
                }

                interactive: false

                parentId : root.model.id
                sortModel: ListModel {
                    ListElement{ criteria: "track_number";  width:0.10; visible: true; text: qsTr("#"); showSection: "" }
                    ListElement{ criteria: "title";         width:0.70; visible: true; text: qsTr("TITLE"); showSection: "" }
                    ListElement{ criteria: "duration";      width:0.20; visible: true; text: qsTr("DURATION"); showSection: "" }
                }
                focus: true

                onActionLeft:  artAndControl.focus = true
                onActionRight: root.actionRight()
                onActionCancel: root.actionCancel()
            }

            Item {
                //dummy item for margins
                width: parent.width
                height: 1
            }
        }
    }

}
