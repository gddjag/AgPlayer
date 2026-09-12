import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls as C
import AgPlayer

T.Button {
    id: control

    property bool primary: false
    property bool danger: false
    property bool compact: false
    property bool prominent: false
    property bool loading: false
    property bool available: true
    property int labelPixelSize: Theme.fontSizeBody

    enabled: available && !loading
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.fontPrimary
    font.pixelSize: labelPixelSize
    implicitHeight: compact ? Theme.controlHeightCompact
                            : prominent ? Theme.controlHeightProminent
                                        : Theme.controlHeight
    implicitWidth: Math.max(implicitHeight,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    Accessible.name: text
    Accessible.description: loading ? qsTr("正在处理") : ""
    Accessible.role: Accessible.Button

    contentItem: Item {
        implicitWidth: buttonLabel.implicitWidth
        implicitHeight: Math.max(buttonLabel.implicitHeight,
                                 busyIndicator.implicitHeight)

        C.BusyIndicator {
            id: busyIndicator
            objectName: "themedButtonBusyIndicator"
            width: Theme.fontSizeBody
            height: width
            running: control.loading
            visible: running
            anchors.centerIn: parent
        }

        Text {
            id: buttonLabel
            text: control.text
            color: !control.enabled ? Theme.textDisabled
                   : control.primary || control.danger ? Theme.accentText
                                                       : Theme.textPrimary
            font.family: control.font.family
            font.pixelSize: control.font.pixelSize
            font.weight: control.font.weight !== Font.Normal
                         ? control.font.weight
                         : control.primary ? Font.Medium : Font.Normal
            font.italic: control.font.italic
            font.underline: control.font.underline
            visible: !control.loading
            width: parent.width
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
        }
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
