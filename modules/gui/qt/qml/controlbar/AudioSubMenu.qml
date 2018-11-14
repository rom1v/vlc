import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Window 2.11

Menu {
    id: menu

    function popupAbove(item) {
        menu.popup((item.x + item.width / 2) - (menu.width / 2), item.y - (menu.height + 20))
    }

    Menu {
        title: qsTr("video")
        visible: player.videoTracks.rowCount() !== 0
        Repeater {
            model: player.videoTracks
            delegate:  MenuItem {
                action: Action {
                    text: model.display
                    checkable: true
                    checked: model.checked
                    onToggled: model.checked = checked
                }
            }
        }
    }

    Menu {
        title: "subtitle"
        visible: player.subtitleTracks.rowCount() !== 0
        Repeater {
            model: player.subtitleTracks
            delegate:  MenuItem {
                action: Action {
                    text: model.display
                    checkable: true
                    checked: model.checked
                    onToggled: model.checked = checked
                }
            }
        }
    }

    Menu {
        title: qsTr("audio")
        visible: player.audioTracks.rowCount() !== 0
        Repeater {
            model: player.audioTracks
            delegate:  MenuItem {
                action: Action {
                    text: model.display
                    checkable: true
                    checked: model.checked
                    onToggled: model.checked = checked
                }
            }
        }
    }

}
