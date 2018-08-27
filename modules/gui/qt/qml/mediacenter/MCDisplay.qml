/*****************************************************************************
 * MCDisplay.qml : The main component to display the mediacenter
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

import QtQuick 2.7
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import "qrc:///style/"

import org.videolan.medialib 0.1

Rectangle {
    color: VLCStyle.bgColor

    property var tabModel: ListModel {
        ListElement {
            displayText: qsTr("Albums")
            name: "music-albums"
            url: "qrc:///mediacenter/MusicAlbumsDisplay.qml"
        }

        ListElement {
            displayText: qsTr("Artists")
            name: "music-artists"
            url: "qrc:///mediacenter/MusicArtistsDisplay.qml"
        }

        ListElement {
            displayText: qsTr("Genres")
            name: "music-genre"
            url: "qrc:///mediacenter/MusicGenresDisplay.qml"
        }

        ListElement {
            displayText: qsTr("Tracks")
            name: "music-tracks"
            url: "qrc:///mediacenter/MusicTrackListDisplay.qml"
        }
    }

    ColumnLayout {
        anchors.fill : parent

        Rectangle {
            Layout.fillWidth:  true

            color: VLCStyle.bannerColor
            height: bar.height

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right

                TabBar {
                    id: bar

                    Layout.preferredHeight: parent.height
                    background: Rectangle {
                        color: VLCStyle.bannerColor
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
                                color: control.hovered ? VLCStyle.hoverBannerColor : VLCStyle.bannerColor
                            }
                            contentItem: Label {
                                text: control.text
                                font: control.font
                                color:  control.checked ?
                                            VLCStyle.textColor_activeSource :
                                            (control.hovered ?  VLCStyle.textColor_activeSource : VLCStyle.textColor)
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: Text.AlignHCenter
                            }
                            onClicked: {
                                stackView.replace(model.url)
                                stackView.focus = true
                            }
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

                    color: VLCStyle.buttonTextColor
                    placeholderText: qsTr("filter")

                    background: Rectangle {
                        radius: 5 //fixme
                        color: VLCStyle.buttonColor
                        border.color: searchBox.text.length < 3 && searchBox.text.length !== 0
                                      ? VLCStyle.alertColor
                                      : VLCStyle.buttonBorderColor
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

            Component.onCompleted: push("qrc:///mediacenter/MusicAlbumsDisplay.qml")

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
