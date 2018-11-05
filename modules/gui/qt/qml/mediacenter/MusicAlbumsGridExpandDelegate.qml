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

    Row {
        id: layout
        spacing: VLCStyle.margin_xsmall
        width: parent.width
        height: expand_infos_id.height

        /* A bigger cover for the album */
        Image {
            id: expand_cover_id

            height: VLCStyle.cover_large
            width: VLCStyle.cover_large
            anchors {
                top: parent.top
                margins: VLCStyle.margin_small
            }

            source: model.cover || VLCStyle.noArtCover
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
                    text: "<b>"+(model.title || "Unknown title")+"</b>"
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
                        .arg(model.main_artist || "Unknown title")
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
            }

            Item {
                width: parent.width
                height: 1
            }
        }
    }
}
