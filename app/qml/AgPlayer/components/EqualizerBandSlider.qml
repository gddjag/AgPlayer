import QtQuick
import QtQuick.Controls
import AgPlayer

Item {
    id: root
    objectName: preamp ? "equalizerPreampSlider" : "eqBandSlider-" + bandIndex
    required property int bandIndex
    required property string frequencyLabel
    required property real gainDb
    property bool preamp: false
    property bool spacious: false
    property string accessibleLabel: frequencyLabel
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
        objectName: root.objectName + "-frequency"
        x: 0
        y: 8
        width: parent.width
        height: 30
        text: root.frequencyLabel
        color: "#EFF3F7"
        font.family: "Microsoft YaHei UI"
        font.pixelSize: 18
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
        objectName: root.objectName + "-control"
        x: 5
        y: 49
        width: parent.width - 10
        height: 239
        property real value: root.gainDb
        readonly property real visualPosition: (root.gainRangeDb - value)
                                               / (root.gainRangeDb * 2)
        property bool pressed: dragArea.pressed
        activeFocusOnTab: true
        Accessible.role: Accessible.Slider
        Accessible.name: root.accessibleLabel
        Accessible.description: qsTr("增益 %1 dB").arg(value.toFixed(1))

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
                color: index === 6 ? "#9BA4AB" : "#6F777D"
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
            color: "#080C0F"
            border.color: "#263039"
        }

        Rectangle {
            x: Math.round((slider.width - width) / 2)
            y: Math.max(0, slider.visualPosition * slider.height)
            width: 4
            height: Math.max(0, slider.height - y)
            radius: 2
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0; color: "#B15DED" }
                GradientStop { position: 0.52; color: "#496CFF" }
                GradientStop { position: 1; color: "#059EF3" }
            }
        }

        Rectangle {
            id: handleShadow
            x: handle.x - 3
            y: handle.y + 3
            width: handle.width + 6
            height: handle.height + 6
            radius: 13
            color: "#55000000"
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
                GradientStop { position: 0; color: "#FFFFFF" }
                GradientStop { position: 0.48; color: "#F7F4EC" }
                GradientStop { position: 1; color: "#C9CED2" }
            }
            border.color: slider.activeFocus ? Theme.focus : "#8A9298"
            border.width: slider.activeFocus ? 2 : 1

            Rectangle {
                anchors.centerIn: parent
                width: 14
                height: 3
                radius: 1.5
                color: "#067BD6"
            }
        }

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function(event) {
                root.setGain(slider.value + (event.angleDelta.y > 0
                                             ? root.gainStepDb
                                             : -root.gainStepDb))
                event.accepted = true
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
        objectName: root.objectName + "-valueChip"
        x: Math.round((parent.width - width) / 2)
        y: 303
        width: 61
        height: 56
        radius: 10
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: "#1B232A" }
            GradientStop { position: 1; color: "#11171C" }
        }
        border.color: "#3B444B"

        Label {
            objectName: root.objectName + "-value"
            anchors.fill: parent
            anchors.topMargin: 3
            text: (root.gainDb > 0 ? "+" : "")
                  + root.gainDb.toFixed(1) + "\ndB"
            color: "#EFF3F7"
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
