import QtQuick 2.9
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.3

import org.videolan.vlc 0.1

import "qrc:///style/"
import "qrc:///utils/" as Utils
import "qrc:///playlist/" as PL

Utils.NavigableFocusScope {
    id: root
    focus: true

    Utils.NavigableFocusScope {
        id: playlistpopup
        width: 0
        anchors {
            top: parent.top
            right: parent.right
            bottom: controlbarpopup.top
        }
        focus: false

        Flickable {
            anchors.fill: parent

            Rectangle {
                color: VLCStyle.colors.setColorAlpha(VLCStyle.colors.banner, 0.9)
                width: root.width/4
                height: parent.height

                PL.PlaylistListView {
                    id: playlistView
                    focus: true
                    anchors.fill: parent
                    onActionLeft: playlistpopup.quit()
                    onActionCancel: playlistpopup.quit()
                }
            }
        }

        function quit() {
            state = "hidden"
            controlbarpopup.focus = true
        }

        state: "hidden"
        states: [
            State {
                name: "visible"
                PropertyChanges {
                    target: playlistpopup
                    width: playlistView.width
                    visible: true
                }
            },
            State {
                name: "hidden"
                PropertyChanges {
                    target: playlistpopup
                    height: 0
                    visible: false
                }
            }
        ]
        transitions: [
            Transition {
                to: "hidden"
                SequentialAnimation {
                    NumberAnimation { target: playlistpopup; property: "width"; duration: 200 }
                    PropertyAction{ target: playlistpopup; property: "visible" }
                }
            },
            Transition {
                to: "visible"
                SequentialAnimation {
                    PropertyAction{ target: playlistpopup; property: "visible" }
                    NumberAnimation { target: playlistpopup; property: "width"; duration: 200 }
                }
            }
        ]
    }


    Utils.NavigableFocusScope {
        id: controlbarpopup
        focus: true
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: controlbar.height

        Flickable {
            anchors.fill: parent
            ControlBar {
                id: controlbar
                width: parent.width
                height: 80
                focus: true
                onActionCancel: {
                    controlbarpopup.state = "hidden"
                }
                onShowPlaylist: {
                    if (playlistpopup.state === "visible")
                        playlistpopup.state = "hidden"
                    else
                        playlistpopup.state = "visible"
                    playlistpopup.focus = true
                }
            }
        }

        state: "visible"
        states: [
            State {
                name: "visible"
                PropertyChanges {
                    target: controlbarpopup
                    height: controlbar.height
                    visible: true
                }
            },
            State {
                name: "hidden"
                PropertyChanges {
                    target: controlbarpopup
                    height: 0
                    visible: false
                    focus: false
                }
            }
        ]
        transitions: [
            Transition {
                to: "hidden"
                SequentialAnimation {
                    NumberAnimation { target: controlbarpopup; property: "height"; duration: 200 }
                    PropertyAction{ target: controlbarpopup; property: "visible" }
                }
            },
            Transition {
                to: "visible"
                SequentialAnimation {
                    PropertyAction{ target: controlbarpopup; property: "visible" }
                    NumberAnimation { target: controlbarpopup; property: "height"; duration: 200 }
                }
            }
        ]
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: {
        console.log("root key handle", controlbarpopup.state)
        if (controlbarpopup.state === "visible")
            return;
        controlbarpopup.state = "visible"
        controlbarpopup.focus = true
        event.accepted = true
    }
}
