/*****************************************************************************
 * MCVideoDisplay.qml : The video component of the mediacenter
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

import QtQuick 2.0
import QtQuick.Controls 2.0
import QtQml.Models 2.2

import org.videolan.medialib 0.1

import "qrc:///utils/" as Utils
import "qrc:///style/"

Utils.NavigableFocusScope {
    id: root

    property alias model: delegateModel.model
    property string mrl

    Utils.SelectableDelegateModel {
        id: delegateModel
        model: networkModelFactory.create(mainctx, mrl)

        delegate: Package {
            id: element
            Column {
                Package.name: "grid"
                Utils.GridItem {
                    noActionButtons: model.type == MLNetworkModel.TYPE_FILE
                    image: model.type == MLNetworkModel.TYPE_SHARE ?
                                "qrc:///type/network.svg" : (model.type == MLNetworkModel.TYPE_DIR ?
                                    "qrc:///type/directory.svg" : "qrc:///type/file-asym.svg")
                    title: model.name || qsTr("Unknown share")
                    selected: element.DelegateModel.inSelected || view.currentItem.currentIndex === index
                    shiftX: view.currentItem.shiftX(model.index)

                    onItemDoubleClicked: {
                        if ( model.type != MLNetworkModel.TYPE_FILE ) {
                            history.push({
                                view: "network",
                                viewProperties: {
                                    model: networkModelFactory.create(mainctx, model.mrl)
                                 },
                            }, History.Go)
                        } else {
                            medialib.addAndPlay( model.mrl )
                        }
                    }
                    onPlayClicked: {
                        medialib.addAndPlay( model.mrl )
                    }
                    onAddToPlaylistClicked: {
                        medialib.addToPlaylist( model.mrl );
                    }
                }
                CheckBox {
                    visible: model.can_index
                    text: "Indexed"
                    checked: model.indexed
                    onCheckedChanged: model.indexed = checked;
                }
            }

            Utils.ListItem {
                Package.name: "list"
                width: root.width
                height: VLCStyle.icon_normal

                color: VLCStyle.colors.getBgColor(element.DelegateModel.inSelected, this.hovered, this.activeFocus)

                cover: Image {
                    id: cover_obj
                    fillMode: Image.PreserveAspectFit
                    source: model.type == MLNetworkModel.TYPE_SHARE ?
                                "qrc:///type/network.svg" : (model.type == MLNetworkModel.TYPE_DIR ?
                                    "qrc:///type/directory.svg" : "qrc:///type/file-asym.svg")
                }
                line1: model.name || qsTr("Unknown share")

                onItemClicked : {
                    delegateModel.updateSelection( modifier, view.currentItem.currentIndex, index )
                    view.currentItem.currentIndex = index
                    this.forceActiveFocus()
                }
                onItemDoubleClicked: {
                    if ( model.type != MLNetworkModel.TYPE_FILE ) {
                        history.push({
                            view: "network",
                            viewProperties: {
                                model: networkModelFactory.create(mainctx, model.mrl)
                             },
                        }, History.Go)
                    } else {
                        medialib.addAndPlay( model.mrl )
                    }
                }
                onPlayClicked: {
                    medialib.addAndPlay( model.mrl )
                }
                onAddToPlaylistClicked: {
                    medialib.addToPlaylist( model.mrl );
                }
            }
        }
        function actionAtIndex(index) {
            if ( delegateModel.selectedGroup.count > 1 ) {
                var list = []
                for (var i = 0; i < delegateModel.selectedGroup.count; i++) {
                    var itemModel = delegateModel.selectedGroup.get(i).model;
                    if (itemModel.type == MLNetworkModel.TYPE_FILE)
                        list.push(itemModel.mrl)
                }
                medialib.addAndPlay( list )
            } else if (delegateModel.selectedGroup.count === 1) {
                var itemModel = delegateModel.selectedGroup.get(0).model;
                if (itemModel.type != MLNetworkModel.TYPE_FILE) {
                    history.push({
                        view: "network",
                        viewProperties: {
                            model: networkModelFactory.create(mainctx, itemModel.mrl)
                         },
                    }, History.Go);
                } else {
                    medialib.addAndPlay( itemModel.mrl );
                }
            }
        }
    }
    Component {
        id: gridComponent

        Utils.KeyNavigableGridView {
            id: gridView_id

            model: delegateModel.parts.grid
            modelCount: delegateModel.items.count

            focus: true

            cellWidth: VLCStyle.cover_normal + VLCStyle.margin_small
            cellHeight: VLCStyle.cover_normal + VLCStyle.fontHeight_normal + VLCStyle.margin_small

            onSelectAll: delegateModel.selectAll()
            onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )
            onActionAtIndex: delegateModel.actionAtIndex(index)

            onActionLeft: root.actionLeft(index)
            onActionRight: root.actionRight(index)
            onActionDown: root.actionDown(index)
            onActionUp: root.actionUp(index)
            onActionCancel: root.actionCancel(index)
        }
    }

    Component {
        id: listComponent
        /* ListView */
        Utils.KeyNavigableListView {
            id: listView_id

            model: delegateModel.parts.list
            modelCount: delegateModel.items.count

            focus: true
            spacing: VLCStyle.margin_xxxsmall

            onSelectAll: delegateModel.selectAll()
            onSelectionUpdated: delegateModel.updateSelection( keyModifiers, oldIndex, newIndex )
            onActionAtIndex: delegateModel.actionAtIndex(index)

            onActionLeft: root.actionLeft(index)
            onActionRight: root.actionRight(index)
            onActionDown: root.actionDown(index)
            onActionUp: root.actionUp(index)
            onActionCancel: root.actionCancel(index)
        }
    }

    StackView {
        id: view

        anchors.fill: parent
        focus: true

        initialItem: medialib.gridView ? gridComponent : listComponent

        replaceEnter: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 0
                to:1
                duration: 500
            }
        }

        replaceExit: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 1
                to:0
                duration: 500
            }
        }

        Connections {
            target: medialib
            onGridViewChanged: {
                if (medialib.gridView)
                    view.replace(gridComponent)
                else
                    view.replace(listComponent)
            }
        }
    }
}
