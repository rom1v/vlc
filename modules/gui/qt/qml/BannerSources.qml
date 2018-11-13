/*****************************************************************************
 * BannerSources.qml : Banner to display sources (Music, Video, Network, ...)
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

import QtQuick 2.9
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.3
import org.videolan.medialib 0.1

import "qrc:///style/"
import "qrc:///utils/" as Utils


Utils.NavigableFocusScope {
    id: root
    height: VLCStyle.icon_normal + VLCStyle.margin_small

    property bool need_toggleView_button: false
    property int selectedIndex: 0
    property alias model: pLBannerSources.model

    Rectangle {
        id: pLBannerSources

        anchors.fill: parent

        // Triggered when the toogleView button is selected
        function toggleView () {
            medialib.gridView = !medialib.gridView
        }

        color: VLCStyle.colors.banner
        property alias model: buttonView.model

        RowLayout {
            anchors.fill: parent

            ToolButton {
                id: history_back

                Layout.preferredHeight: VLCStyle.icon_normal
                Layout.preferredWidth: VLCStyle.icon_normal
                Layout.alignment: Qt.AlignVCenter

                focus: true

                KeyNavigation.right: buttonView

                onClicked: history.pop(History.Go)

                contentItem:  Image {
                    source: "qrc:///toolbar/dvd_prev.svg"
                    fillMode: Image.PreserveAspectFit
                    height: history_back.width
                    width: history_back.height
                    anchors.centerIn: history_back
                }

                background: Rectangle {
                    height: history_back.width
                    width: history_back.height
                    color: (history_back.hovered || history_back.activeFocus) ? VLCStyle.colors.bgHover : VLCStyle.colors.banner
                }
            }

            /* Button for the sources */
            TabBar {
                id: buttonView

                focusPolicy: Qt.StrongFocus
                onActiveFocusChanged: {
                    if (activeFocus)
                        sourcesButtons.children[0].forceActiveFocus()
                }

                Layout.preferredHeight: VLCStyle.icon_normal
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter

                KeyNavigation.left: history_back

                property alias model: sourcesButtons.model
                /* Repeater to display each button */
                Repeater {
                    id: sourcesButtons
                    focus: true

                    TabButton {
                        id: control
                        text: model.displayText

                        //initial focus
                        focusPolicy: Qt.StrongFocus
                        focus: index === 0

                        checkable: true
                        padding: 0
                        onClicked: {
                            checked =  !control.checked;
                            root.selectedIndex = model.index
                        }

                        background: Rectangle {
                            implicitHeight: parent.height
                            width: parent.contentItem.width
                            color: (control.hovered || control.activeFocus) ? VLCStyle.colors.bgHover : VLCStyle.colors.banner
                        }

                        contentItem: Row {
                            Image {
                                id: icon
                                anchors {
                                    verticalCenter: parent.verticalCenter
                                    rightMargin: VLCStyle.margin_xsmall
                                    leftMargin: VLCStyle.margin_small
                                }
                                height: VLCStyle.icon_normal
                                width: VLCStyle.icon_normal
                                source: model.pic
                                fillMode: Image.PreserveAspectFit
                            }

                            Label {
                                text: control.text
                                font: control.font
                                color: control.hovered ?  VLCStyle.colors.textActiveSource : VLCStyle.colors.text
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: Text.AlignHCenter
                                anchors {
                                    verticalCenter: parent.verticalCenter
                                    rightMargin: VLCStyle.margin_xsmall
                                    leftMargin: VLCStyle.margin_small
                                }
                            }
                        }
                    }
                }
            }


            /* button to choose the view displayed (list or grid) */
            ToolButton {
                id: view_selector

                Layout.preferredHeight: VLCStyle.icon_normal
                Layout.preferredWidth: VLCStyle.icon_normal
                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight

                KeyNavigation.left: buttonView

                onClicked: toggleView()

                contentItem:  Image {
                    source: "qrc:///toolbar/tv.svg"
                    fillMode: Image.PreserveAspectFit
                    height: view_selector.width
                    width: view_selector.height
                    anchors.centerIn: view_selector
                }

                background: Rectangle {
                    height: view_selector.width
                    width: view_selector.height
                    color: (view_selector.hovered || view_selector.activeFocus) ? VLCStyle.colors.bgHover : VLCStyle.colors.banner
                }
            }
        }
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: {
        if (!event.accepted)
            defaultKeyAction(event, 0)
    }
}
