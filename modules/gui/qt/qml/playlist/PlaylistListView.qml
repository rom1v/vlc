/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQml.Models 2.2

import org.videolan.vlc 0.1

import "qrc:///utils/" as Utils
import "qrc:///style/"

Utils.NavigableFocusScope {
    id: root

    property var plmodel: PlaylistListModel {
        playlistId: mainctx.playlist
    }

    //label for DnD
    Utils.DNDLabel {
        id: dragItem
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

        delegate: Package {
            id: element

            PLItem {
                id: plitem
                Package.name: "list"
                width: root.width
                color: VLCStyle.colors.getBgColor(element.DelegateModel.inSelected, plitem.hovered, plitem.activeFocus)

                dragitem: dragItem

                onItemClicked : {
                    view.forceActiveFocus()
                    if (delegateModel.mode == "move") {
                        var selectedIndexes = delegateModel.getSelectedIndexes()
                        var preTarget = index
                        /* move to _above_ the clicked item if move up, but
                         * _below_ the clicked item if move down */
                        if (preTarget > selectedIndexes[0])
                            preTarget++
                        view.currentIndex = selectedIndexes[0]
                        root.plmodel.moveItemsPre(selectedIndexes, preTarget)
                    } else if ( delegateModel.mode == "select" ) {
                    } else {
                        delegateModel.onUpdateIndex( modifier , view.currentIndex, index)
                        view.currentIndex = index
                    }
                }
                onItemDoubleClicked:  delegateModel.onAction(index, true)

                onDropedMovedAt: {
                    if (drop.hasUrls) {
                        delegateModel.onDropUrlAtPos(drop.urls, target)
                    } else {
                        /* on drag&drop, the target is the position _before_
                         * the move is applied */
                        delegateModel.moveSelectionToPreTarget(target)
                    }
                }
            }
        }

        function getSelectedIndexes() {
            var list = []
            for (var i = 0; i < delegateModel.selectedGroup.count; i++ ) {
                list.push(delegateModel.selectedGroup.get(i).itemsIndex)
            }
            return list;
        }

        function moveSelectionToPreTarget(target) {
            var selectedIndexes = getSelectedIndexes()
            view.currentIndex = selectedIndexes[0]
            root.plmodel.moveItemsPre(selectedIndexes, target)
        }

        function moveSelectionToPostTarget(target) {
            var selectedIndexes = getSelectedIndexes()
            view.currentIndex = selectedIndexes[0]
            root.plmodel.moveItemsPost(selectedIndexes, target)
        }

        function onDropMovedAtEnd() {
            moveSelectionToPreTarget(items.count)
        }

        function onDropUrlAtPos(urls, target) {
            var list = []
            for (var i = 0; i < urls.length; i++){
                list.push(urls[i])
            }
            mainPlaylistController.insert(target, list)
        }

        function onDropUrlAtEnd(urls) {
            var list = []
            for (var i = 0; i < urls.length; i++){
                list.push(urls[i])
            }
            mainPlaylistController.append(list)
        }

        function onDelete() {
            var list = []
            for (var i = 0; i < delegateModel.selectedGroup.count; i++ ) {
                list.push(delegateModel.selectedGroup.get(i).itemsIndex)
            }
            root.plmodel.removeItems(list)
        }

        function onPlay() {
            if (delegateModel.selectedGroup.count > 0)
                mainPlaylistController.goTo(delegateModel.selectedGroup.get(0).itemsIndex, true)
        }

        function onAction(index) {
            if (mode === "select")
                updateSelection( Qt.ControlModifier, index, view.currentIndex )
            else //normal
                onPlay()
        }

        function onUpdateIndex( keyModifiers, oldIndex, newIndex )
        {
            if (delegateModel.mode === "select") {
                console.log("update selection select")
            } else if (delegateModel.mode === "move") {
                if (delegateModel.selectedGroup.count === 0)
                    return

                var selectedIndexes = getSelectedIndexes()

                /* always move relative to the first item of the selection */
                var target = selectedIndexes[0];
                if (newIndex > oldIndex) {
                    /* move down */
                    target++
                } else if (newIndex < oldIndex && target > 0) {
                    /* move up */
                    target--
                }

                view.currentIndex = selectedIndexes[0]
                /* the target is the position _after_ the move is applied */
                root.plmodel.moveItemsPost(selectedIndexes, target)
            } else  { //normal
                updateSelection( keyModifiers, oldIndex, newIndex )
            }
        }
    }

    Utils.KeyNavigableListView {
        id: view

        anchors.fill: parent
        focus: true

        model: root.plmodel
        modelCount: root.plmodel.count

        property string mode: "normal"

        footer: PLItemFooter {}

        delegate: PLItem {
            /*
             * implicit variables:
             *  - model: gives access to the values associated to PlaylistListModel roles
             *  - index: the index of this item in the list
             */
            id: plitem
            plmodel: root.plmodel
            width: root.width

            onItemClicked: {
                /* to receive keys events */
                view.forceActiveFocus()
                /* from PLItem signal itemClicked(key, modifier) */
                if (modifier & Qt.ControlModifier) {
                    root.plmodel.toggleSelected(index)
                } else {
                    root.plmodel.setSelection([index])
                }
                console.log("current selection: " + root.plmodel.getSelection())
            }
            onItemDoubleClicked: mainPlaylistController.goTo(index, true)
            color: VLCStyle.colors.getBgColor(model.selected, plitem.hovered, plitem.activeFocus)

            onDragStarting: {
                if (!root.plmodel.isSelected(index)) {
                    /* the dragged item is not in the selection, replace the selection */
                    root.plmodel.setSelection([index])
                }
            }

            onDropedMovedAt: {
                if (drop.hasUrls) {
                    delegateModel.onDropUrlAtPos(drop.urls, target)
                } else {
                    root.plmodel.moveItems(root.plmodel.getSelection(), target)
                }
            }
        }

        onSelectAll: root.plmodel.selectAll()
        //onSelectionUpdated: delegateModel.onUpdateIndex( keyModifiers, oldIndex, newIndex )
        Keys.onDeletePressed: root.plmodel.removeItems(root.plmodel.getSelection())
        onActionAtIndex: delegateModel.onAction(index)
        onActionRight: {
            overlay.state = "normal"
            overlay.focus = true
        }
        onActionLeft: overlay.state = "hidden" //this.onCancel(index, root.actionLeft)
        //onActionCancel: this.onCancel(index, root.actionCancel)
        onActionUp: root.actionUp(index)
        onActionDown: root.actionDown(index)

        function onCancel(index, fct) {
            if (delegateModel.mode === "select" || delegateModel.mode === "move")
            {
                overlay.state = "hidden"
                delegateModel.mode = "normal"
            }
            else
            {
                fct(index)
            }
        }

        function onDropMovedAtEnd() {
            onMoveSelectionAtPos(items.count)
        }

        function onDropUrlAtPos(urls, target) {
            var list = []
            for (var i = 0; i < urls.length; i++){
                list.push(urls[i])
            }
            mainPlaylistController.insert(target, list)
        }

        function onDropUrlAtEnd(urls) {
            var list = []
            for (var i = 0; i < urls.length; i++){
                list.push(urls[i])
            }
            mainPlaylistController.append(list)
        }


        Connections {
            target: root.plmodel
            onCurrentIndexChanged: {
                var plIndex = root.plmodel.currentIndex
                if (view.currentIndex === -1 && plIndex >= 0) {
                    delegateModel.items.get(plIndex).inSelected = true
                    view.currentIndex = plIndex
                }
            }
        }
        Connections {
            target: delegateModel.items
            onCountChanged: {
                if (view.currentIndex === -1 && delegateModel.items.count > 0) {
                    delegateModel.items.get(0).inSelected = true
                    view.currentIndex = 0
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: delegateModel.items.count === 0
        font.pixelSize: VLCStyle.fontHeight_xxlarge
        color: root.activeFocus ? VLCStyle.colors.accent : VLCStyle.colors.text
        text: qsTr("playlist is empty")
    }

    Keys.priority: Keys.AfterItem
    Keys.forwardTo: view
    Keys.onPressed: defaultKeyAction(event, 0)
}
