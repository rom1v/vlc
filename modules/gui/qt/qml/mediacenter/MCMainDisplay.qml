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

import QtQuick 2.7
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import org.videolan.medialib 0.1

import "qrc:///style/"
import "qrc:///qml/"
import "qrc:///utils/" as Utils


Utils.NavigableFocusScope {
    id: root

    //name and properties of the tab to be initially loaded
    property string view: "music"
    property var viewProperties: QtObject {}

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

    Connections {
        target: history
        onCurrentChanged: {
            if ( !current || !current.view ) {
                console.warn("unable to load requested view, undefined")
                return
            }
            stackView.loadView(current.view, current.viewProperties)
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent


        Layout.minimumWidth: VLCStyle.minWidthMediacenter
        spacing: 0

        /* Source selection*/
        BannerSources {
            id: sourcesBanner

            Layout.preferredHeight: height
            Layout.minimumHeight: height
            Layout.maximumHeight: height
            Layout.fillWidth: true

            need_toggleView_button: true

            model: root.tabModel

            onSelectedIndexChanged: {
                stackView.replace(root.tabModel.get(selectedIndex).url)
                history.push({
                                 view: root.tabModel.get(selectedIndex).name,
                                 viewProperties: {}
                             }, History.Stay)
                stackView.focus = true
            }

            onActionDown: stackView.focus = true

            onActionLeft: root.actionLeft(index)
            onActionRight: root.actionRight(index)
            onActionUp: root.actionUp(index)
            onActionCancel: root.actionCancel(index)
        }

        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true

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

            function loadView(name, viewProperties)
            {
                var found = false
                for (var tab = 0; tab < root.tabModel.count; tab++ )
                    if (root.tabModel.get(tab).name === name) {
                        //we can't use push(url, properties) as Qt interprets viewProperties
                        //as a second component to load
                        var component = Qt.createComponent(root.tabModel.get(tab).url)
                        if (component.status === Component.Ready ) {
                            var page = component.createObject(stackView, viewProperties)
                            stackView.replace(page)
                            view = name
                            found = true
                            break;
                        }
                    }
                if (!found)
                    console.warn("unable to load view " + name)
                return found
            }

        }
    }

    Connections {
        target: stackView.currentItem
        ignoreUnknownSignals: true

        onActionUp:     sourcesBanner.focus = true
        onActionCancel: sourcesBanner.focus = true

        onActionLeft:   root.actionLeft(index)
        onActionRight:  root.actionRight(index)
        onActionDown:   root.actionDown(index)
    }
}
