import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.RangeSlider {
    id: control
    implicitHeight: 28
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth; height: 3; radius: 1.5
        color: control.enabled ? Theme.controlSubtleBorder : Theme.disabled
        Rectangle {
            x: control.first.visualPosition * parent.width
            width: (control.second.visualPosition - control.first.visualPosition) * parent.width
            height: parent.height; radius: parent.radius
            color: control.enabled ? Theme.accent : Theme.disabled
        }
    }
    first.handle: Rectangle {
        x: control.leftPadding + control.first.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 12; height: 12; radius: 6
        color: !control.enabled ? Theme.border
              : (control.first.pressed || control.hovered
                 ? Theme.surfaceHover : "#FFFFFF")
        border.color: control.activeFocus ? Theme.focus : Theme.controlSubtleBorder
        border.width: control.activeFocus ? 2 : 1
        Rectangle {
            z: -1
            x: -1; y: 1
            width: parent.width + 2; height: parent.height + 2
            radius: width / 2
            color: Qt.rgba(0, 0, 0, 0.16)
        }
    }
    second.handle: Rectangle {
        x: control.leftPadding + control.second.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 12; height: 12; radius: 6
        color: !control.enabled ? Theme.border
              : (control.second.pressed || control.hovered
                 ? Theme.surfaceHover : "#FFFFFF")
        border.color: control.activeFocus ? Theme.focus : Theme.controlSubtleBorder
        border.width: control.activeFocus ? 2 : 1
        Rectangle {
            z: -1
            x: -1; y: 1
            width: parent.width + 2; height: parent.height + 2
            radius: width / 2
            color: Qt.rgba(0, 0, 0, 0.16)
        }
    }
}
