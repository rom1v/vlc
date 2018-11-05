import QtQuick 2.7
import QtQuick.Controls 2.2

GridView {
    id: gridView_id

    clip: true
    ScrollBar.vertical: ScrollBar { }

    //key navigation is reimplemented for item selection
    keyNavigationEnabled: false

    property int _colCount: Math.floor(width / cellWidth)
    property int modelCount: 0
    signal selectionUpdated( int keyModifiers, int oldIndex,int newIndex )
    signal selectAll()
    signal actionLeft( int index )
    signal actionRight( int index )
    signal actionAtIndex( int index )
    signal actionCancel( int index )

    Keys.onPressed: {
        var newIndex = -1
        if (event.key === Qt.Key_Right || event.matches(StandardKey.MoveToNextChar)) {
            if ((currentIndex + 1) % _colCount == 0) {//are we at the end of line
                actionRight(currentIndex)
                event.accepted = true
                return
            }
            newIndex = Math.min(modelCount - 1, currentIndex + 1)
        } else if (event.key === Qt.Key_Left || event.matches(StandardKey.MoveToPreviousChar)) {
            if (currentIndex % _colCount == 0) {//are we at the begining of line
                actionLeft(currentIndex)
                event.accepted = true
                return
            }
            newIndex = Math.max(0, currentIndex - 1)
        } else if (event.key === Qt.Key_Down || event.matches(StandardKey.MoveToNextLine) ||event.matches(StandardKey.SelectNextLine) )
            newIndex = Math.min(modelCount - 1, currentIndex + _colCount)
        else if (event.key === Qt.Key_PageDown || event.matches(StandardKey.MoveToNextPage) ||event.matches(StandardKey.SelectNextPage))
            newIndex = Math.min(modelCount - 1, currentIndex + _colCount * 5)
        else if (event.key === Qt.Key_Up || event.matches(StandardKey.MoveToPreviousLine) ||event.matches(StandardKey.SelectPreviousLine))
            newIndex = Math.max(0, currentIndex - _colCount)
        else if (event.key === Qt.Key_PageUp || event.matches(StandardKey.MoveToPreviousPage) ||event.matches(StandardKey.SelectPreviousPage))
            newIndex = Math.max(0, currentIndex - _colCount * 5)
        else if (event.matches(StandardKey.SelectAll)) {
            selectAll()
            event.accepted = true
        } else if (event.key === Qt.Key_Space || event.matches(StandardKey.InsertParagraphSeparator)) { //enter/return/space
            actionAtIndex(currentIndex)
            event.accepted = true
        } else if ( event.matches(StandardKey.Back) || event.matches(StandardKey.Cancel)) {
            actionCancel(currentIndex)
            event.accepted = true
        }

        if (newIndex != -1 && newIndex != currentIndex) {
            currentIndex = newIndex
            selectionUpdated(event.modifiers, currentIndex, newIndex)
            event.accepted = true
        }
    }
}
