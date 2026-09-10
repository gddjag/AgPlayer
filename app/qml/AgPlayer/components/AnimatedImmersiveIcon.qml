import QtQuick
import QtQuick.Shapes
import AgPlayer

// User-supplied 眼睛_显示.svg geometry, tinted by the shared player theme.
// Keep the component name and color API for all player shells.
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
        width: 1024
        height: 1024
        anchors.centerIn: parent
        scale: Math.min(root.width, root.height) / 1024

        Shape {
            anchors.fill: parent
            ShapePath {
                strokeWidth: -1
                fillColor: root.color
                fillRule: ShapePath.WindingFill
                PathSvg {
                    path: "M512 768c-183.466667 0-328.533333-85.333333-426.666667-256 98.133333-170.666667 243.2-256 426.666667-256s328.533333 85.333333 426.666667 256c-98.133333 170.666667-243.2 256-426.666667 256z m8.533333-426.666667c-128 0-256 55.466667-328.533333 170.666667 72.533333 115.2 200.533333 170.666667 328.533333 170.666667s238.933333-55.466667 311.466667-170.666667c-72.533333-115.2-183.466667-170.666667-311.466667-170.666667z m-8.533333 298.666667c-72.533333 0-128-55.466667-128-128s55.466667-128 128-128 128 55.466667 128 128-55.466667 128-128 128z m0-85.333333c25.6 0 42.666667-17.066667 42.666667-42.666667s-17.066667-42.666667-42.666667-42.666667-42.666667 17.066667-42.666667 42.666667 17.066667 42.666667 42.666667 42.666667z"
                }
            }
        }
    }
}
