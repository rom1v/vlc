/*****************************************************************************
 * MusicGenresDisplay.qml : Component to display when category is "genres"
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

import QtQuick 2.0
import QtQuick.Controls 2.0
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
    }

    Utils.SelectableDelegateModel {
        id: delegateModel
        model: MLGenreModel {
            ml: medialib
        }

        delegate: Package {
            id: element
            Utils.GridItem {
                Package.name: "grid"
                width: VLCStyle.cover_normal
                height: VLCStyle.cover_normal + VLCStyle.fontHeight_normal

                color: element.DelegateModel.inSelected ? VLCStyle.hoverBgColor : "transparent"

                cover: Utils.MultiCoverPreview {
                    albums: MLAlbumModel {
                        ml: medialib
                        parentId: model.id
                    }
                }
                name: model.name || "Unknown genre"

                onItemClicked: {
                    console.log('Clicked on details : '+model.name);
                    delegateModel.updateSelection( modifier , gridView_id.currentIndex, index)
                    gridView_id.currentIndex = index
                }
                onPlayClicked: {
                    console.log('Clicked on play : '+model.name);
                    medialib.addAndPlay( model.id )
                }
                onAddToPlaylistClicked: {
                    console.log('Clicked on addToPlaylist : '+model.name);
                    medialib.addToPlaylist( model.id );
                }
            }

            Utils.ListItem {
                Package.name: "list"
                height: VLCStyle.icon_normal
                width: parent.width

                color: element.DelegateModel.inSelected ? VLCStyle.hoverBgColor : "transparent"

                cover:  Utils.MultiCoverPreview {
                    albums: MLAlbumModel {
                        ml: medialib
                        parentId: model.id
                    }
                }

                line1: (model.name || "Unknown genre")+" - "+model.nb_tracks+" tracks"

                onItemClicked: {
                    console.log("Clicked on : "+model.name);
                    delegateModel.updateSelection( modifier, listView_id.currentIndex, index )
                    listView_id.currentIndex = index
                }
                onPlayClicked: {
                    console.log('Clicked on play : '+model.name);
                    medialib.addAndPlay( model.id )
                }
                onAddToPlaylistClicked: {
                    console.log('Clicked on addToPlaylist : '+model.name);
                    medialib.addToPlaylist( model.id );
                }
            }
        }
    }

    /* Grid View */
    Utils.KeyNavigableGridView {
        id: gridView_id

        model: delegateModel.parts.grid
        modelCount: delegateModel.items.count

        visible: medialib.gridView
        enabled: medialib.gridView
        focus: medialib.gridView

        anchors.fill: parent
        cellWidth: (VLCStyle.cover_normal) + VLCStyle.margin_small
        cellHeight: (VLCStyle.cover_normal + VLCStyle.fontHeight_normal) + VLCStyle.margin_small

        onSelectAll: delegateModel.selectAll()
        onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )
    }

    /* List View */
    Utils.KeyNavigableListView {
        id: listView_id

        model: delegateModel.parts.list
        modelCount: delegateModel.items.count

        visible: !medialib.gridView
        enabled: !medialib.gridView
        focus: !medialib.gridView

        anchors.fill: parent
        spacing: VLCStyle.margin_xxxsmall

        onSelectAll: delegateModel.selectAll()
        onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )
    }
}
