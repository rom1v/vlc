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
import QtQuick.Controls 1.4 as QC14
import QtQuick.Controls 2.2

import "qrc:///style/"

import "qrc:///mediacenter/" as MC

Rectangle {
    color: VLCStyle.bgColor

    ColumnLayout {
        id: column
        anchors.fill: parent

        Layout.minimumWidth: VLCStyle.minWidthMediacenter
        spacing: 0

        /* Source selection*/
        BannerSources {
            id: sourcesBanner

            height: VLCStyle.heightBar_normal
            Layout.preferredHeight: height
            Layout.minimumHeight: height
            Layout.maximumHeight: height
            Layout.fillWidth: true

            need_toggleView_button: true
        }

        /* MediaCenter */
        MC.MCDisplay {
            id: mcDisplay

            Layout.fillHeight: true
            Layout.fillWidth: true
        }
    }

    MC.ScanProgressBar {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
