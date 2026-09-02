import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.Button {
    id: control

    property bool primary: false
    property bool danger: false
    property bool compact: false
    property bool prominent: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: compact ? Theme.controlHeightCompact
                            : prominent ? Theme.controlHeightProminent
                                        : Theme.controlHeight
    implicitWidth: Math.max(implicitHeight,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    Accessible.name: text
    Accessible.role: Accessible.Button

    contentItem: Text {
        text: control.text
        color: !control.enabled ? Theme.textDisabled
               : control.primary || control.danger ? Theme.accentText
                                                  : Theme.textPrimary
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
        font.weight: control.primary ? Font.Medium : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled ? Theme.disabled
               : control.danger ? (control.down ? Qt.darker(Theme.danger, 1.15)
                                                 : control.hovered ? Qt.lighter(Theme.danger, 1.08)
                                                                   : Theme.danger)
               : control.primary ? (control.down ? Theme.accentPressed
                                                  : control.hovered ? Theme.accentHover
                                                                    : Theme.accent)
               : control.down ? Theme.surfacePressed
               : control.hovered ? Theme.surfaceHover
                                 : Theme.surfaceElevated
        border.color: control.activeFocus ? Theme.focus : Theme.opaqueBorder
        border.width: control.activeFocus ? 2 : (control.primary || control.danger ? 0 : 1)

        Behavior on color {
            ColorAnimation { duration: 140 }
        }
    }
}
