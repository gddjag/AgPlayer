import QtQuick
import QtQuick.Controls
import AgPlayer

Slider {
    id: control

    property int visibleGrooveThickness: Theme.sliderTrackHeight
    property int thumbDiameter: Theme.sliderHandleExtent
    property int pointerHitExtent: Theme.minimumInteractionExtent
    property color editorAccentColor: Theme.accent
    property color editorGrooveColor: Theme.border
    property color editorThumbColor: Theme.textPrimary
    property real wheelStep: 0
    signal wheelAdjusted(real requestedValue)

    implicitWidth: orientation === Qt.Horizontal ? 120 : pointerHitExtent
    implicitHeight: orientation === Qt.Horizontal ? pointerHitExtent : 120
    leftPadding: orientation === Qt.Horizontal
        ? thumbDiameter / 2 : (pointerHitExtent - visibleGrooveThickness) / 2
    rightPadding: orientation === Qt.Horizontal
        ? thumbDiameter / 2 : (pointerHitExtent - visibleGrooveThickness) / 2
    topPadding: orientation === Qt.Horizontal
        ? (pointerHitExtent - visibleGrooveThickness) / 2 : thumbDiameter / 2
    bottomPadding: orientation === Qt.Horizontal
        ? (pointerHitExtent - visibleGrooveThickness) / 2 : thumbDiameter / 2

    background: Item {
        anchors.fill: parent

        Rectangle {
            objectName: "editorSliderGroove"
            clip: true
            width: control.orientation === Qt.Horizontal
                ? control.availableWidth : control.visibleGrooveThickness
            height: control.orientation === Qt.Horizontal
                ? control.visibleGrooveThickness : control.availableHeight
            x: control.orientation === Qt.Horizontal
                ? control.leftPadding
                : control.leftPadding
                    + (control.availableWidth - width) / 2
            y: control.orientation === Qt.Horizontal
                ? control.topPadding
                    + (control.availableHeight - height) / 2
                : control.topPadding
            radius: control.visibleGrooveThickness / 2
            color: control.editorGrooveColor
            border.color: Theme.borderStrong
            border.width: 1
            Rectangle {
                objectName: "editorSliderActiveSegment"
                width: control.orientation === Qt.Horizontal
                    ? Math.max(0, control.visualPosition * parent.width)
                    : parent.width
                height: control.orientation === Qt.Horizontal
                    ? parent.height
                    : Math.max(0, control.visualPosition * parent.height)
                anchors.bottom: control.orientation === Qt.Vertical
                    ? parent.bottom : undefined
                radius: control.visibleGrooveThickness / 2
                color: control.editorAccentColor
            }
        }
    }

    handle: Rectangle {
        width: control.thumbDiameter
        height: control.thumbDiameter
        x: control.orientation === Qt.Horizontal
            ? control.leftPadding + control.visualPosition
              * (control.availableWidth - width)
            : control.leftPadding + (control.availableWidth - width) / 2
        y: control.orientation === Qt.Horizontal
            ? control.topPadding + (control.availableHeight - height) / 2
            : control.topPadding + (1 - control.visualPosition)
              * (control.availableHeight - height)
        radius: width / 2
        color: control.editorThumbColor
        border.color: Theme.textSecondary
        border.width: 1
    }

    MouseArea {
        objectName: "editorSliderWheelArea"
        anchors.fill: parent
        enabled: control.enabled && control.wheelStep > 0
        acceptedButtons: Qt.NoButton
        preventStealing: false
        onWheel: function(wheel) {
            const direction = wheel.angleDelta.y > 0 ? 1 : -1
            control.wheelAdjusted(Math.max(control.from,
                Math.min(control.to,
                    control.value + direction * control.wheelStep)))
            wheel.accepted = true
        }
    }
}
