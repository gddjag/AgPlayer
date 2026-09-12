import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.RadioButton {
    id: control

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(Theme.controlHeight, contentItem.implicitWidth)
    Accessible.name: text
    Accessible.role: Accessible.RadioButton

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: 0
        y: (control.height - height) / 2
        radius: width / 2
        color: !control.enabled ? Theme.disabled
               : control.down ? Theme.surfacePressed
               : control.hovered ? Theme.surfaceHover : "transparent"
        border.color: control.activeFocus ? Theme.focus
                      : control.checked ? Theme.accent : Theme.border
        border.width: control.activeFocus ? 2 : 1

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: width / 2
            color: control.enabled ? Theme.accent : Theme.textDisabled
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
