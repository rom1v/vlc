import QtQuick 2.9
import QtQuick.Controls 2.2


FocusScope {
    id: root
    property alias header: headerLoader.sourceComponent
    property alias headerItem: headerLoader.item
    property alias body: bodyLoader.sourceComponent
    property alias bodyItem: bodyLoader.item
    property int headerReservedHeight: 0
    property alias contentY: flickable.contentY

    Flickable {
        id: flickable
        anchors.fill : parent

        contentHeight: headerLoader.item.height + bodyLoader.item.contentHeight
        focus: true
        clip: true
        ScrollBar.vertical: ScrollBar {  active: true }

        Loader {
            id : headerLoader
            y: Math.max(0, flickable.contentY - root.headerReservedHeight)
            width: flickable.width
        }

        Loader {
            width: flickable.width
            y: headerLoader.y + headerLoader.height
            height: flickable.height
            id: bodyLoader

            onStatusChanged: {
                if (status != Loader.Ready)
                    return
                item.interactive = false
                focus = true
            }
        }

        //Bi-directionnal binding between bodyLoader.item.contentY and flickable.contentY
        onContentYChanged: {
            var newVal = Math.max(0, flickable.contentY + bodyLoader.item.originY - root.headerReservedHeight )
            if (bodyLoader.item.contentY != newVal)
                bodyLoader.item.contentY = newVal
        }

        Connections {
            target: bodyLoader.item
            onContentYChanged: {
                var newVal = bodyLoader.item.contentY - bodyLoader.item.originY + root.headerReservedHeight
                if (flickable.contentY != newVal)
                    flickable.contentY = newVal
            }
        }
    }
}
