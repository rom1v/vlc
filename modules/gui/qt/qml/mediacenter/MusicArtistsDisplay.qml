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

import QtQuick.Controls 2.4
import QtQuick 2.9
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
    onCurrentArtistIndexChanged: {
        if (currentArtistIndex == -1)
            mainView.replace(artistGridComponent)
        else
            mainView.replace(albumComponent)
    }
    property var artistId: null

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

                color: VLCStyle.colors.getBgColor(element.DelegateModel.inSelected, this.hovered, this.activeFocus)

                cover: Image {
                    id: cover_obj
                    fillMode: Image.PreserveAspectFit
                    source: model.cover || VLCStyle.noArtCover
                }
                line1: model.name || qsTr("Unknown artist")

                onItemClicked: {
                    currentArtistIndex = index
                    //albumDisplay.parentId = model.id
                    artistId = model.id
                    artistModel.updateSelection( modifier , artistList.currentIndex, index)
                    artistList.currentIndex = index
                    artistList.forceActiveFocus()
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

                image: VLCStyle.noArtCover
                title: model.name || "Unknown Artist"
                selected: element.DelegateModel.inSelected

                shiftX: ((index % mainView.currentItem._colCount) + 1) *
                        ((mainView.currentItem.width - mainView.currentItem._colCount * mainView.currentItem.cellWidth) / (mainView.currentItem._colCount + 1))

                onItemClicked: {
                    artistModel.updateSelection( modifier , mainView.currentItem.currentIndex, index)
                    mainView.currentItem.currentIndex = index
                    mainView.currentItem.focus = true
                }
                onPlayClicked: {
                    medialib.addAndPlay( model.id )
                }
                onAddToPlaylistClicked: {
                    console.log('Clicked on addToPlaylist : '+model.name);
                    medialib.addToPlaylist( model.id );
                }

                //replace image with a mutlicovers preview
                Utils.MultiCoverPreview {
                    id: multicover
                    visible: false
                    width: VLCStyle.cover_normal
                    height: VLCStyle.cover_normal

                    albums: MLAlbumModel {
                        ml: medialib
                        parentId: model.id
                    }
                }
                Component.onCompleted: {
                    multicover.grabToImage(function(result) {
                        gridItem.image = result.url
                        multicover.destroy()
                    })

                }
            }
        }
    }


    Component {
        id: artistGridComponent
        Utils.KeyNavigableGridView {
            cellWidth: (VLCStyle.cover_normal) + VLCStyle.margin_small
            cellHeight: (VLCStyle.cover_normal + VLCStyle.fontHeight_normal)  + VLCStyle.margin_small

            model: artistModel.parts.grid
            modelCount: artistModel.items.count

            onSelectAll: artistModel.selectAll()
            onSelectionUpdated: artistModel.updateSelection( keyModifiers, oldIndex, newIndex )
            onActionLeft: artistList.focus = true
        }
    }

    Component {
        id: albumComponent
        // Display selected artist albums
        MusicAlbumsDisplay {
            parentId: artistId

            //placehoder header
            header: Item {
                height: VLCStyle.heightBar_xlarge
            }

            //banner will stay above the display MusicAlbumsDisplay
            ArtistTopBanner {
                anchors.left: parent.left
                anchors.right: parent.right
                contentY: parent.contentY
                artist: artistModel.items.get(currentArtistIndex).model
            }

            onActionLeft: artistList.focus = true
        }
    }

    RowLayout {
        anchors.fill: parent
        Utils.KeyNavigableListView {
            Layout.preferredWidth: parent.width * 0.25
            Layout.preferredHeight: parent.height
            Layout.minimumWidth: 250

            id: artistList
            spacing: 2
            model: artistModel.parts.list
            modelCount: artistModel.items.count

            onSelectAll: artistModel.selectAll()
            onSelectionUpdated: artistModel.updateSelection( keyModifiers, oldIndex, newIndex )
            onActionRight: {
                console.log("artists on right")
                //mainView.currentItem.forceActiveFocus()
                mainView.focus = true
            }
        }

        StackView {
            id: mainView
            Layout.preferredWidth: parent.width * 0.75
            Layout.preferredHeight: parent.height

            initialItem: artistGridComponent

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
