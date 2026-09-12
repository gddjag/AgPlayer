import QtQuick
import QtQuick.Shapes
import AgPlayer

Item {
    id: root
    property real value: 0
    property color ringColor: Theme.accent
    property string valueText: qsTr("%1分").arg(Math.round(Math.max(0, Math.min(100, value))))
    implicitWidth: 72
    implicitHeight: 72

    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            strokeColor: Theme.losslessGrid
            strokeWidth: 6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: Math.max(1, root.width / 2 - 6)
                radiusY: radiusX
                startAngle: -90
                sweepAngle: 360
            }
        }

        ShapePath {
            strokeColor: root.ringColor
            strokeWidth: 6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: Math.max(1, root.width / 2 - 6)
                radiusY: radiusX
                startAngle: -90
                sweepAngle: 360 * Math.max(0, Math.min(100, root.value)) / 100
            }
        }
    }

    Text {
        id: valueLabel
        objectName: "losslessConfidenceValue"
        anchors.centerIn: parent
        text: root.valueText
        color: Theme.textPrimary
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.losslessFontSizeSection
        font.weight: Font.DemiBold
    }
}
