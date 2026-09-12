import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.Switch {
    id: control

    property color checkedColor: Theme.accent
    property real indicatorWidth: 36
    property real indicatorHeight: 20
    property real labelPixelSize: Theme.fontSizeBodyStrong
    property bool indicatorTrailing: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Math.max(indicatorWidth + 6, contentItem.implicitWidth)
    implicitHeight: Math.max(Theme.controlHeight, indicatorHeight + 8)

    indicator: Rectangle {
        implicitWidth: control.indicatorWidth
        implicitHeight: control.indicatorHeight
        x: control.indicatorTrailing ? control.width - width : 0
        y: (control.height - height) / 2
        radius: height / 2
        color: !control.enabled ? Theme.disabled
               : control.checked ? (control.hovered ? Theme.accentHover : control.checkedColor)
               : (control.hovered ? Theme.hoverSurface : Theme.border)
        border.color: control.activeFocus ? Theme.focus : "transparent"
        border.width: control.activeFocus ? 2 : 0

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            x: control.checked ? parent.width - width - 2 : 2
            width: parent.height - 4
            height: width
            radius: width / 2
            color: control.enabled ? Theme.onBrandGradientText : Theme.textDisabled

            Behavior on x {
                NumberAnimation { duration: 120 }
            }
        }
    }

    contentItem: Text {
        text: control.text
        visible: text.length > 0
        color: control.enabled ? Theme.primaryText : Theme.secondaryText
        font.family: Theme.fontPrimary
        font.pixelSize: control.labelPixelSize
        leftPadding: visible && !control.indicatorTrailing
                     ? control.indicator.width + control.spacing : 0
        rightPadding: visible && control.indicatorTrailing
                      ? control.indicator.width + control.spacing : 0
        verticalAlignment: Text.AlignVCenter
    }
}
