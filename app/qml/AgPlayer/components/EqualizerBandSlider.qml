import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: preamp ? "equalizerPreampSlider"
                        : "eqBandSlider-" + bandIndex
    required property int bandIndex
    required property string frequencyLabel
    required property real gainDb
    property bool preamp: false
    property string accessibleLabel: frequencyLabel
    implicitWidth: 56
    implicitHeight: 220

    function setGain(value) {
        var bounded = Math.max(-12, Math.min(12,
                      Math.round(value * 10) / 10))
        if (preamp)
            EqualizerController.preampDb = bounded
        else
            EqualizerController.setBandGain(bandIndex, bounded)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 3

        Label {
            Layout.fillWidth: true
            text: root.frequencyLabel
            color: Theme.primaryText
            font.pixelSize: Math.max(12, Qt.application.font.pixelSize)
            horizontalAlignment: Text.AlignHCenter
        }

        Slider {
            id: slider
            objectName: root.objectName + "-control"
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: true
            Layout.preferredWidth: 34
            orientation: Qt.Vertical
            from: -12
            to: 12
            stepSize: 0.1
            snapMode: Slider.SnapAlways
            live: true
            Accessible.name: root.accessibleLabel
            Accessible.description: qsTr("增益 %1 dB").arg(value.toFixed(1))
            onMoved: root.setGain(value)
            Keys.onUpPressed: function(event) {
                root.setGain(value + (event.modifiers & Qt.ShiftModifier ? 1 : 0.1))
                event.accepted = true
            }
            Keys.onDownPressed: function(event) {
                root.setGain(value - (event.modifiers & Qt.ShiftModifier ? 1 : 0.1))
                event.accepted = true
            }
            Binding on value {
                value: root.gainDb
                restoreMode: Binding.RestoreBindingOrValue
            }

            background: Rectangle {
                x: slider.leftPadding + slider.availableWidth / 2 - width / 2
                y: slider.topPadding
                width: 4
                height: slider.availableHeight
                radius: 2
                color: Theme.border

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: slider.visualPosition * parent.height
                    width: parent.width
                    height: parent.height - y
                    radius: 2
                    color: Theme.accent
                }
            }

            handle: Rectangle {
                x: slider.leftPadding + slider.availableWidth / 2 - width / 2
                y: slider.topPadding + slider.visualPosition
                   * (slider.availableHeight - height)
                width: 22
                height: 10
                radius: 3
                color: Theme.elevated
                border.color: slider.activeFocus ? Theme.accent : Theme.secondaryText
                border.width: slider.activeFocus ? 2 : 1
            }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(event) {
                    root.setGain(slider.value
                                 + (event.angleDelta.y > 0 ? 0.1 : -0.1))
                    event.accepted = true
                }
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.WithinBounds
                onDoubleTapped: root.setGain(0)
            }
        }

        Label {
            Layout.fillWidth: true
            text: (root.gainDb >= 0 ? "+" : "")
                  + root.gainDb.toFixed(1) + (root.preamp ? " dB" : "")
            color: Theme.primaryText
            font.pixelSize: Math.max(12, Qt.application.font.pixelSize)
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
