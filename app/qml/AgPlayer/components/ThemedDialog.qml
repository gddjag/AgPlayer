import QtQuick
import QtQuick.Controls
import AgPlayer

Dialog {
    id: control

    modal: true
    padding: Theme.spacingLg
    palette.window: Theme.surfaceElevated
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.textPrimary

    background: Rectangle {
        color: Theme.surfaceElevated
        radius: Theme.radiusMd
        border.color: Theme.opaqueBorder
        border.width: 1
    }
}
