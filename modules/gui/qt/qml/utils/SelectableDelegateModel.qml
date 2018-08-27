import QtQuick 2.7
import QtQml.Models 2.2

DelegateModel {
    id: delegateModel

    property int shiftIndex: -1

    groups: [
        DelegateModelGroup { id: selectedGroup; name: "selected"; includeByDefault: false
            onChanged: {
                console.log("select group changed")
            }
        }
    ]

    function _addRange(from, to) {
        for (var i = from; i <= to; i++) {
            console.log("add[", i, "] ", delegateModel.items.get(i))
            delegateModel.items.get(i).inSelected = true
        }
    }
    function _delRange(from, to) {
        for (var i = from; i <= to; i++) {
            console.log("del[", i, "] ", delegateModel.items.get(i))
            delegateModel.items.get(i).inSelected = false
        }
    }

    function selectAll() {
        delegateModel.items.addGroups(0, delegateModel.items.count, ["selected"])
    }

    function updateSelection( keymodifiers, oldIndex, newIndex ) {
        if ((keymodifiers & Qt.ShiftModifier)) {
            if ( shiftIndex === oldIndex) {
                if ( newIndex > shiftIndex )
                    _addRange(shiftIndex, newIndex)
                else
                    _addRange(newIndex, shiftIndex)
            } else if (shiftIndex <= newIndex && newIndex < oldIndex) {
                _delRange(newIndex + 1, oldIndex )
            } else if ( shiftIndex < oldIndex && oldIndex < newIndex ) {
                _addRange(oldIndex, newIndex)
            } else if ( newIndex < shiftIndex && shiftIndex < oldIndex ) {
                _delRange(shiftIndex, oldIndex)
                _addRange(newIndex, shiftIndex)
            } else if ( newIndex < oldIndex && oldIndex < shiftIndex  ) {
                _addRange(newIndex, oldIndex)
            } else if ( oldIndex <= shiftIndex && shiftIndex < newIndex ) {
                _delRange(oldIndex, shiftIndex)
                _addRange(shiftIndex, newIndex)
            } else if ( oldIndex < newIndex && newIndex <= shiftIndex  ) {
                _delRange(oldIndex, newIndex - 1)
            }
        } else {
            var e = delegateModel.items.get(newIndex)
            if (e.inSelected) {
                if ((keymodifiers & Qt.ControlModifier) == Qt.ControlModifier)
                    e.inSelected = false
                else
                    selectedGroup.remove(0,selectedGroup.count) //clear
            } else {
                if ((keymodifiers & Qt.ControlModifier) == Qt.ControlModifier) {
                    e.inSelected = true
                } else {
                    selectedGroup.remove(0,selectedGroup.count) //clear
                    e.inSelected = true
                }
            }
            shiftIndex = newIndex
        }
    }
}
