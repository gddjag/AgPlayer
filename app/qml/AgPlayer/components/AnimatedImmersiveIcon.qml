import QtQuick
import QtQuick.Shapes
import AgPlayer

// Exact vector geometry from the user-supplied 沉浸视觉.svg. The shared
// component is used by the classic, integrated, and rolling player themes.
Item {
    id: root
    objectName: "animatedImmersiveIcon"
    property color color: Theme.iconPrimary
    readonly property real phase: 0
    readonly property bool animating: false
    implicitWidth: 20
    implicitHeight: 20

    Item {
        width: 1024
        height: 1024
        anchors.centerIn: parent
        scale: Math.min(root.width, root.height) / 1024

        Shape {
            objectName: "uploadedImmersivePath1"
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillColor: root.color
                PathSvg { path: "M640 970.666667H384c-118.186667 0-198.272-25.002667-251.946667-78.72S53.333333 758.186667 53.333333 640V384c0-118.186667 25.002667-198.272 78.72-251.946667S265.813333 53.333333 384 53.333333h256c118.186667 0 198.272 25.002667 251.946667 78.72S970.666667 265.813333 970.666667 384v256c0 118.186667-25.002667 198.272-78.72 251.946667S758.186667 970.666667 640 970.666667z m-256-853.333334c-100.096 0-165.802667 19.2-206.72 59.946667S117.333333 283.904 117.333333 384v256c0 100.096 19.072 165.802667 59.946667 206.72S283.904 906.666667 384 906.666667h256c100.096 0 165.802667-19.072 206.72-59.946667S906.666667 740.096 906.666667 640V384c0-100.096-19.072-165.802667-59.946667-206.72S740.096 117.333333 640 117.333333z" }
            }
        }

        Shape {
            objectName: "uploadedImmersivePath2"
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillColor: root.color
                PathSvg { path: "M512 558.08a31.957333 31.957333 0 0 1-16.042667-4.266667l-226.133333-131.029333a32 32 0 0 1-11.648-43.733333 32 32 0 0 1 43.733333-11.648L512 488.96l208.384-120.704a32 32 0 1 1 32.085333 55.466667l-224.426666 130.133333a32 32 0 0 1-16.042667 4.224z" }
            }
        }

        Shape {
            objectName: "uploadedImmersivePath3"
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillColor: root.color
                PathSvg { path: "M512 790.186667a32 32 0 0 1-32-32v-232.533334a32 32 0 0 1 32-32 32 32 0 0 1 32 32v232.533334a32 32 0 0 1-32 32z" }
            }
        }

        Shape {
            objectName: "uploadedImmersivePath4"
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillColor: root.color
                PathSvg { path: "M512.426667 223.829333a144.042667 144.042667 0 0 1 68.266666 16.085334l136.533334 76.032a152.96 152.96 0 0 1 72.96 123.349333v145.066667a153.6 153.6 0 0 1-72.789334 123.733333l-136.533333 75.946667a145.066667 145.066667 0 0 1-68.821333 16.213333 142.890667 142.890667 0 0 1-68.565334-16.213333l-136.490666-75.946667a152.96 152.96 0 0 1-72.746667-123.733333V439.893333a153.6 153.6 0 0 1 72.789333-123.733333l136.362667-75.690667a141.525333 141.525333 0 0 1 69.034667-16.64z m-0.384 512.384a81.621333 81.621333 0 0 0 37.845333-8.192l136.533333-75.861333a89.6 89.6 0 0 0 39.765334-67.797333v-145.066667a88.746667 88.746667 0 0 0-39.68-67.498667l-136.746667-75.904a80.768 80.768 0 0 0-37.333333-8.064 78.208 78.208 0 0 0-37.418667 8.277334h-0.384l-136.533333 75.946666a89.6 89.6 0 0 0-39.850667 67.84v144.469334a88.746667 88.746667 0 0 0 39.68 67.498666l136.533333 76.032a79.744 79.744 0 0 0 37.589334 8.32z" }
            }
        }
    }
}
