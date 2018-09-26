import QtQuick 2.9

import "qrc:///style/"

Item {
    id: foot
    property bool _dropVisible: false

    width: parent.width
    height: Math.max(VLCStyle.icon_normal, view.height - y)

    Rectangle {
        width: parent.width
        anchors.top: parent.top
        antialiasing: true
        height: 2
        visible: foot._dropVisible
        color: VLCStyle.colors.accent
    }

    DropArea {
        anchors { fill: parent }
        onEntered: {
            foot._dropVisible = true
            return true
        }
        onExited: foot._dropVisible = false
        onDropped: {
            delegateModel.onDropMovedAtEnd()
            foot._dropVisible = false
        }
    }
}
