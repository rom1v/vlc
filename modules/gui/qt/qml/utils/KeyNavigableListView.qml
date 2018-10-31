import QtQuick 2.7
import QtQuick.Controls 2.2

ListView {
    id: listview_id

    property int modelCount: 0

    signal selectionUpdated( int keyModifiers, int oldIndex,int newIndex )
    signal selectAll()
    signal actionLeft( int index )
    signal actionRight( int index )
    signal actionAtIndex( int index )
    signal actionCancel( int index )


    //key navigation is reimplemented for item selection
    keyNavigationEnabled: false

    clip: true
    ScrollBar.vertical: ScrollBar { id: scroll_id }

    Keys.onPressed: {
        var newIndex = -1
        if ( event.key === Qt.Key_Down || event.matches(StandardKey.MoveToNextLine) ||event.matches(StandardKey.SelectNextLine) )
            newIndex = Math.min(modelCount - 1, currentIndex + 1)
        else if ( event.key === Qt.Key_PageDown || event.matches(StandardKey.MoveToNextPage) ||event.matches(StandardKey.SelectNextPage))
            newIndex = Math.min(modelCount - 1, currentIndex + 10)
        else if ( event.key === Qt.Key_Up || event.matches(StandardKey.MoveToPreviousLine) ||event.matches(StandardKey.SelectPreviousLine) )
            newIndex = Math.max(0, currentIndex - 1)
        else if ( event.key === Qt.Key_PageUp || event.matches(StandardKey.MoveToPreviousPage) ||event.matches(StandardKey.SelectPreviousPage))
            newIndex = Math.max(0, currentIndex - 10)
        else if (event.matches(StandardKey.SelectAll)) {
            selectAll()
            event.accepted = true
        } else if (event.key === Qt.Key_Right || event.matches(StandardKey.MoveToNextChar) ) {
            actionRight(currentIndex)
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.matches(StandardKey.MoveToPreviousChar) ) {
            actionLeft(currentIndex)
            event.accepted = true
        } else if (event.key === Qt.Key_Space || event.matches(StandardKey.InsertParagraphSeparator)) { //enter/return/space
            actionAtIndex(currentIndex)
            event.accepted = true
        } else if ( event.matches(StandardKey.Back) || event.matches(StandardKey.Cancel)) {
            actionCancel(currentIndex)
            event.accepted = true
        }

        if (newIndex != -1) {
            var oldIndex = currentIndex
            currentIndex = newIndex
            selectionUpdated(event.modifiers, oldIndex, newIndex)
            event.accepted = true
        }
    }
}
