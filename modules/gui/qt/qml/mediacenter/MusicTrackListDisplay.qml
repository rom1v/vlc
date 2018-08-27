import QtQuick 2.0
import QtQuick.Controls 1.4 as QC14
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.1
import org.videolan.medialib 0.1
import "qrc:///utils/" as Utils
import "qrc:///style/"

QC14.TableView
{
    id: root
    sortIndicatorVisible: true
    selectionMode: QC14.SelectionMode.ExtendedSelection

    focus: true

    model: MLAlbumTrackModel {
        id: albumModel
        ml: medialib
        onParentIdChanged: {
            selection.clear()
        }
    }
    property var sortModel: ListModel {
        ListElement { text: qsTr("Alphabetic asc");  criteria: "title"; desc: Qt.AscendingOrder}
        ListElement { text: qsTr("Alphabetic desc"); criteria: "title"; desc: Qt.DescendingOrder }
        ListElement { text: qsTr("Album asc");      criteria: "album_title"; desc: Qt.AscendingOrder }
        ListElement { text: qsTr("Album desc");     criteria: "album_title"; desc: Qt.DescendingOrder }
        ListElement { text: qsTr("Artist asc");      criteria: "main_artist"; desc: Qt.AscendingOrder }
        ListElement { text: qsTr("Artist desc");     criteria: "main_artist"; desc: Qt.DescendingOrder }
        ListElement { text: qsTr("Date asc");        criteria: "release_year"; desc: Qt.AscendingOrder }
        ListElement { text: qsTr("Date desc");       criteria: "release_year"; desc: Qt.DescendingOrder}
        ListElement { text: qsTr("Duration asc");    criteria: "duration"; desc: Qt.AscendingOrder}
        ListElement { text: qsTr("Duration desc");   criteria: "duration"; desc: Qt.DescendingOrder }
        ListElement { text: qsTr("Track number asc");  criteria: "track_number"; desc: Qt.AscendingOrder}
        ListElement { text: qsTr("Track number desc"); criteria: "track_number"; desc: Qt.DescendingOrder }
    }

    property alias parentId: albumModel.parentId

    property var columnModel: ListModel {
        ListElement{ role: "track_number"; visible: true;  title: qsTr("TRACK NB"); showSection: "" }
        ListElement{ role: "disc_number";  visible: false;  title:qsTr("DISC NB");  showSection: "" }
        ListElement{ role: "title";        visible: true;  title: qsTr("TITLE");    showSection: "title" }
        ListElement{ role: "main_artist";  visible: true;  title: qsTr("ARTIST");   showSection: "main_artist" }
        ListElement{ role: "album_title";  visible: false; title: qsTr("ALBUM");    showSection: "album_title" }
        ListElement{ role: "duration";     visible: true;  title: qsTr("DURATION"); showSection: "" }
    }
    Component {
        id: tablecolumn_model
        QC14.TableViewColumn {
        }
    }

    Component.onCompleted: {
        for( var i=0; i < columnModel.count; i++ )
        {
            if (columnModel.get(i).visible) {
                var col = addColumn(tablecolumn_model)
                col.role = columnModel.get(i).role
                col.title = columnModel.get(i).title
            }
        }
    }


    Menu {
        id: headerMenu
        onActiveFocusChanged: {
            if (!activeFocus)
                close()
        }

        Repeater {
            model: columnModel
            MenuItem {
                text: model.title
                checkable: true
                checked: model.visible

                onTriggered: {
                    model.visible = checked
                    if (checked)  {
                        var col = addColumn(tablecolumn_model)
                        col.role = model.role
                        col.title = model.title
                    } else {
                        for (var i= 0; i <  columnCount; i++ ) {
                            if ( getColumn(i).role === model.role ) {
                                removeColumn(i)
                                break;
                            }
                        }
                    }
                }
            }
        }
    }


    itemDelegate: Text {
        text: styleData.value
        elide: Text.ElideRight
        font.pixelSize: VLCStyle.fontSize_normal
        color: VLCStyle.textColor
    }


    headerDelegate: Rectangle {
        height: textItem.implicitHeight * 1.2
        color: VLCStyle.buttonColor

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton | Qt.LeftButton | Qt.MiddleButton
            onClicked: {
                //FIXME Qt 5.10 introduce popup
                headerMenu.open(mouse.x, mouse.y)
                var pos  = mapToItem(root, mouse.x, mouse.y)
                headerMenu.x = pos.x
                headerMenu.y = pos.y
            }
        }

        Text {
            id: textItem
            text: styleData.value
            elide: Text.ElideRight
            font {
                bold: true
                pixelSize: VLCStyle.fontSize_normal
            }

            anchors {
                fill: parent
                leftMargin: VLCStyle.margin_xxsmall
                rightMargin: VLCStyle.margin_xxsmall
            }
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignLeft

            color: VLCStyle.buttonTextColor
        }

        Text {
            anchors {
                right: parent.right
                leftMargin: VLCStyle.margin_xxsmall
                rightMargin: VLCStyle.margin_xxsmall
            }
            visible: styleData.column === sortIndicatorColumn
            text: sortIndicatorOrder === Qt.AscendingOrder ? "⯆" : "⯅"
            color: VLCStyle.vlc_orange
        }
        //right handle
        Rectangle {
            color: VLCStyle.buttonBorderColor
            height: parent.height * 0.8
            width: 1
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }
        //line below
        Rectangle {
            color: VLCStyle.buttonBorderColor
            height: 1
            width: parent.width
            anchors.bottom: parent.bottom
        }
    }

    onSortIndicatorColumnChanged: {
        model.sortByColumn(getColumn(sortIndicatorColumn).role, sortIndicatorOrder)
    }
    onSortIndicatorOrderChanged: {
        model.sortByColumn(getColumn(sortIndicatorColumn).role, sortIndicatorOrder)
    }

    section.property : columnModel.get(sortIndicatorColumn).showSection
    section.criteria: ViewSection.FirstCharacter
    section.delegate: Text {
        text: section
        elide: Text.ElideRight
        font.pixelSize: VLCStyle.fontSize_xlarge
        color: VLCStyle.textColor
    }

    onDoubleClicked: {
        medialib.addAndPlay( albumModel.get(row).id )
    }

    Keys.onEnterPressed: {
        selection.forEach(function(rowIndex) {
            medialib.addAndPlay( albumModel.get(rowIndex).id )
        })
    }
}

