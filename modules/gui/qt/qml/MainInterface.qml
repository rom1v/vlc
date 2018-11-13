/*****************************************************************************
 * MainInterface.qml : Main QML component displaying the mediacenter, the
 *     playlist and the sources selection
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
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.2
import org.videolan.medialib 0.1

import "qrc:///utils/" as Utils
import "qrc:///style/"

import "qrc:///mediacenter/" as MC
import "qrc:///playlist/" as PL

Rectangle {
    id: root
    color: VLCStyle.colors.bg

    Row {
        anchors.fill: parent
        MC.MCMainDisplay {
            id: mlview
            width: parent.width * (2. / 3)
            height: parent.height
            focus: true
            onActionRight: playlist.focus = true
        }

        PL.PlaylistListView {
            id: playlist
            width: parent.width / 3
            height: parent.height
            onActionLeft: mlview.focus = true
        }
    }


    MC.ScanProgressBar {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }


    Component.onCompleted: {
        history.push({
            view : "music",
            viewProperties : {
                view : "albums",
                viewProperties : {}
            }
        }, History.Go)
    }
}
