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

Utils.NavigableFocusScope {
    id: root
    property alias model: delegateModel.model
    property var sortModel: ListModel {
        ListElement { text: qsTr("Alphabetic");  criteria: "title" }
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
        id: delegateModel
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
                    artistId = model.id
                    delegateModel.updateSelection( modifier , artistList.currentIndex, index)
                    artistList.currentIndex = index
                    artistList.forceActiveFocus()
                }

                onItemDoubleClicked: {
                    delegateModel.actionAtIndex(index)
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

                //shiftX: mainView.currentItem.shiftX(index)

                onItemClicked: {
                    delegateModel.updateSelection( modifier , artistList.currentIndex, index)
                    artistList.currentIndex = index
                    artistList.forceActiveFocus()
                }

                onItemDoubleClicked: {
                    delegateModel.actionAtIndex(index)
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
                        maxItems: 4
                    }
                }
                Component.onCompleted: {
                    multicover.grabToImage(function(result) {
                        gridItem.image = result.url
                        //multicover.destroy()
                    })
                }
            }
        }

        function actionAtIndex(index) {
            console.log("actionAtIndex", index)
            if (delegateModel.selectedGroup.count > 1) {
                var list = []
                for (var i = 0; i < delegateModel.selectedGroup.count; i++)
                    list.push(delegateModel.selectedGroup.get(i).model.id)
                medialib.addAndPlay( list )
            } else if (delegateModel.selectedGroup.count === 1) {
                root.artistId =  delegateModel.selectedGroup.get(0).model.id
                root.currentArtistIndex = index
                artistList.currentIndex = index
            }
        }
    }

    Component {
        id: artistGridComponent
        Utils.KeyNavigableGridView {
            cellWidth: (VLCStyle.cover_normal) + VLCStyle.margin_small
            cellHeight: (VLCStyle.cover_normal + VLCStyle.fontHeight_normal)  + VLCStyle.margin_small

            model: delegateModel.parts.grid
            modelCount: delegateModel.items.count

            onSelectAll: delegateModel.selectAll()
            onActionAtIndex: delegateModel.actionAtIndex(index)
            onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )

            onActionLeft: artistList.focus = true
            onActionRight: root.actionRight(index)
            onActionUp: root.actionUp(index)
            onActionDown: root.actionDown(index)
            onActionCancel: root.actionCancel(index)
        }
    }

    Component {
        id: albumComponent
        // Display selected artist albums
        //fixme this isn't working properly for the moment
        //Utils.BannerView {
        //    id: albumsView
        //    header: ArtistTopBanner {
        //        anchors.left: parent.left
        //        anchors.right: parent.right
        //        contentY: albumsView.contentY
        //        artist: delegateModel.items.get(currentArtistIndex).model
        //    }
        //    headerReservedHeight: VLCStyle.heightBar_large
        //
        //    body: MusicAlbumsDisplay {
        //        focus: true
        //        parentId: artistId
        //        onActionLeft: artistList.focus = true
        //    }
        //}
        FocusScope {
            property alias currentIndex: albumSubView.currentIndex
            ColumnLayout {
                anchors.fill: parent
                ArtistTopBanner {
                    id: artistBanner
                    Layout.fillWidth: true
                    focus: false
                    //contentY: albumsView.contentY
                    contentY: 0
                    artist: delegateModel.items.get(currentArtistIndex).model
                }
                MusicAlbumsDisplay {
                    id: albumSubView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    focus: true
                    parentId: artistId
                    onActionLeft: artistList.focus = true

                    onActionRight: root.actionRight(index)
                    onActionUp: root.actionUp(index)
                    onActionDown: root.actionDown(index)
                    onActionCancel: root.actionCancel(index)
                }
            }
        }
    }

    Row {
        anchors.fill: parent
        Utils.KeyNavigableListView {
            width: parent.width * 0.25
            height: parent.height

            id: artistList
            spacing: 2
            model: delegateModel.parts.list
            modelCount: delegateModel.items.count

            onSelectAll: delegateModel.selectAll()
            onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )
            onActionAtIndex: delegateModel.actionAtIndex(index)

            onActionRight:  mainView.focus = true
            onActionLeft: root.actionLeft(index)
            onActionUp: root.actionUp(index)
            onActionDown: root.actionDown(index)
            onActionCancel: root.actionCancel(index)
        }

        StackView {
            id: mainView
            width: parent.width * 0.75
            height: parent.height
            focus: true

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
