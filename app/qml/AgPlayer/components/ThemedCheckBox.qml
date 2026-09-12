import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.CheckBox {
    id: control

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    property int indicatorSize: 18
    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(Theme.controlHeight, contentItem.implicitWidth)

    indicator: Rectangle {
        implicitWidth: control.indicatorSize
        implicitHeight: control.indicatorSize
        x: 0
        y: (control.height - height) / 2
        radius: Theme.radiusXs
        color: control.checked ? (control.enabled ? (control.hovered ? Theme.accentHover : Theme.accent)
                                                   : Theme.disabled)
                               : (control.hovered && control.enabled ? Theme.hoverSurface : "transparent")
        border.color: control.activeFocus ? Theme.focus
                      : control.checked ? (control.enabled ? Theme.accentBorder : Theme.border) : Theme.border
        border.width: control.activeFocus ? 2 : 1

        Text {
            anchors.centerIn: parent
            text: "✓"
            color: control.enabled ? Theme.accentText : Theme.textDisabled
            font.pixelSize: Theme.fontSizeMeta
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.primaryText : Theme.secondaryText
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
