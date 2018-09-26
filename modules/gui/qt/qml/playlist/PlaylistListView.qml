/*****************************************************************************
 * PlaylistListView.qml : List view that can group similar items
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
import QtQml.Models 2.2

import org.videolan.medialib 0.1
import org.videolan.vlc 0.1

import "qrc:///utils/" as Utils
import "qrc:///style/"

FocusScope {
    id: root

    property var plmodel: PlaylistListModel {
        playlistId: mainctx.playlist
    }


    PlaylistControlerModel {
        id: plControler
        playlistPtr: mainctx.playlist
    }

    //label for DnD
    Utils.DNDLabel {
        id: dragItem
        text: qsTr("%1 tracks selected").arg(delegateModel.selectedGroup.count)
    }


    /* popup side menu allowing to perform group action  */
    PlaylistMenu {
        id: overlay

        anchors.verticalCenter: root.verticalCenter
        anchors.right: view.right
        z: 2

        onMenuExit:{
            delegateModel.mode = "normal"
            view.focus = true
        }
        onClear: delegateModel.onDelete()
        onPlay: delegateModel.onPlay()
        onSelectionMode:  {
            delegateModel.mode = selectionMode ? "select" : "normal"
            view.focus = true
        }
        onMoveMode: {
            delegateModel.mode = moveMode ? "move" : "normal"
            view.focus = true
        }
    }

    //model

    Utils.SelectableDelegateModel {
        id: delegateModel
        model: root.plmodel

        property string mode: "normal"
        onModeChanged: console.log("delegate mode changed to", mode)

        delegate: Package {
            id: element

            PLItem {
                id: plitem
                Package.name: "list"
                width: root.width
                color: VLCStyle.colors.getBgColor(element.DelegateModel.inSelected, plitem.hovered, plitem.activeFocus)// index === view.currentIndex)

                dragitem: dragItem

                onItemClicked : {
                    view.forceActiveFocus()
                    if (delegateModel.mode == "move") {
                        delegateModel.onMoveSelectionAtPos(index)
                        view.currentIndex = index
                    } else if ( delegateModel.mode == "select" ) {
                    } else {
                        delegateModel.onUpdateIndex( modifier , view.currentIndex, index)
                        view.currentIndex = index
                    }
                }
                onItemDoubleClicked:  delegateModel.onAction(index, true)

                onDropedMovedAt: {
                    delegateModel.onMoveSelectionAtPos(target)
                }
            }
        }

        function onMoveSelectionAtPos(target) {
            var list = []
            for (var i = 0; i < delegateModel.selectedGroup.count; i++ ) {
                list.push(delegateModel.selectedGroup.get(i).itemsIndex)
            }
            console.log("move", list, "to", target)
            root.plmodel.moveItems(list, target)
        }

        function onDropMovedAtEnd() {
            onMoveSelectionAtPos(items.count)
        }

        function onDelete() {
            var list = []
            for (var i = 0; i < delegateModel.selectedGroup.count; i++ ) {
                list.push(delegateModel.selectedGroup.get(i).itemsIndex)
            }
            console.log("delete", list)
            root.plmodel.removeItems(list)
        }

        function onPlay() {
            if (delegateModel.selectedGroup.count > 0)
                plControler.goTo(delegateModel.selectedGroup.get(0).itemsIndex, true)
        }

        function onAction(index) {
            if (mode === "select")
                updateSelection( Qt.ControlModifier, index, view.currentIndex )
            else //normal
                onPlay()
        }

        function onUpdateIndex( keyModifiers, oldIndex, newIndex )
        {
            console.log("onUpdateIndex", oldIndex, newIndex)
            if (delegateModel.mode === "select") {
                console.log("update selection select")
            } else if (delegateModel.mode === "move") {
                if (delegateModel.selectedGroup.count === 0)
                    return

                var list = []
                for (var i = 0; i < delegateModel.selectedGroup.count; i++ ) {
                    list.push(delegateModel.selectedGroup.get(i).itemsIndex)
                }
                var minIndex= delegateModel.selectedGroup.get(0).itemsIndex
                var maxIndex= delegateModel.selectedGroup.get(delegateModel.selectedGroup.count - 1).itemsIndex

                if (newIndex > oldIndex) {
                    //after the next item
                    newIndex = Math.min(maxIndex + 2, delegateModel.items.count)
                    view.currentIndex = Math.min(maxIndex, delegateModel.items.count)
                } else if (newIndex < oldIndex) {
                    //before the previous item
                    view.currentIndex = Math.max(minIndex, 0)
                    newIndex = Math.max(minIndex - 1, 0)
                }

                console.log("move", list, "to", newIndex)
                root.plmodel.moveItems(list, newIndex)

            } else  { //normal
                console.log("update selection regular")
                updateSelection( keyModifiers, oldIndex, newIndex )
            }
        }
    }

    Utils.KeyNavigableListView {
        id: view

        anchors.fill: parent
        focus: true

        model: delegateModel.parts.list
        modelCount: delegateModel.items.count

        footer: PLItemFooter {}

        onSelectAll: delegateModel.selectAll()
        onSelectionUpdated: delegateModel.onUpdateIndex( keyModifiers, oldIndex, newIndex )
        Keys.onDeletePressed: delegateModel.onDelete()
        onActionAtIndex: delegateModel.onAction(index)
        onActionRight: {
            overlay.state = "normal"
            overlay.focus = true
        }
        onActionCancel: this.onCancel()
        onActionLeft: this.onCancel()

        function onCancel() {
            if (delegateModel.mode === "select" || delegateModel.mode === "move")
            {
                overlay.state = "hidden"
                delegateModel.mode = "normal"
            }
        }
    }

}
