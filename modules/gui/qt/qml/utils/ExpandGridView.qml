/*****************************************************************************
 * ExpandGridView.qml : Item displayed inside a grid view
 ****************************************************************************
 * Copyright (C) 2006-2018 VideoLAN and AUTHORS
 * $Id$
 *
 * Authors: Pierre Lamot <pierre@videolabs.io>
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
import QtQuick 2.9
import QtQuick.Controls 2.2

Flickable {
    id: root

    clip: true
    ScrollBar.vertical: ScrollBar { }

    //disable bound behaviors to avoid visual artifacts around the expand delegate
    boundsBehavior: Flickable.StopAtBounds

    /// expected width and height
    property int cellWidth: 100
    property int cellHeight: 100

    //margin to apply
    property int _marginBottom: cellHeight / 2
    property int _marginTop: cellHeight / 3

    //model to be rendered, model has to be passed twice, as they cannot be shared between views
    property alias modelTop: top.model
    property alias modelBottom: bottom.model

    property alias delegateTop: top.delegate
    property alias delegateBottom: bottom.delegate

    property Component header: undefined
    //property bool _headerActive: header !== undefined
    property int _headerBottomY: header ? headerLoader.height : 0
    property bool _headerVisible:  header && contentY < headerLoader.height

    property int modelCount: 0

    // number of elements per row, for internal computation
    property int _colCount: Math.floor(width / cellWidth)
    property int topContentY: contentY
    property int bottomContentY: contentY + height

    /// the id of the item to be expanded
    property int expandIndex: -1
    property int _oldExpandIndex: -1
    property bool _expandActive: expandIndex !== -1

    //delegate to display the extended item
    property Component expandDelegate: Item{}

    function _rowOfIndex( index ) {
        return Math.ceil( (index + 1) / _colCount) - 1
    }

    onExpandIndexChanged: _updateExpandPosition()
    on_ColCountChanged: _updateExpandPosition()
    function _updateExpandPosition() {
        if (_oldExpandIndex === -1 || _rowOfIndex(_oldExpandIndex) < _rowOfIndex(expandIndex))
            root.contentY = Math.max(0, root.contentY - expandItem.height)
        expandItem.y = cellHeight * (Math.floor(expandIndex / _colCount) + 1) + _headerBottomY
        if ( expandItem.bottomY > root.bottomContentY )
            root.contentY = Math.min(expandItem.bottomY - root.height + _marginBottom, contentHeight - height)
        _oldExpandIndex = expandIndex
    }


    states: [
        State {
            name: "-header-expand"
            when: !header && !_expandActive
            PropertyChanges {
                target: root
                topContentY: contentY
                contentHeight: cellHeight * Math.ceil(modelCount / _colCount)
            }
        },
        State {
            name: "-header+expand"
            when: !header &&_expandActive
            PropertyChanges {
                target: root
                topContentY: contentY
                contentHeight: cellHeight * Math.ceil(modelCount / _colCount) + expandItem.height
            }
        },
        State {
            name: "+header-expand"
            when: header && !_expandActive
            PropertyChanges {
                target: root
                topContentY: contentY - headerLoader.height
                contentHeight: cellHeight * Math.ceil(modelCount / _colCount) + headerLoader.height
            }
        },
        State {
            name: "+header+expand"
            when: header && _expandActive
            PropertyChanges {
                target: root
                topContentY: contentY - headerLoader.height
                contentHeight: cellHeight * Math.ceil(modelCount / _colCount) + expandItem.height + headerLoader.height
            }
        }
    ]

    Loader {
        id: headerLoader
        sourceComponent: root.header
        y: 0
        anchors.left: parent.left
        anchors.right: parent.right
    }

    //Gridview visible above the expanded item
    GridView {
        id: top
        clip: true
        interactive: false

        cellWidth: root.cellWidth
        cellHeight: root.cellHeight

        anchors.left: parent.left
        anchors.right: parent.right

        states: [
            //expand is unactive or below the view
            State {
                name: "visible_noexpand"
                when: !_expandActive || expandItem.y >= root.bottomContentY
                PropertyChanges {
                    target: top
                    y: (!_headerVisible) ? root.contentY : _headerBottomY

                    height: (!_headerVisible) ? root.height : (root.height - (_headerBottomY - root.contentY) )
                    //FIXME: should we add + originY? this seemed to fix some issues but has performance impacts
                    //OriginY, seems to change randomly on grid resize
                    contentY: (!_headerVisible) ? root.topContentY  : 0
                    visible: true
                    enabled: true
                }
            },
            //expand is active and within the view
            State {
                name: "visible_expand"
                when: _expandActive && (expandItem.y >= root.contentY) && (expandItem.y < root.bottomContentY)
                PropertyChanges {
                    target: top
                    y: (!_headerVisible) ? root.contentY : _headerBottomY
                    height: expandItem.y - root.topContentY
                    //FIXME: should we add + originY? this seemed to fix some issues but has performance impacts
                    //OriginY, seems to change randomly on grid resize
                    contentY: (!_headerVisible) ? root.topContentY : 0
                    visible: true
                    enabled: true
                }
            },
            //expand is active and above the view
            State {
                name: "hidden"
                when: _expandActive && (expandItem.y < root.contentY)
                PropertyChanges {
                    target: top
                    visible: false
                    enabled: false
                    height: 1
                    y: 0
                    contentY: 0
                }
            }
        ]
    }

    //Expanded item view
    Loader {
        id: expandItem
        sourceComponent: expandDelegate
        active: _expandActive
        y: 0 //updated by _updateExpandPosition
        property int bottomY: y + height
        anchors.left: parent.left
        anchors.right: parent.right
    }

    //Gridview visible below the expand item
    GridView {
        id: bottom
        clip: true
        interactive: false

        cellWidth: root.cellWidth
        cellHeight: root.cellHeight

        anchors.left: parent.left
        anchors.right: parent.right

        property bool hidden: !_expandActive
                              || (expandItem.bottomY >= root.bottomContentY)
                              || _rowOfIndex(expandIndex) === _rowOfIndex(modelCount - 1)
        states: [
            //expand is visible and above the view
            State {
                name: "visible_noexpand"
                when: !bottom.hidden && (expandItem.bottomY < root.contentY)
                PropertyChanges {
                    target: bottom
                    enabled: true
                    visible: true
                    height: root.height
                    y: root.contentY
                    //FIXME: should we add + originY? this seemed to fix some issues but has performance impacts.
                    //OriginY, seems to change randomly on grid resize
                    contentY: expandItem.y + root.contentY - expandItem.bottomY - _headerBottomY
                }
            },
            //expand is visible and within the view
            State {
                name: "visible_expand"
                when: !bottom.hidden && (expandItem.bottomY > root.contentY) && (expandItem.bottomY < root.bottomContentY)
                PropertyChanges {
                    target: bottom
                    enabled: true
                    visible: true
                    height: Math.min(root.bottomContentY - expandItem.bottomY, cellHeight * ( _rowOfIndex(modelCount - 1) - _rowOfIndex(expandIndex)))
                    y: expandItem.bottomY
                    //FIXME: should we add + originY? this seemed to fix some issues but has performance impacts.
                    //OriginY, seems to change randomly on grid resize
                    contentY: expandItem.y - _headerBottomY
                }
            },
            //expand is inactive or below the view
            State {
                name: "hidden"
                when: bottom.hidden
                PropertyChanges {
                    target: bottom
                    enabled: false
                    visible: false
                    height: 1
                    y: 0
                    contentY: 0
                }
            }
        ]
    }

    //from KeyNavigableGridView
    function _yOfIndex( index ) {
        if (index > (_rowOfIndex( expandIndex ) + 1) * _colCount)
            return _rowOfIndex(currentIndex) * cellHeight + expandItem.height + _headerBottomY
        else
            return _rowOfIndex(currentIndex) * cellHeight + _headerBottomY
    }

    //index of the item currently selected on keyboard
    property int currentIndex: 0
    onCurrentIndexChanged: {
        if ( _yOfIndex(currentIndex) + cellHeight > root.bottomContentY)
            root.contentY = Math.min(_yOfIndex(currentIndex) + cellHeight - root.height + _marginBottom, contentHeight - height)
        else if (_yOfIndex(currentIndex)  < root.contentY)
            root.contentY = Math.max(_yOfIndex(currentIndex) - _marginTop, 0)
    }
    //signals emitted when selected items is updated from keyboard
    signal selectionUpdated( int keyModifiers, int oldIndex,int newIndex )
    signal selectAll()

    Keys.onPressed: {
        var newIndex = -1
        if (event.key === Qt.Key_Right)
            newIndex = Math.min(modelCount - 1, currentIndex + 1)
        else if (event.key === Qt.Key_Left)
            newIndex = Math.max(0, currentIndex - 1)
        else if (event.key === Qt.Key_Down)
            newIndex = Math.min(modelCount - 1, currentIndex + _colCount)
        else if (event.key === Qt.Key_PageDown)
            newIndex = Math.min(modelCount - 1, currentIndex + _colCount * 5)
        else if (event.key === Qt.Key_Up)
            newIndex = Math.max(0, currentIndex - _colCount)
        else if (event.key === Qt.Key_PageUp)
            newIndex = Math.max(0, currentIndex - _colCount * 5)
        else if (event.key === Qt.Key_Home)
            newIndex = currentIndex - currentIndex % _colCount
        else if (event.key === Qt.Key_End)
            newIndex = Math.min(modelCount - 1, (currentIndex - currentIndex % _colCount) + _colCount - 1)
        else if (event.key === Qt.Key_A && (event.modifiers &  Qt.ControlModifier) == Qt.ControlModifier ) {
            selectAll()
            event.accepted = true
        }

        if (newIndex != -1) {
            selectionUpdated(event.modifiers, currentIndex, newIndex)
            currentIndex = newIndex
            event.accepted = true
        }
    }
}
