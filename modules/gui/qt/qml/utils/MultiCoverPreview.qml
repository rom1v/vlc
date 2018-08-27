/*****************************************************************************
 * ArtistCover.qml : A component to display up to 4 albums covers in a grid layout
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

/********************************************************
 * If 2 or 3 items, some cover will be cropped to fit in
 * not square spaces
 * If 1 album : |---------| If 2 albums : |----|----|
 *              |         |               |    |    |
 *              |         |               |    |    |
 *              |         |               |    |    |
 *              |---------|               |----|----|
 * If 3 albums : |----|----| If 4+ albums : |----|----|
 *               |    |    |                |    |    |
 *               |    |----|                |----|----|
 *               |    |    |                |    |    |
 *               |----|----|                |----|----|
 ********************************************************/

import QtQuick 2.0
import QtQuick.Layouts 1.3
import "qrc:///style"

Item {
    id: root
    property var albums: undefined

    GridLayout {
        id: gridCover_id

        anchors.fill: parent

        columns: 2
        columnSpacing: VLCStyle.margin_xxxsmall
        rowSpacing: VLCStyle.margin_xxxsmall

        Repeater {
            property int count: albums.rowCount()
            model: Math.min(albums.rowCount(), 4)

            /* One cover */
            Image {
                id: img

                Layout.rowSpan: albums.rowCount() >= 1 ? 2 : 1
                Layout.columnSpan: albums.rowCount() === 1 ? 2 : 1
                Layout.fillHeight: true
                Layout.fillWidth: true
                source: albums.get(index).cover || VLCStyle.noArtCover
                fillMode: Image.PreserveAspectCrop
            }
        }
    }

    /* "..." label */
    // If there are more than 4 albums, display "..." to signal there are more
    Text {
        id: moreText

        anchors.right: parent.right
        anchors.bottom: parent.bottom

        text: "..."
        font.pixelSize: 30
        color: VLCStyle.textColor
        style: Text.Outline
        styleColor: VLCStyle.bgColor

        visible: albums.rowCount() >= 4
    }
}
