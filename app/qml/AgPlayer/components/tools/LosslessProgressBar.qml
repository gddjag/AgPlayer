import QtQuick
import AgPlayer

Item {
    id: root
    property real value: 0
    property color fillColor: Theme.accent
    property color trackColor: Theme.losslessGrid
    implicitHeight: 8

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.trackColor
    }

    Rectangle {
        width: parent.width * Math.max(0, Math.min(1, Number(root.value || 0)))
        height: parent.height
        radius: height / 2
        color: root.fillColor
    }
}
