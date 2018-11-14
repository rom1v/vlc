import QtQuick 2.9
import QtQuick.Controls 2.4

import "qrc:///style/"

/* button to choose the view displayed (list or grid) */
ToolButton {
    id: control

    property url imageSource: undefined

    contentItem:  Image {
        source: control.imageSource
        fillMode: Image.PreserveAspectFit
        height: control.width
        width: control.height
        anchors.centerIn: control
    }

    background: Rectangle {
        height: control.width
        width: control.height
        color: "transparent"
        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 2
            visible: control.activeFocus || control.checked
            color: control.activeFocus ? VLCStyle.colors.accent  : VLCStyle.colors.bgHover
        }
    }
}
