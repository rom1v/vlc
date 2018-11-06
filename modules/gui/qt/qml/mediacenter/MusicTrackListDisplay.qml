import QtQuick 2.9
import QtQuick.Controls 2.4
import QtQml.Models 2.2
import QtQuick.Layouts 1.3

import org.videolan.medialib 0.1

import "qrc:///utils/" as Utils
import "qrc:///style/"

Utils.KeyNavigableTableView {
    id: root

    sortModel: ListModel {
        ListElement{ criteria: "track_number";width:0.15; text: qsTr("TRACK NB"); showSection: "" }
        ListElement{ criteria: "disc_number"; width:0.15; text: qsTr("DISC NB");  showSection: "" }
        ListElement{ criteria: "title";       width:0.15; text: qsTr("TITLE");    showSection: "title" }
        ListElement{ criteria: "main_artist"; width:0.15; text: qsTr("ARTIST");   showSection: "main_artist" }
        ListElement{ criteria: "album_title"; width:0.15; text: qsTr("ALBUM");    showSection: "album_title" }
        ListElement{ criteria: "duration";    width:0.15; text: qsTr("DURATION"); showSection: "" }
    }

    model: MLAlbumTrackModel {
        id: rootmodel
        ml: medialib
    }

    property alias parentId: rootmodel.parentId

    onActionForSelection: {
        var list = []
        for (var i = 0; i < selection.count; i++ ) {
            list.push(selection.get(i).model.id)
        }
        medialib.addAndPlay(list)
    }
}
