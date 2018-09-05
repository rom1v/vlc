/*****************************************************************************
 * MCMusicDisplay.qml : The music component of the mediacenter
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
import "qrc:///style/"

import org.videolan.medialib 0.1

Rectangle {
    id: root
    color: VLCStyle.colors.bg

    //name and properties of the tab to be initially loaded
    property string view: "albums"
    property var viewProperties: QtObject {}

    property var tabModel: ListModel {
        ListElement {
            displayText: qsTr("Albums")
            name: "albums"
            url: "qrc:///mediacenter/MusicAlbumsDisplay.qml"
        }

        ListElement {
            displayText: qsTr("Artists")
            name: "artists"
            url: "qrc:///mediacenter/MusicArtistsDisplay.qml"
        }

        ListElement {
            displayText: qsTr("Genres")
            name: "genres"
            url: "qrc:///mediacenter/MusicGenresDisplay.qml"
        }

        ListElement {
            displayText: qsTr("Tracks")
            name: "tracks"
            url: "qrc:///mediacenter/MusicTrackListDisplay.qml"
        }
    }

    function loadView(name, viewProperties)
    {
        var found = false
        for (var tab = 0; tab < tabModel.count; tab++ )
            if (tabModel.get(tab).name === name) {
                //we can't use push(url, properties) as Qt interprets viewProperties
                //as a second component to load
                var component = Qt.createComponent(tabModel.get(tab).url)
                if (component.status === Component.Ready ) {
                    var page = component.createObject(stackView, viewProperties)
                    stackView.push(page)
                    root.view = name
                    found = true
                    break;
                }
            }
        if (!found)
            console.warn("unable to load view " + name)
        return found
    }

    ColumnLayout {
        anchors.fill : parent

        Rectangle {
            Layout.fillWidth:  true

            color: VLCStyle.colors.banner
            height: bar.height

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right

                TabBar {
                    id: bar

                    Layout.preferredHeight: parent.height
                    background: Rectangle {
                        color: VLCStyle.colors.banner
                    }

                    /* List of sub-sources for Music */
                    Repeater {
                        id: model_music_id

                        model: tabModel

                        //Column {
                        TabButton {
                            id: control
                            text: model.displayText
                            background: Rectangle {
                                color: control.hovered ? VLCStyle.colors.bannerHover : VLCStyle.colors.banner
                            }
                            contentItem: Label {
                                text: control.text
                                font: control.font
                                color:  (control.checked || control.hovered) ?
                                            VLCStyle.colors.textActiveSource :
                                            VLCStyle.colors.text
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: Text.AlignHCenter
                            }
                            onClicked: {
                                stackView.replace(model.url)
                                history.push({
                                    view: "music",
                                    viewProperties: {
                                        view: model.name,
                                        viewProperties: {}
                                    },
                                }, History.Stay)
                                stackView.focus = true
                            }
                            checked: (model.name === root.view)

                        }
                    }
                }

                /* Spacer */
                Item {
                    Layout.fillWidth: true
                }

                TextField {
                    Layout.preferredWidth: VLCStyle.widthSearchInput
                    Layout.preferredHeight: parent.height
                    id: searchBox

                    color: VLCStyle.colors.buttonText
                    placeholderText: qsTr("filter")

                    background: Rectangle {
                        radius: 5 //fixme
                        color: VLCStyle.colors.button
                        border.color: searchBox.text.length < 3 && searchBox.text.length !== 0
                                      ? VLCStyle.colors.alert
                                      : VLCStyle.colors.buttonBorder
                    }

                    onTextChanged: {
                        stackView.currentItem.model.searchPattern = text;
                    }
                }

                /* Selector to choose a specific sorting operation */
                ComboBox {
                    id: combo

                    //Layout.fillHeight: true
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                    Layout.preferredWidth: VLCStyle.widthSortBox
                    height: parent.height
                    textRole: "text"
                    model: stackView.currentItem.sortModel
                    onCurrentIndexChanged: {
                        var sorting = model.get(currentIndex);
                        stackView.currentItem.model.sortByColumn(sorting.criteria, sorting.desc)
                    }
                }
            }
        }


        /* The data elements */
        StackView  {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true

            Component.onCompleted: {
                var found = loadView(view, viewProperties)
                if (!found)
                    push(tabModel.get(0).url)
            }

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
}
