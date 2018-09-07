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
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import org.videolan.medialib 0.1
import "qrc:///style/"

Rectangle {
    id: pLBannerSources

    property bool need_toggleView_button: false
    property int selectedIndex: 0

    height: VLCStyle.icon_normal + VLCStyle.margin_xsmall

    // Triggered when the toogleView button is selected
    function toggleView () {
        medialib.gridView = !medialib.gridView
    }

    color: VLCStyle.colors.banner
    property alias model: buttonView.model
    onActiveFocusChanged: {
        if (activeFocus)
            buttonView.forceActiveFocus()
    }

    RowLayout {
        anchors.fill: parent


        Item {
            width: toolButtons.width
            visible: parent.width > (toolButtons.width * 2 + buttonView.width)
            Layout.alignment: Qt.AlignLeft | Qt.AlignBottom
        }

        /* Button for the sources */
        TabBar {
            id: buttonView

            Layout.alignment: parent.width > (toolButtons.width * 2 + buttonView.width) ? (Qt.AlignHCenter | Qt.AlignBottom ) : (Qt.AlignLeft | Qt.AlignBottom )

            focus: true
            activeFocusOnTab: true

            property alias model: sourcesButtons.model
            /* Repeater to display each button */
            Repeater {
                id: sourcesButtons

                TabButton {
                    id: control
                    text: model.displayText

                    focusPolicy: Qt.StrongFocus

                    checkable: true
                    padding: 0
                    onClicked: {
                        checked =  !control.checked;
                        pLBannerSources.selectedIndex = model.index
                    }

                    background: Rectangle {
                        implicitHeight: parent.height
                        //width: btn_txt.implicitWidth+VLCStyle.margin_small*2
                        color: (control.hovered || control.activeFocus) ? VLCStyle.colors.bannerHover : VLCStyle.colors.banner
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

        ToolBar {
            id: toolButtons

            Layout.alignment: Qt.AlignRight  | Qt.AlignBottom

            activeFocusOnTab: true
            onActiveFocusChanged: {
                console.log("toolbar gained focus")
            }

            RowLayout {
                ToolButton {
                    id: history_back

                    height: VLCStyle.icon_normal
                    width: VLCStyle.icon_normal

                    anchors.verticalCenter: parent.verticalCenter

                    enabled: !history.empty

                    highlighted: activeFocus

                    //focus: true
                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.left: sourcesButtons
                    KeyNavigation.right: colorTheme_selector
                    onActiveFocusChanged: {
                        console.log("history_back gained focus")
                    }


                    Image {
                        source: "qrc:///toolbar/dvd_prev.svg"
                        fillMode: Image.PreserveAspectFit
                        anchors.fill: parent
                    }

                    onClicked: history.pop(History.Go)
                }

                /* button to toogle between night and day mode */
                ToolButton {
                    id: colorTheme_selector

                    height: VLCStyle.icon_normal
                    width: VLCStyle.icon_normal

                    highlighted: activeFocus

                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.left: history_back
                    KeyNavigation.right: view_selector
                    onActiveFocusChanged: {
                        console.log("colorTheme_selector gained focus")
                    }


                    Image {
                        source: "qrc:///prefsmenu/advanced/intf.svg"
                        fillMode: Image.PreserveAspectFit
                        anchors.fill: parent
                    }

                    onClicked: VLCStyle.colors.changeColorTheme()
                }

                /* button to choose the view displayed (list or grid) */
                ToolButton {
                    id: view_selector

                    enabled: need_toggleView_button
                    visible: need_toggleView_button

                    highlighted: activeFocus

                    height: VLCStyle.icon_normal
                    width: VLCStyle.icon_normal

                    focusPolicy: Qt.StrongFocus
                    KeyNavigation.left: colorTheme_selector
                    KeyNavigation.right: sourcesButtons
                    onActiveFocusChanged: {
                        console.log("view_selector gained focus")
                    }


                    Image {
                        source: "qrc:///toolbar/tv.svg"
                        fillMode: Image.PreserveAspectFit
                        anchors.fill: parent
                    }
                    onClicked: toggleView()
                }
            }
        }

    }
}
