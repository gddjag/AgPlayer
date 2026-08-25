import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.CheckBox {
    id: control

    implicitHeight: 32
    implicitWidth: Math.max(32, contentItem.implicitWidth)

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: 0
        y: (control.height - height) / 2
        radius: 4
        color: control.checked ? (control.enabled ? (control.hovered ? Theme.accent : Theme.cyan)
                                                   : Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.45))
                               : (control.hovered && control.enabled ? Theme.hoverSurface : "transparent")
        border.color: control.activeFocus ? Theme.accent
                      : control.checked ? (control.enabled ? Theme.cyan : Theme.border) : Theme.border
        border.width: control.activeFocus ? 2 : 1

        Text {
            anchors.centerIn: parent
            text: "✓"
            color: control.enabled ? Theme.accentText : Theme.secondaryText
            font.pixelSize: 12
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.primaryText : Theme.secondaryText
        font.family: Theme.fontPrimary
        font.pixelSize: 12
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
