import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.RangeSlider {
    id: control
    implicitHeight: 28
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth; height: 4; radius: 2
        color: control.enabled ? Theme.border : Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.55)
        Rectangle {
            x: control.first.visualPosition * parent.width
            width: (control.second.visualPosition - control.first.visualPosition) * parent.width
            height: parent.height; radius: parent.radius
            color: control.enabled ? Theme.accent : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
        }
    }
    first.handle: Rectangle {
        x: control.leftPadding + control.first.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 14; height: 14; radius: 7
        color: !control.enabled ? Theme.border
              : (control.first.pressed || control.hovered ? Theme.hoverSurface : Theme.panel)
        border.color: control.activeFocus ? Theme.accent : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
    second.handle: Rectangle {
        x: control.leftPadding + control.second.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 14; height: 14; radius: 7
        color: !control.enabled ? Theme.border
              : (control.second.pressed || control.hovered ? Theme.hoverSurface : Theme.panel)
        border.color: control.activeFocus ? Theme.accent : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
}
