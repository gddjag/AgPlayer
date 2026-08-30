import QtQuick
import AgPlayer

Item {
    id: root
    objectName: "skinBackdrop"

    property real radius: 0
    clip: radius > 0

    Rectangle {
        id: gradientPaint
        objectName: "skinBackdropGradient"
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.backdropStart }
            GradientStop { position: 0.5; color: Theme.backdropMiddle }
            GradientStop { position: 1.0; color: Theme.backdropEnd }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.glassInnerHighlight }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}
