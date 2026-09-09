import QtQuick
import QtQuick.Shapes
import AgPlayer

// Shared static eye. Keep the component name and color API for all player shells.
Item {
    id: root
    objectName: "animatedImmersiveIcon"
    property color color: Theme.iconPrimary
    property color eyeColor: Theme.background
    readonly property real phase: 0
    readonly property bool animating: false
    implicitWidth: 22
    implicitHeight: 22

    Item {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: Math.min(root.width, root.height) / 24

        Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: root.color
                strokeWidth: 1.75
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg {
                    path: "M2 12 C4.5 7.5 8 5 12 5 C16 5 19.5 7.5 22 12 C19.5 16.5 16 19 12 19 C8 19 4.5 16.5 2 12 Z"
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: "transparent"
            border.color: root.color
            border.width: 1.75
            antialiasing: true
        }
    }
}
