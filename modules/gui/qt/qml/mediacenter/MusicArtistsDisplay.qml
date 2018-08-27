/*****************************************************************************
 * MusicAlbumsDisplay.qml : Component to display when category is "artists"
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
import QtQuick.Controls 1.4 as QC14
import QtQml.Models 2.2
import QtQuick.Layouts 1.3

import org.videolan.medialib 0.1

import "qrc:///utils/" as Utils
import "qrc:///style/"

Item {
    id: artistViewLoader
    property alias model: artistModel.model
    property var sortModel: ListModel {
        ListElement { text: qsTr("Alphabetic asc");  criteria: "title"; desc: Qt.AscendingOrder}
        ListElement { text: qsTr("Alphabetic desc"); criteria: "title"; desc: Qt.DescendingOrder }
    }

    property int currentArtistIndex: -1

    MLAlbumModel {
        id: albumModel
        ml: medialib
    }

    Utils.SelectableDelegateModel {
        id: artistModel
        model: MLArtistModel {
            ml: medialib
        }
        delegate: Package {
            id: element
            Utils.ListItem {
                Package.name: "list"
                height: VLCStyle.icon_normal
                width: parent.width

                color: element.DelegateModel.inSelected ? VLCStyle.hoverBgColor : "transparent"

                cover: Image {
                    id: cover_obj
                    fillMode: Image.PreserveAspectFit
                    source: model.cover || VLCStyle.noArtCover
                }
                line1: model.name || qsTr("Unknown artist")

                onItemClicked: {
                    currentArtistIndex = index
                    albumModel.parentId = model.id
                    artistModel.updateSelection( modifier , artistList.currentIndex, index)
                    artistList.currentIndex = index
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

            Utils.GridItem {
                Package.name: "grid"
                id: gridItem
                width: VLCStyle.cover_normal
                height: VLCStyle.cover_normal + VLCStyle.fontHeight_normal

                color: element.DelegateModel.inSelected ? VLCStyle.hoverBgColor : "transparent"

                cover: Utils.MultiCoverPreview {
                    albums: MLAlbumModel {
                        ml: medialib
                        parentId: model.id
                    }
                }
                name: model.name || "Unknown Artist"

                onItemClicked: {
                    console.log('Clicked on details : '+model.name);
                    //currentArtistIndex = index
                    artistModel.updateSelection( modifier , artistGridView.currentIndex, index)
                    artistGridView.currentIndex = index
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

    QC14.SplitView {
        anchors.fill: parent
        Utils.KeyNavigableListView {
            id: artistList
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            Layout.minimumWidth: 250
            spacing: 2
            model: artistModel.parts.list
            modelCount: artistModel.items.count

            onSelectAll: artistModel.selectAll()
            onSelectionUpdated: artistModel.updateSelection( keyModifiers, oldIndex, newIndex )
        }

        StackLayout {
            id: albumStackLayout
            currentIndex: (currentArtistIndex === -1) ? 0 : 1

            Utils.KeyNavigableGridView {
                id: artistGridView

                focus: albumStackLayout.currentIndex === 0

                cellWidth: (VLCStyle.cover_normal) + VLCStyle.margin_small
                cellHeight: (VLCStyle.cover_normal + VLCStyle.fontHeight_normal)  + VLCStyle.margin_small
                Layout.fillHeight: true
                Layout.fillWidth: true

                model: artistModel.parts.grid
                modelCount: artistModel.items.count

                onSelectAll: artistModel.selectAll()
                onSelectionUpdated: artistModel.updateSelection( keyModifiers, oldIndex, newIndex )
            }

            // Display selected artist albums
            MusicAlbumsDisplay {
                id: albumDisplay

                focus: albumStackLayout.currentIndex === 1

                Layout.fillHeight: true
                Layout.fillWidth: true

                model: albumModel

                topBanner: ArtistTopBanner{
                    width: parent.width
                    height: VLCStyle.heightBar_xlarge
                    artist: artistModel.items.get(currentArtistIndex).model
                }

                ArtistTopBanner {
                    z: 2
                    width: parent.width
                    height: VLCStyle.heightBar_large
                    visible: albumDisplay.contentY >= -height
                    artist: artistModel.items.get(currentArtistIndex).model
                }
            }
        }
    }
}
