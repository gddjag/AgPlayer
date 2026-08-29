import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.RangeSlider {
    id: control
    property bool glassStyle: false
    implicitHeight: 28
    background: Rectangle {
        objectName: "rangeSliderTrack"
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.glassStyle ? 3 : 4
        radius: height / 2
        color: !control.enabled ? Theme.disabled
              : control.glassStyle ? Theme.integratedSoftOutline : Theme.border
        Rectangle {
            x: control.first.visualPosition * parent.width
            width: (control.second.visualPosition - control.first.visualPosition) * parent.width
            height: parent.height; radius: parent.radius
            color: control.enabled ? Theme.accent : Theme.disabled
        }
    }
    first.handle: Rectangle {
        objectName: "rangeSliderFirstHandle"
        x: control.leftPadding + control.first.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 14; height: 14; radius: 7
        color: !control.enabled ? Theme.border
              : control.glassStyle
                ? Theme.integratedSliderHandle
              : (control.first.pressed || control.hovered ? Theme.hoverSurface : Theme.panel)
        border.color: control.activeFocus ? Theme.focus
                      : control.glassStyle ? Theme.integratedSoftOutline
                                           : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
    second.handle: Rectangle {
        objectName: "rangeSliderSecondHandle"
        x: control.leftPadding + control.second.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 14; height: 14; radius: 7
        color: !control.enabled ? Theme.border
              : control.glassStyle
                ? Theme.integratedSliderHandle
              : (control.second.pressed || control.hovered ? Theme.hoverSurface : Theme.panel)
        border.color: control.activeFocus ? Theme.focus
                      : control.glassStyle ? Theme.integratedSoftOutline
                                           : Theme.border
        border.width: control.activeFocus ? 2 : 1
    }
}
