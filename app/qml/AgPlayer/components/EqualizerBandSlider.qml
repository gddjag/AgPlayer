import QtQuick
import QtQuick.Controls
import AgPlayer

Item {
    id: root
    objectName: preamp ? "equalizerPreampSlider" : "equalizerBand-" + bandIndex
    required property int bandIndex
    required property string frequencyLabel
    required property real gainDb
    property bool preamp: false
    property bool spacious: false
    property string accessibleLabel: frequencyLabel
    readonly property string controlObjectPrefix: preamp
                                                  ? "equalizerPreampSlider"
                                                  : "eqBandSlider-" + bandIndex
    readonly property bool compact: height < 300
    readonly property real sliderTop: compact ? 32 : 49
    readonly property real sliderHeight: compact ? Math.max(86, height - 92) : 239
    readonly property real gainRangeDb: EqualizerController.gainRangeDb
    readonly property real gainStepDb: EqualizerController.gainStepDb
    implicitWidth: 79
    implicitHeight: 375

    function setGain(value) {
        var range = Math.max(0.1, gainRangeDb)
        var step = Math.max(0.1, gainStepDb)
        var quantized = Math.round(value / step) * step
        var bounded = Math.max(-range, Math.min(range, quantized))
        bounded = Math.round(bounded * 10) / 10
        if (preamp) {
            EqualizerController.preampDb = bounded
        } else {
            EqualizerController.setBandGain(bandIndex, bounded)
            root.gainDb = EqualizerController.bandGain(bandIndex)
        }
    }

    Connections {
        target: EqualizerController
        function onBandGainChanged(index, gainDb) {
            if (!root.preamp && index === root.bandIndex)
                root.gainDb = gainDb
        }
        function onCurrentPresetChanged() {
            if (!root.preamp)
                root.gainDb = EqualizerController.bandGain(root.bandIndex)
        }
    }

    Label {
        id: frequencyLabelItem
        objectName: root.controlObjectPrefix + "-frequency"
        x: 0
        y: root.compact ? 2 : 8
        width: parent.width
        height: 30
        text: root.frequencyLabel
        color: Theme.textPrimary
        font.family: "Microsoft YaHei UI"
        font.pixelSize: root.compact ? 16 : 18
        font.weight: Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            onDoubleClicked: root.setGain(0)
        }
    }

    Item {
        id: slider
        objectName: root.controlObjectPrefix + "-control"
        x: 5
        y: root.sliderTop
        width: parent.width - 10
        height: root.sliderHeight
        property real value: root.gainDb
        property bool lastWheelAccepted: false
        readonly property real visualPosition: (root.gainRangeDb - value)
                                               / (root.gainRangeDb * 2)
        property bool pressed: dragArea.pressed
        activeFocusOnTab: true
        Accessible.role: Accessible.Slider
        Accessible.name: root.accessibleLabel
        Accessible.description: qsTr("增益 %1 dB").arg(value.toFixed(1))

        function applyWheelDelta(angleDeltaY, pixelDeltaY) {
            var verticalDelta = angleDeltaY !== 0 ? angleDeltaY : pixelDeltaY
            lastWheelAccepted = verticalDelta !== 0
            if (!lastWheelAccepted)
                return false
            root.setGain(value + (verticalDelta > 0 ? root.gainStepDb
                                                    : -root.gainStepDb))
            return true
        }

        Keys.onUpPressed: function(event) {
            root.setGain(value + root.gainStepDb
                         * ((event.modifiers & Qt.ShiftModifier) ? 10 : 1))
            event.accepted = true
        }
        Keys.onDownPressed: function(event) {
            root.setGain(value - root.gainStepDb
                         * ((event.modifiers & Qt.ShiftModifier) ? 10 : 1))
            event.accepted = true
        }
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_PageUp) {
                root.setGain(value + root.gainStepDb * 10)
                event.accepted = true
            } else if (event.key === Qt.Key_PageDown) {
                root.setGain(value - root.gainStepDb * 10)
                event.accepted = true
            }
        }

        Repeater {
            model: 13
            Rectangle {
                required property int index
                x: Math.round((slider.width - width) / 2)
                y: Math.round(index * (slider.height - 1) / 12)
                width: index === 6 ? 27 : 19
                height: 1
                color: index === 6 ? Theme.borderStrong
                                   : Theme.opaqueDivider
                opacity: index === 6 ? 0.95 : 0.78
            }
        }

        Rectangle {
            id: railShadow
            x: Math.round((slider.width - width) / 2)
            y: 0
            width: 8
            height: slider.height
            radius: 4
            color: Theme.background
            border.color: Theme.opaqueBorder
        }

        Rectangle {
            x: Math.round((slider.width - width) / 2)
            y: Math.max(0, slider.visualPosition * slider.height)
            width: 4
            height: Math.max(0, slider.height - y)
            radius: 2
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0; color: "#B15DED" } // theme-color-allow: equalizer band visualization
                GradientStop { position: 0.52; color: "#496CFF" } // theme-color-allow: equalizer band visualization
                GradientStop { position: 1; color: "#059EF3" } // theme-color-allow: equalizer band visualization
            }
        }

        Rectangle {
            id: handleShadow
            x: handle.x - 3
            y: handle.y + 3
            width: handle.width + 6
            height: handle.height + 6
            radius: 13
            color: Theme.controlHandleShadow
        }

        Rectangle {
            id: handle
            x: Math.round((slider.width - width) / 2)
            y: Math.max(0, Math.min(slider.height - height,
                                   slider.visualPosition
                                   * (slider.height - height)))
            width: 30
            height: 40
            radius: 10
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0; color: Theme.controlHandle }
                GradientStop { position: 0.48; color: Theme.accentText }
                GradientStop { position: 1; color: Theme.textSecondary }
            }
            border.color: slider.activeFocus ? Theme.focus : Theme.borderStrong
            border.width: slider.activeFocus ? 2 : 1

            Rectangle {
                anchors.centerIn: parent
                width: 14
                height: 3
                radius: 1.5
                color: Theme.highlight
            }
        }

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function(event) {
                event.accepted = slider.applyWheelDelta(event.angleDelta.y,
                                                        event.pixelDelta.y)
            }
        }

        MouseArea {
            id: dragArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeVerCursor

            function updateGain(pointerY) {
                var fraction = Math.max(0, Math.min(height, pointerY)) / height
                root.setGain(root.gainRangeDb
                             - fraction * root.gainRangeDb * 2)
            }

            onPressed: function(mouse) {
                slider.forceActiveFocus()
                updateGain(mouse.y)
            }
            onPositionChanged: function(mouse) {
                if (pressed)
                    updateGain(mouse.y)
            }
            onDoubleClicked: root.setGain(0)
        }
    }

    Rectangle {
        id: valueChip
        objectName: root.controlObjectPrefix + "-valueChip"
        x: Math.round((parent.width - width) / 2)
        y: root.compact ? root.sliderTop + root.sliderHeight + 6 : 303
        width: root.compact ? Math.max(34, parent.width - 4) : 61
        height: root.compact ? 50 : 56
        radius: 10
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: Theme.surfaceElevated }
            GradientStop { position: 1; color: Theme.surface }
        }
        border.color: Theme.opaqueBorder

        Label {
            objectName: root.controlObjectPrefix + "-value"
            anchors.fill: parent
            anchors.topMargin: 3
            text: (root.gainDb > 0 ? "+" : "")
                  + root.gainDb.toFixed(1) + "\ndB"
            color: Theme.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: 16
            lineHeight: 1.15
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            onDoubleClicked: root.setGain(0)
        }
    }
}
