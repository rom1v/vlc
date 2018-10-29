import QtQuick 2.7
import "qrc:///style/"

Rectangle {
    property alias text: label.text

    z: 1
    width:  label.implicitWidth
    height: label.implicitHeight
    color: VLCStyle.colors.button
    border.color : VLCStyle.colors.buttonBorder
    visible: false

    Drag.active: visible

    function updatePos(x, y) {
        var pos = root.mapFromGlobal(x, y)
        dragItem.x = pos.x + 10
        dragItem.y = pos.y + 10
    }

    Text {
        id: label
        font.pixelSize: VLCStyle.fontSize_normal
        color: VLCStyle.colors.text
        text: qsTr("%1 tracks selected").arg(delegateModel.selectedGroup.count)
    }
}
