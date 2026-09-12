import QtQuick
import QtQuick.Templates as T
import AgPlayer

T.RangeSlider {
    id: control
    property bool glassStyle: false
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: Theme.controlHeight
    background: Rectangle {
        objectName: "rangeSliderTrack"
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: Theme.sliderTrackHeight
        radius: height / 2
        color: !control.enabled ? Theme.disabled
              : control.glassStyle ? Theme.integratedSoftOutline
                                   : Theme.controlSubtleBorder
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
        width: Theme.sliderHandleExtent
        height: Theme.sliderHandleExtent
        radius: width / 2
        color: !control.enabled ? Theme.border
              : control.glassStyle
                ? Theme.integratedSliderHandle
              : (control.first.pressed || control.hovered
                 ? Theme.surfaceHover : Theme.controlHandle)
        border.color: control.activeFocus ? Theme.focus
                      : control.glassStyle ? Theme.integratedSoftOutline
                                           : Theme.controlSubtleBorder
        border.width: control.activeFocus ? 2 : 1
        Rectangle {
            z: -1
            x: -1; y: 1
            width: parent.width + 2; height: parent.height + 2
            radius: width / 2
            color: Theme.controlHandleShadow
        }
    }
    second.handle: Rectangle {
        objectName: "rangeSliderSecondHandle"
        x: control.leftPadding + control.second.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: Theme.sliderHandleExtent
        height: Theme.sliderHandleExtent
        radius: width / 2
        color: !control.enabled ? Theme.border
              : control.glassStyle
                ? Theme.integratedSliderHandle
              : (control.second.pressed || control.hovered
                 ? Theme.surfaceHover : Theme.controlHandle)
        border.color: control.activeFocus ? Theme.focus
                      : control.glassStyle ? Theme.integratedSoftOutline
                                           : Theme.controlSubtleBorder
        border.width: control.activeFocus ? 2 : 1
        Rectangle {
            z: -1
            x: -1; y: 1
            width: parent.width + 2; height: parent.height + 2
            radius: width / 2
            color: Theme.controlHandleShadow
        }
    }
}
