import QtQuick 2.0
import QtQuick.Controls 2.2

import "qrc:///style/"

ProgressBar {
    property int progressPercent: 0
    property bool discoveryDone: true

    Connections {
        target: medialib
        onProgressUpdated: {
            progressPercent = percent;
            if (discoveryDone)
                progressText_id.text = percent + "%";
        }
        onDiscoveryProgress: {
            progressText_id.text = entryPoint;
        }
        onDiscoveryStarted: discoveryDone = false
        onDiscoveryCompleted: discoveryDone = true
    }

    visible: (progressPercent < 100) && (progressPercent != 0)
    id: progressBar_id
    from: 0
    to: 100
    height: progressText_id.height
    anchors.topMargin: 10
    anchors.bottomMargin: 10
    value: progressPercent
    Text {
        id: progressText_id
        color: VLCStyle.textColor
        anchors.horizontalCenter: parent.horizontalCenter
    }
}
