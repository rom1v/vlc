import QtQuick 2.3
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3

import "qrc:///style/"

FocusScope{
    id: root

    signal menuExit()
    signal play()
    signal clear()
    signal selectionMode()
    signal moveMode()

    Keys.onPressed: {

        if (event.matches(StandardKey.MoveToPreviousChar)  //left
            || event.matches(StandardKey.MoveToNextChar) //right
            || event.matches(StandardKey.Back)
            || event.matches(StandardKey.Cancel) //esc
            )
        {
            _exitMenu();
            event.accepted = true
            return;
        }
        switch (event.key)
        {
        case Qt.Key_Yellow:
            break;
        case Qt.Key_Red:
            break;
        case Qt.Key_Blue:
            break;
        case Qt.Key_Green:
            break;
        }
    }
    //Keys.onRightPressed:  _exitMenu()
    //Keys.onLeftPressed:  _exitMenu()
    //Keys.onBackPressed:  _exitMenu()
    //Keys.onCancelPressed: _exitMenu()

    width: VLCStyle.icon_large
    height: VLCStyle.icon_large * 5
    property int _hiddentX: VLCStyle.icon_large

    //property alias state: overlay.state

    function _exitMenu() {
        root.state = "hidden"
        menuExit()
    }

    Item {
        id: overlay
        anchors.fill: parent

        Column {
            anchors.right: parent.right
            spacing: VLCStyle.margin_xsmall

            RoundButton {
                id: playButton

                height: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                width: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                x: root._hiddentX

                KeyNavigation.down: clearButton
                icon.source: "qrc:///toolbar/play_b.svg"
                onClicked: {
                    play()
                    _exitMenu()
                }
                focus: true
                background: Rectangle {
                    radius: parent.radius
                    implicitHeight: parent.width
                    implicitWidth: parent.height
                    color: "palegreen"
                }
            }
            RoundButton {
                id: clearButton

                height: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                width: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                x: root._hiddentX

                KeyNavigation.down: selectButton
                icon.source: "qrc:///toolbar/clear.svg"
                onClicked: {
                    clear()
                    _exitMenu()
                }
                background: Rectangle {
                    radius: parent.radius
                    implicitHeight: parent.width
                    implicitWidth: parent.height
                    color: "pink"
                }
            }
            RoundButton {
                id: selectButton

                height: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                width: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                x: root._hiddentX

                KeyNavigation.down: moveButton
                icon.source: "qrc:///toolbar/playlist.svg"

                checkable: true
                checked: false
                onClicked:  root.state = checked ? "select" : "normal"
                onCheckedChanged: selectionMode(checked)
                background: Rectangle {
                    radius: parent.radius
                    implicitHeight: parent.width
                    implicitWidth: parent.height
                    color: "lightblue"
                }
            }
            RoundButton {
                id: moveButton

                height: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                width: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                x: root._hiddentX

                KeyNavigation.down: backButton
                icon.source: "qrc:///toolbar/space.svg"

                checkable: true
                checked: false
                onClicked:  root.state = checked ? "move" : "normal"
                onCheckedChanged: moveMode(checked)
                background: Rectangle {
                    radius: parent.radius
                    implicitHeight: parent.width
                    implicitWidth: parent.height
                    color: "lightyellow"
                }
            }
            RoundButton {
                id: backButton
                height: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                width: activeFocus ? VLCStyle.icon_normal * 1.3 : VLCStyle.icon_normal
                x: root._hiddentX

                icon.source: "qrc:///menu/exit.svg"

                onClicked:  _exitMenu()

                background: Rectangle {
                    radius: parent.radius
                    implicitHeight: parent.width
                    implicitWidth: parent.height
                    color: "lightgrey"
                }
            }
        }
    }

    onStateChanged: console.log("new overlay state is", state)
    state: "hidden"

    states: [
        State {
            name: "hidden"
            PropertyChanges { target: selectButton; checked: false }
            PropertyChanges { target: moveButton; checked: false }
        },
        State {
            name: "normal"
            PropertyChanges { target: moveButton; checked: false }
            PropertyChanges { target: selectButton; checked: false }
        },
        State {
            name: "select"
            PropertyChanges { target: selectButton; checked: true }
            PropertyChanges { target: moveButton; checked: false }
        },

        State {
            name: "move"
            PropertyChanges { target: selectButton; checked: false }
            PropertyChanges { target: moveButton; checked: true }
        }
    ]

    transitions: [
        Transition {
            from: "hidden"; to: "*"
            ParallelAnimation {
                SequentialAnimation {
                    NumberAnimation { target: playButton; properties: "x"; duration: 200; from: _hiddentX; to: 0 }
                }
                SequentialAnimation {
                    PauseAnimation { duration: 25 }
                    NumberAnimation { target: clearButton; properties: "x"; duration: 200; from: _hiddentX; to: 0 }
                }
                SequentialAnimation {
                    PauseAnimation { duration: 75 }
                    NumberAnimation { target: selectButton; properties: "x"; duration: 200; from: _hiddentX; to: 0 }
                }
                SequentialAnimation {
                    PauseAnimation { duration: 50 }
                    NumberAnimation { target: moveButton; properties: "x"; duration: 200; from: _hiddentX; to: 0 }
                }
                SequentialAnimation {
                    PauseAnimation { duration: 100 }
                    NumberAnimation { target: backButton; properties: "x"; duration: 200; from: _hiddentX; to: 0 }
                }
            }
        },
        Transition {
            from: "*"; to: "hidden"
            ParallelAnimation {
                SequentialAnimation {
                    PauseAnimation { duration: 100 }
                    NumberAnimation { target: playButton; properties: "x"; duration: 200; from: 0; to: _hiddentX }
                }
                SequentialAnimation {
                    PauseAnimation { duration: 75 }
                    NumberAnimation { target: clearButton; properties: "x"; duration: 200; from: 0; to: _hiddentX }
                }
                SequentialAnimation {
                    PauseAnimation { duration: 50 }
                    NumberAnimation { target: selectButton; properties: "x"; duration: 200; from: 0; to: _hiddentX }
                }
                SequentialAnimation {
                    PauseAnimation { duration: 25 }
                    NumberAnimation { target: moveButton; properties: "x"; duration: 200; from: 0; to: _hiddentX }
                }
                SequentialAnimation {
                    NumberAnimation { target: backButton; properties: "x"; duration: 200; from: 0; to: _hiddentX }
                }
            }
        }
    ]
}
