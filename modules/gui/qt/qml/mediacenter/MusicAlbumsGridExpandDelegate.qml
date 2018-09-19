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

Rectangle {
    id: root
    property var model: []
    color: VLCStyle.colors.bg

    RowLayout {
        spacing: VLCStyle.margin_xsmall
        anchors.fill: parent

        /* A bigger cover for the album */
        Image {
            id: expand_cover_id

            Layout.preferredHeight: VLCStyle.cover_large
            Layout.preferredWidth: VLCStyle.cover_large
            Layout.alignment: Qt.AlignTop
            Layout.margins: VLCStyle.margin_small

            source: model.cover || VLCStyle.noArtCover
        }

        ColumnLayout {
            id: expand_infos_id

            spacing: VLCStyle.margin_xsmall
            Layout.fillWidth:  true

            /* The title of the albums */
            // Needs a rectangle too prevent the tracks from overlapping the title when scrolled
            Rectangle {
                id: expand_infos_titleRect_id
                height: expand_infos_title_id.implicitHeight
                Layout.fillWidth:  true
                Layout.alignment: Qt.AlignLeft
                Layout.topMargin: VLCStyle.margin_small
                Layout.leftMargin: VLCStyle.margin_small
                Layout.rightMargin: VLCStyle.margin_small
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
                Layout.fillWidth:  true
                Layout.alignment: Qt.AlignLeft
                Layout.leftMargin: VLCStyle.margin_small
                Layout.rightMargin: VLCStyle.margin_small
                Layout.topMargin: VLCStyle.margin_xxsmall
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

                Layout.alignment: Qt.AlignLeft
                Layout.leftMargin: VLCStyle.margin_small
                Layout.rightMargin: VLCStyle.margin_small
                Layout.topMargin: VLCStyle.margin_xsmall
                Layout.bottomMargin: VLCStyle.margin_small
                Layout.fillHeight: true
                Layout.fillWidth: true

                Layout.preferredHeight: expand_track_id.flickableItem.height

                parentId : root.model.id

                columnModel: ListModel {
                    ListElement{ role: "title";    visible: true; title: qsTr("TITLE"); showSection: "" }
                    ListElement{ role: "duration"; visible: true; title: qsTr("DURATION"); showSection: "" }
                }
            }
        }
    }
}
