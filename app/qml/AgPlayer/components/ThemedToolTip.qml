import QtQuick
import QtQuick.Controls
import AgPlayer

ToolTip {
    id: control

    delay: 500
    timeout: 5000
    padding: Theme.spacingSm
    contentItem: Text {
        text: control.text
        color: Theme.textPrimary
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeMeta
    }
    background: Rectangle {
        color: Theme.surfaceElevated
        radius: Theme.radiusSm
        border.color: Theme.opaqueBorder
        border.width: 1
    }
}
