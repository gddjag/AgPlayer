import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.Slider {
    id: control

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: Theme.controlHeight

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: Theme.sliderTrackHeight
        radius: height / 2
        color: control.enabled ? Theme.opaqueDivider : Theme.disabled

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled ? Theme.accent : Theme.textDisabled
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        width: Theme.sliderHandleExtent
        height: Theme.sliderHandleExtent
        radius: width / 2
        color: control.enabled ? Theme.surfaceElevated : Theme.disabled
        border.color: control.activeFocus ? Theme.focus
                      : control.pressed || control.hovered ? Theme.accent
                                                         : Theme.opaqueBorder
        border.width: control.activeFocus ? 2 : 1

        Behavior on border.color {
            ColorAnimation { duration: 140 }
        }
    }
}
