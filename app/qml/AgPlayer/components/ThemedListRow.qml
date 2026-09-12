import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.ItemDelegate {
    id: control

    property bool selected: false
    property bool media: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: media ? Theme.mediaListRowHeight : Theme.listRowHeight
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    Accessible.name: text
    Accessible.role: Accessible.ListItem

    contentItem: Text {
        text: control.text
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
        font.weight: control.selected ? Font.Medium : Font.Normal
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled ? Theme.disabled
               : control.down ? Theme.surfacePressed
               : control.selected ? Theme.selectedSurface
               : control.hovered ? Theme.surfaceHover : "transparent"
        border.color: control.activeFocus ? Theme.focus : "transparent"
        border.width: control.activeFocus ? 2 : 0

        Behavior on color {
            ColorAnimation { duration: 140 }
        }
    }
}
