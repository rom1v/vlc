import QtQuick 2.9
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.3

import org.videolan.vlc 0.1

import "qrc:///style/"
import "qrc:///utils/" as Utils


Utils.NavigableFocusScope {
    id: root

    signal showPlaylist()

    Keys.priority: Keys.AfterItem
    Keys.onPressed: defaultKeyAction(event, 0)

    Rectangle {
        anchors.fill: parent

        PlaylistControlerModel {
            id: playlistCtrl
            playlistPtr: mainctx.playlist
        }

        color: VLCStyle.colors.setColorAlpha(VLCStyle.colors.banner, 0.9)

        ColumnLayout {
            anchors.fill: parent

            SliderBar {
                id: trackPositionSlider
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.fillWidth: true
                enabled: player.playingState == PlayerControler.PLAYING_STATE_PLAYING || player.playingState == PlayerControler.PLAYING_STATE_PAUSED
                Keys.onDownPressed: buttons.focus = true

            }

            Utils.NavigableFocusScope {
                id: buttons
                Layout.fillHeight: true
                Layout.fillWidth: true

                focus: true

                onActionUp: {
                    if (trackPositionSlider.enabled)
                        trackPositionSlider.focus = true
                    else
                        root.actionUp(index)
                }
                onActionDown: root.actionDown(index)
                onActionLeft: root.actionLeft(index)
                onActionRight: root.actionRight(index)
                onActionCancel: root.actionCancel(index)

                Keys.priority: Keys.AfterItem
                Keys.onPressed: defaultKeyAction(event, 0)

                ToolBar {
                    id: centerbuttons
                    anchors.centerIn: parent

                    focusPolicy: Qt.StrongFocus
                    focus: true

                    background: Rectangle {
                        color: "transparent"
                    }

                    Component.onCompleted: {
                        playBtn.focus= true
                    }

                    RowLayout {
                        focus: true
                        anchors.fill: parent
                        Utils.ImageToolButton {
                            width: VLCStyle.icon_small
                            height: VLCStyle.icon_small
                            checked: playlistCtrl.random
                            imageSource: "qrc:///buttons/playlist/shuffle_on.svg"
                            onClicked: playlistCtrl.toggleRandom()
                            KeyNavigation.right: prevBtn
                        }

                        Utils.ImageToolButton {
                            id: prevBtn
                            width: VLCStyle.icon_small
                            height: VLCStyle.icon_small
                            imageSource: "qrc:///toolbar/previous_b.svg"
                            onClicked: playlistCtrl.prev()
                            KeyNavigation.right: playBtn
                        }

                        Utils.ImageToolButton {
                            id: playBtn
                            width: VLCStyle.icon_small
                            height: VLCStyle.icon_small
                            imageSource: player.playingState === PlayerControler.PLAYING_STATE_PLAYING
                                         ? "qrc:///toolbar/pause_b.svg"
                                         : "qrc:///toolbar/play_b.svg"
                            onClicked: playlistCtrl.togglePlayPause()
                            focus: true
                            KeyNavigation.right: nextBtn
                        }

                        Utils.ImageToolButton {
                            id: nextBtn
                            width: VLCStyle.icon_small
                            height: VLCStyle.icon_small
                            imageSource: "qrc:///toolbar/next_b.svg"
                            onClicked: playlistCtrl.next()
                            KeyNavigation.right: randomBtn
                        }

                        Utils.ImageToolButton {
                            id: randomBtn
                            width: VLCStyle.icon_small
                            height: VLCStyle.icon_small

                            checked: playlistCtrl.repeatMode === PlaylistControlerModel.PLAYBACK_REPEAT_NONE
                            imageSource: (playlistCtrl.repeatMode == PlaylistControlerModel.PLAYBACK_REPEAT_CURRENT)
                                         ? "qrc:///buttons/playlist/repeat_one.svg"
                                         : "qrc:///buttons/playlist/repeat_all.svg"
                            onClicked: playlistCtrl.toggleRepeatMode()
                            KeyNavigation.right: langBtn
                        }
                    }
                }

                ToolBar {
                    id: rightButtons
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter

                    focusPolicy: Qt.StrongFocus
                    background: Rectangle {
                        color: "transparent"
                    }
                    Component.onCompleted: {
                        rightButtons.contentItem.focus= true
                    }

                    RowLayout {
                        anchors.fill: parent
                        Utils.ImageToolButton {
                            AudioSubMenu {
                                id: audioSubMenu
                                onClosed: parent.forceActiveFocus()
                            }


                            id: langBtn
                            width: VLCStyle.icon_small
                            height: VLCStyle.icon_small
                            checked: playlistCtrl.random
                            imageSource: "qrc:///toolbar/audiosub.svg"
                            onClicked: audioSubMenu.popupAbove(this)
                            KeyNavigation.right: playlistBtn
                        }

                        Utils.ImageToolButton {
                            id: playlistBtn
                            width: VLCStyle.icon_small
                            height: VLCStyle.icon_small
                            imageSource: "qrc:///toolbar/playlist.svg"
                            onClicked: root.showPlaylist()
                            KeyNavigation.right: fullscreenBtn
                        }


                        Utils.ImageToolButton {
                            id: fullscreenBtn
                            width: VLCStyle.icon_small
                            height: VLCStyle.icon_small
                            imageSource: "qrc:///toolbar/fullscreen.svg"
                            onClicked: player.toggleFullscreen()
                        }
                    }
                }
            }
        }
    }

}
