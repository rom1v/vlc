import QtQuick 2.7
import QtQuick.Controls 2.2

/*
 * This class is designed to be inherited, It provide basic key handling to navigate between view
 * classes that inherits this class should provide something like
 * Keys.onPressed {
 *   //custom key handling
 *   defaultKeyAction(event, index)
 * }
 */
FocusScope {
    signal actionUp( int index )
    signal actionDown( int index )
    signal actionLeft( int index )
    signal actionRight( int index )
    signal actionCancel( int index )

    function defaultKeyAction(event, index) {
        if (event.accepted)
            return
        if ( event.key === Qt.Key_Down || event.matches(StandardKey.MoveToNextLine) ||event.matches(StandardKey.SelectNextLine) ) {
            actionDown( index )
            event.accepted = true
        } else if ( event.key === Qt.Key_Up || event.matches(StandardKey.MoveToPreviousLine) ||event.matches(StandardKey.SelectPreviousLine) ) {
            actionUp( index  )
            event.accepted = true
        } else if (event.key === Qt.Key_Right || event.matches(StandardKey.MoveToNextChar) ) {
            actionRight( index )
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.matches(StandardKey.MoveToPreviousChar) ) {
            actionLeft( index )
            event.accepted = true
        } else if ( event.matches(StandardKey.Back) || event.matches(StandardKey.Cancel)) {
            actionCancel( index )
            event.accepted = true
        }
    }
}
