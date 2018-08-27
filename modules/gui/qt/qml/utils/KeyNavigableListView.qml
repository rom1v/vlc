import QtQuick 2.7
import QtQuick.Controls 2.2

ListView {
    id: listview_id

    property int modelCount: 0

    signal selectionUpdated( int keyModifiers, int oldIndex,int newIndex )
    signal selectAll()

    //key navigation is reimplemented for item selection
    keyNavigationEnabled: false

    clip: true
    ScrollBar.vertical: ScrollBar { id: scroll_id }

    Keys.onPressed: {
        var newIndex = -1
        if (event.key === Qt.Key_Down)
            newIndex = Math.min(modelCount - 1, currentIndex + 1)
        else if (event.key === Qt.Key_PageDown)
            newIndex = Math.min(modelCount - 1, currentIndex + 10)
        else if (event.key === Qt.Key_Up)
            newIndex = Math.max(0, currentIndex - 1)
        else if (event.key === Qt.Key_PageUp)
            newIndex = Math.max(0, currentIndex - 10)
        else if (event.key === Qt.Key_A && (event.modifiers &  Qt.ControlModifier) == Qt.ControlModifier ) {
            selectAll()
            event.accepted = true
        }

        if (newIndex != -1) {
            selectionUpdated(event.modifiers, currentIndex, newIndex)
            currentIndex = newIndex
            event.accepted = true
        }
    }
}
