/*****************************************************************************
 * MCMainDisplay.qml: Main media center display
 ****************************************************************************
 * Copyright (C) 2006-2011 VideoLAN and AUTHORS
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
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import "qrc:///style/"
import "qrc:///qml/"

ColumnLayout {
    id: column
    anchors.fill: parent

    Layout.minimumWidth: VLCStyle.minWidthMediacenter
    spacing: 0

    property var tabModel: ListModel {
        ListElement {
            displayText: qsTr("Music")
            pic: "qrc:///sidebar/music.svg"
            name: "music"
            url: "qrc:///mediacenter/MCMusicDisplay.qml"
        }
        ListElement {
            displayText: qsTr("Video")
            pic: "qrc:///sidebar/movie.svg"
            name: "video"
            url: "qrc:///mediacenter/MCVideoDisplay.qml"
        }
        ListElement {
            displayText: qsTr("Network")
            pic: "qrc:///sidebar/screen.svg"
            name: "network"
            url: "qrc:///mediacenter/MCNetworkDisplay.qml"
        }
    }

    /* Source selection*/
    BannerSources {
        id: sourcesBanner

        height: VLCStyle.heightBar_normal
        Layout.preferredHeight: height
        Layout.minimumHeight: height
        Layout.maximumHeight: height
        Layout.fillWidth: true

        need_toggleView_button: true

        model: tabModel

        onSelectedIndexChanged: {
            stackView.replace(tabModel.get(selectedIndex).url)
            stackView.focus = true
        }
    }

    StackView {
        id: stackView
        Layout.fillWidth: true
        Layout.fillHeight: true

        Component.onCompleted: push("qrc:///mediacenter/MCMusicDisplay.qml")

        replaceEnter: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 0
                to:1
                duration: 200
            }
        }

        replaceExit: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 1
                to:0
                duration: 200
            }
        }
    }
}
