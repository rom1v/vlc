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
    id: root

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

    property alias model: delegateModel.model
    property alias parentId: delegateModel.parentId
    onParentIdChanged: {
        gridView_id.expandIndex = -1
    }

    function _switchExpandItem(index) {
        if (gridView_id.expandIndex === index)
            gridView_id.expandIndex = -1
        else
            gridView_id.expandIndex = index
    }

    function _gridItemClicked( keys, modifier, index ) {
        _switchExpandItem( index )
        delegateModel.updateSelection( modifier , gridView_id.currentIndex, index)
        gridView_id.currentIndex = index
    }

    property int contentY: 0
    property alias header: gridView_id.header

    Utils.SelectableDelegateModel {
        id: delegateModel
        property alias parentId: albumModelId.parentId

        model: MLAlbumModel {
            id: albumModelId
            ml: medialib
        }

        delegate: Package {
            id: element

            MusicAlbumsDisplayGridItem {
                Package.name: "gridTop"
            }

            MusicAlbumsDisplayGridItem {
                Package.name: "gridBottom"
            }

            Utils.ListItem {
                Package.name: "list"
                width: root.width
                height: VLCStyle.icon_normal

                color: VLCStyle.colors.getBgColor(element.DelegateModel.inSelected, this.hovered, root.activeFocus)

                cover: Image {
                    id: cover_obj
                    fillMode: Image.PreserveAspectFit
                    source: model.cover || VLCStyle.noArtCover
                }
                line1: (model.title || qsTr("Unknown title"))+" ["+model.duration+"]"
                line2: model.main_artist || qsTr("Unknown artist")

                onItemClicked : {
                    console.log('Clicked on details : '+model.title)
                    //switchExpandItem(index)
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

    Utils.ExpandGridView {
        id: gridView_id

        anchors.fill: parent

        visible: medialib.gridView
        enabled: medialib.gridView
        focus: medialib.gridView

        cellWidth: VLCStyle.cover_normal + VLCStyle.margin_small
        cellHeight: VLCStyle.cover_normal + VLCStyle.fontHeight_normal + VLCStyle.margin_small

        header: root.header

        expandDelegate:  Rectangle {
            id: expandDelegateId
            height: VLCStyle.heightBar_xxlarge
            width: root.width
            property int currentId: -1
            property alias model : albumDetail.model

            //this mouse area intecepts mouse events
            MouseArea {
                anchors.fill    : parent
                propagateComposedEvents: false
                onClicked: {}
                onDoubleClicked: {}


                MusicAlbumsGridExpandDelegate {
                    id: albumDetail
                    anchors.fill: parent
                    visible: true
                    model: delegateModel.items.get(gridView_id.expandIndex).model
                }
            }
            Connections {
                target: gridView_id
                onExpandIndexChanged: {
                    if (gridView_id.expandIndex !== -1)
                        expandDelegateId.model = delegateModel.items.get(gridView_id.expandIndex).model
                }
            }
        }

        modelTop: delegateModel.parts.gridTop
        modelBottom: delegateModel.parts.gridBottom
        modelCount: delegateModel.items.count

        onContentYChanged:{
            root.contentY = contentY
        }

        Keys.onReturnPressed: {
            _switchExpandItem(currentIndex)
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

        header: root.header

        spacing: VLCStyle.margin_xxxsmall
        anchors.fill: parent

        model: delegateModel.parts.list
        modelCount: delegateModel.items.count

        onContentYChanged:{
            root.contentY = contentY
        }

        //Keys.onReturnPressed: {
        //    _switchExpandItem(currentIndex)
        //}

        onSelectAll: delegateModel.selectAll()
        onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )
    }
}
