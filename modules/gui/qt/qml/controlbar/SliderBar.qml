import QtQuick 2.9
import QtQuick.Controls 2.2

import "qrc:///style/"

Slider {
    id: control
    anchors.margins: VLCStyle.margin_xxsmall

    value: player.position
    onMoved: player.position = control.position

    height: 5
    implicitHeight: 5

    topPadding: 0
    leftPadding: 0
    bottomPadding: 0
    rightPadding: 0

    stepSize: 0.01

    background: Rectangle {
        width: control.availableWidth
        implicitHeight: control.implicitHeight
        height: implicitHeight
        color: VLCStyle.colors.bg

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            color: control.activeFocus ? VLCStyle.colors.accent : VLCStyle.colors.bgHover
            radius: parent.height
        }
    }

    handle: Rectangle {
        visible: control.activeFocus
        x: (control.visualPosition * control.availableWidth) - width / 2
        y: 2.5 - width / 2
        implicitWidth: VLCStyle.margin_small
        implicitHeight: VLCStyle.margin_small
        radius: VLCStyle.margin_small
        color: VLCStyle.colors.accent
    }
}
