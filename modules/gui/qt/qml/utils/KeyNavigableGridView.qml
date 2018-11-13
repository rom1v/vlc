import QtQuick 2.7
import QtQuick.Controls 2.2


NavigableFocusScope {
    id: gridview_id

    property int modelCount: 0

    signal selectionUpdated( int keyModifiers, int oldIndex,int newIndex )
    signal selectAll()
    signal actionAtIndex( int index )

    //compute a delta that can be applied to grid elements to obtain an horizontal distribution
    function shiftX( index ) {
        var rightSpace = width - (view._colCount * view.cellWidth)
        return ((index % view._colCount) + 1) * (rightSpace / (view._colCount + 1))
    }

    //forward view properties
    property alias interactive: view.interactive
    property alias model: view.model

    property alias cellWidth: view.cellWidth
    property alias cellHeight: view.cellHeight

    property alias originX: view.originX
    property alias originY: view.originY

    property alias contentX: view.contentX
    property alias contentY:  view.contentY
    property alias contentHeight: view.contentHeight

    property alias footer: view.footer
    property alias footerItem: view.footerItem
    property alias header: view.header
    property alias headerItem: view.headerItem

    property alias currentIndex: view.currentIndex

    GridView {
        id: view

        anchors.fill: parent

        clip: true
        ScrollBar.vertical: ScrollBar { }

        focus: true

        //key navigation is reimplemented for item selection
        keyNavigationEnabled: false

        property int _colCount: Math.floor(width / cellWidth)

        Keys.onPressed: {
            var newIndex = -1
            if (event.key === Qt.Key_Right || event.matches(StandardKey.MoveToNextChar)) {
                if ((currentIndex + 1) % _colCount !== 0) {//are we not at the end of line
                    newIndex = Math.min(gridview_id.modelCount - 1, currentIndex + 1)
                }
            } else if (event.key === Qt.Key_Left || event.matches(StandardKey.MoveToPreviousChar)) {
                if (currentIndex % _colCount !== 0) {//are we not at the begining of line
                    newIndex = Math.max(0, currentIndex - 1)
                }
            } else if (event.key === Qt.Key_Down || event.matches(StandardKey.MoveToNextLine) ||event.matches(StandardKey.SelectNextLine) ) {
                newIndex = Math.min(gridview_id.modelCount - 1, currentIndex + _colCount)
            } else if (event.key === Qt.Key_PageDown || event.matches(StandardKey.MoveToNextPage) ||event.matches(StandardKey.SelectNextPage)) {
                newIndex = Math.min(gridview_id.modelCount - 1, currentIndex + _colCount * 5)
            } else if (event.key === Qt.Key_Up || event.matches(StandardKey.MoveToPreviousLine) ||event.matches(StandardKey.SelectPreviousLine)) {
                newIndex = Math.max(0, currentIndex - _colCount)
            } else if (event.key === Qt.Key_PageUp || event.matches(StandardKey.MoveToPreviousPage) ||event.matches(StandardKey.SelectPreviousPage)) {
                newIndex = Math.max(0, currentIndex - _colCount * 5)
            } else if (event.key === Qt.Key_Space || event.matches(StandardKey.InsertParagraphSeparator)) { //enter/return/space
                actionAtIndex(currentIndex)
                event.accepted = true
            }

            if (newIndex != -1 && newIndex != currentIndex) {
                var oldIndex = currentIndex
                currentIndex = newIndex
                selectionUpdated(event.modifiers, oldIndex, newIndex)
                event.accepted = true
            }

            if (!event.accepted)
                defaultKeyAction(event, currentIndex)
        }
    }

}
