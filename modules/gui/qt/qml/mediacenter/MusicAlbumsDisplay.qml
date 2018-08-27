/*****************************************************************************
 * MusicAlbumsDisplay.qml : Component to display when category is "albums"
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
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.1
import QtQml.Models 2.2
import org.videolan.medialib 0.1


import "qrc:///utils/" as Utils
import "qrc:///style/"

Item {
    id: viewLoader

    property alias model: delegateModel.model
    property var sortModel: ListModel {
        ListElement { text: qsTr("Alphabetic asc");  criteria: "title"; desc: Qt.AscendingOrder}
        ListElement { text: qsTr("Alphabetic desc"); criteria: "title"; desc: Qt.DescendingOrder }
        ListElement { text: qsTr("Duration asc");    criteria: "duration"; desc: Qt.AscendingOrder}
        ListElement { text: qsTr("Duration desc");   criteria: "duration"; desc: Qt.DescendingOrder }
        ListElement { text: qsTr("Date asc");        criteria: "release_year"; desc: Qt.AscendingOrder }
        ListElement { text: qsTr("Date desc");       criteria: "release_year"; desc: Qt.DescendingOrder}
        ListElement { text: qsTr("Artist asc");      criteria: "main_artist"; desc: Qt.AscendingOrder }
        ListElement { text: qsTr("Artist desc");     criteria: "main_artist"; desc: Qt.DescendingOrder }
    }

    function switchExpandItem(index) {
        if ( index === footer_overlay.currentId && footer_overlay.state !== "HIDDEN" )
            footer_overlay.state = "HIDDEN"
        else {
            footer_overlay.state = "VISIBLE"
            footer_overlay.model = delegateModel.items.get(index).model
        }
        footer_overlay.currentId = index
    }

    property int contentY: 0

    property Component topBanner: Item {}

    Utils.SelectableDelegateModel {
        id: delegateModel

        model: MLAlbumModel {
            ml: medialib
        }

        delegate: Package {
            id: element

            Utils.GridItem {
                Package.name: "grid"

                width: VLCStyle.cover_normal
                height: VLCStyle.cover_normal + VLCStyle.fontHeight_normal + VLCStyle.margin_xsmall

                color: element.DelegateModel.inSelected ? VLCStyle.hoverBgColor : "transparent"

                cover : Image {
                    source: model.cover || VLCStyle.noArtCover
                }

                name : model.title || "Unknown title"
                date : model.release_year !== "0" ? model.release_year : ""
                infos : model.duration + " - " + model.nb_tracks + " tracks"

                onItemClicked : function(keys, modifier){
                    console.log('Clicked on details : '+model.title)
                    switchExpandItem(index)
                    delegateModel.updateSelection( modifier , gridView_id.currentIndex, index)
                    gridView_id.currentIndex = index
                }
                onPlayClicked: {
                    console.log('Clicked on play : '+model.title);
                    medialib.addAndPlay( model.id )

                }
                onAddToPlaylistClicked : {
                    console.log('Clicked on addToPlaylist : '+model.title);
                    medialib.addToPlaylist( model.id );
                }
            }

            Utils.ListItem {
                Package.name: "list"
                width: parent.width
                height: VLCStyle.icon_normal

                color: element.DelegateModel.inSelected ? VLCStyle.hoverBgColor : "transparent"

                cover: Image {
                    id: cover_obj
                    fillMode: Image.PreserveAspectFit
                    source: model.cover || VLCStyle.noArtCover
                }
                line1: (model.title || qsTr("Unknown title"))+" ["+model.duration+"]"
                line2: model.main_artist || qsTr("Unknown artist")

                onItemClicked : {
                    console.log('Clicked on details : '+model.title)
                    switchExpandItem(index)
                    delegateModel.updateSelection( modifier, listView_id.currentIndex, index )
                    listView_id.currentIndex = index
                }
                onPlayClicked: {
                    console.log('Clicked on play : '+model.title);
                    medialib.addAndPlay( model.id )
                }
                onAddToPlaylistClicked : {
                    console.log('Clicked on addToPlaylist : '+model.title);
                    medialib.addToPlaylist( model.id );
                }
            }
        }
    }

    Utils.KeyNavigableGridView {
        id: gridView_id

        anchors.fill: parent

        visible: medialib.gridView
        enabled: medialib.gridView
        focus: medialib.gridView

        cellWidth: VLCStyle.cover_normal + VLCStyle.margin_small
        cellHeight: VLCStyle.cover_normal + VLCStyle.fontHeight_normal + VLCStyle.margin_small

        header: topBanner
        model: delegateModel.parts.grid
        modelCount: delegateModel.items.count

        onContentYChanged:{
            viewLoader.contentY = contentY
        }

        Keys.onReturnPressed: {
            switchExpandItem(currentIndex)
        }

        onSelectAll: delegateModel.selectAll()
        onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )
    }

/* ListView */
    Utils.KeyNavigableListView {
        id: listView_id

        visible: !medialib.gridView
        focus: !medialib.gridView
        enabled: !medialib.gridView

        spacing: VLCStyle.margin_xxxsmall
        anchors.fill: parent

        header: topBanner
        model: delegateModel.parts.list
        modelCount: delegateModel.items.count

        onContentYChanged:{
            viewLoader.contentY = contentY
        }

        Keys.onReturnPressed: {
            switchExpandItem(currentIndex)
        }

        onSelectAll: delegateModel.selectAll()
        onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )
    }

    Rectangle {
        id: footer_overlay
        height: VLCStyle.heightBar_xlarge
        width: parent.width
        anchors.bottom: parent.bottom
        z: 0
        property alias model: albumview.model
        property int currentId: -1

        //this mouse area intecepts mouse events
        MouseArea {
            anchors.fill    : parent
            propagateComposedEvents: false
            onClicked: {}
            onDoubleClicked: {}

            MusicAlbumsGridExpandDelegate {
                id: albumview
                anchors.fill: parent
                visible: true
            }
        }

        states: [
            State {
                name: "HIDDEN"
                PropertyChanges { target: footer_overlay; height: 0; visible: false }
            },
            State {
                name: "VISIBLE"
                PropertyChanges { target: footer_overlay; height: VLCStyle.heightBar_xxlarge; visible: true}
            }
        ]
        state: "HIDDEN"

        transitions: [
            Transition {
                from: "HIDDEN"
                to: "VISIBLE"
                SequentialAnimation {
                    PropertyAnimation { properties: "visible" }
                    NumberAnimation { properties: "height"; easing.type: Easing.InOutQuad }
                }
            },
            Transition {
                from: "VISIBLE"
                to: "HIDDEN"
                SequentialAnimation {
                    NumberAnimation { properties: "height"; easing.type: Easing.InOutQuad }
                    PropertyAnimation { properties: "visible" }
                }
            }
        ]
    }
}
