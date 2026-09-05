import QtQuick
import QtQuick.Controls
import AgPlayer

Dialog {
    id: control

    modal: true
    padding: Theme.spacingLg
    font.family: Theme.fontPrimary
    font.pixelSize: Theme.fontSizeBody
    palette.window: Theme.surfaceElevated
    palette.text: Theme.textPrimary
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText

    background: Rectangle {
        color: Theme.surfaceElevated
        radius: Theme.radiusMd
        border.color: Theme.opaqueBorder
        border.width: 1
    }
}
