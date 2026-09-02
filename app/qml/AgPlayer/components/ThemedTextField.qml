import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.TextField {
    id: control

    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    color: control.enabled ? Theme.textPrimary : Theme.textDisabled
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.family: Theme.fontPrimary
    font.pixelSize: Theme.fontSizeBody
    verticalAlignment: TextInput.AlignVCenter
    focusPolicy: Qt.StrongFocus
    Accessible.name: accessibleName.length > 0 ? accessibleName : placeholderText
    Accessible.role: Accessible.EditableText

    property string accessibleName: ""

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled ? Theme.disabled
               : control.hovered ? Theme.surfaceHover : Theme.surfaceElevated
        border.color: control.activeFocus ? Theme.focus : Theme.opaqueBorder
        border.width: control.activeFocus ? 2 : 1

        Behavior on color {
            ColorAnimation { duration: 140 }
        }
    }
}
