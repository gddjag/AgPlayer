import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    implicitWidth: spacious ? 72 : 58
    implicitHeight: 300

    function setGain(value) {
        var bounded = Math.max(-12, Math.min(12, Math.round(value * 10) / 10))
        if (preamp)
            EqualizerController.preampDb = bounded
        else
            EqualizerController.setBandGain(bandIndex, bounded)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.spacious ? 9 : 6

        Label {
            objectName: root.objectName + "-frequency"
            Layout.fillWidth: true
            Layout.preferredHeight: root.spacious ? 28 : 24
            text: root.frequencyLabel
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: root.spacious ? 16 : 14
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
            Layout.fillWidth: true
            Layout.fillHeight: true

            Repeater {
                model: 9
                Rectangle {
                    required property int index
                    x: Math.round((parent.width - width) / 2)
                    y: Math.round(index * (parent.height - 1) / 8)
                    width: index === 4 ? (root.spacious ? 24 : 18)
                                       : (root.spacious ? 14 : 10)
                    height: 1
                    color: index === 4 ? Theme.borderStrong : Theme.divider
                    opacity: index === 4 ? 0.8 : 0.65
                }
            }

            Item {
                id: slider
                objectName: root.objectName + "-control"
                anchors.fill: parent
                anchors.leftMargin: root.spacious ? 14 : 11
                anchors.rightMargin: root.spacious ? 14 : 11
                property real value: root.gainDb
                readonly property real visualPosition: (12 - value) / 24
                property bool pressed: dragArea.pressed
                activeFocusOnTab: true
                Accessible.role: Accessible.Slider
                Accessible.name: root.accessibleLabel
                Accessible.description: qsTr("增益 %1 dB").arg(value.toFixed(1))
                Keys.onUpPressed: function(event) {
                    root.setGain(value + (event.modifiers & Qt.ShiftModifier ? 1 : 0.1))
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) {
                    root.setGain(value - (event.modifiers & Qt.ShiftModifier ? 1 : 0.1))
                    event.accepted = true
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_PageUp) {
                        root.setGain(value + 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_PageDown) {
                        root.setGain(value - 1)
                        event.accepted = true
                    }
                }

                Item {
                    x: slider.width / 2 - 2
                    y: 9
                    width: 4
                    height: slider.height - 18

                    Rectangle {
                        anchors.fill: parent
                        radius: 2
                        color: Theme.isLight ? Theme.border : Theme.surfacePressed
                        border.color: Theme.border
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: slider.visualPosition * parent.height
                        width: 3
                        height: Math.max(0, parent.height - y)
                        radius: 2
                        gradient: Gradient {
                            GradientStop { position: 0; color: Theme.waveformViolet }
                            GradientStop { position: 1; color: Theme.waveformCyan }
                        }
                    }
                }

                Rectangle {
                    x: slider.width / 2 - width / 2
                    y: slider.visualPosition * (slider.height - height)
                    width: root.spacious ? 32 : 26
                    height: root.spacious ? 38 : 18
                    radius: root.spacious ? 11 : 6
                    gradient: root.spacious ? handleGradient : null
                    color: root.spacious ? "transparent"
                                         : slider.pressed ? Theme.surfacePressed
                                                          : Theme.surfaceElevated
                    border.color: slider.activeFocus ? Theme.focus
                                  : root.spacious ? Theme.textSecondary
                                                  : Theme.borderStrong
                    border.width: slider.activeFocus ? 2 : 1

                    Gradient {
                        id: handleGradient
                        orientation: Gradient.Vertical
                        GradientStop {
                            position: 0
                            color: Theme.isLight ? Theme.surfaceElevated
                                                 : Theme.primaryText
                        }
                        GradientStop {
                            position: 1
                            color: Theme.isLight ? Theme.surfacePressed
                                                 : Theme.textSecondary
                        }
                    }

                    Rectangle {
                        visible: root.spacious
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: parent.radius - 2
                        color: "transparent"
                        border.color: Theme.highlightSoft
                        border.width: 1
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: root.spacious ? 13 : 10
                        height: root.spacious ? 3 : 2
                        radius: 1
                        color: Theme.accent
                    }

                }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(event) {
                        root.setGain(slider.value
                                     + (event.angleDelta.y > 0 ? 0.1 : -0.1))
                        event.accepted = true
                    }
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.SizeVerCursor
                    function updateGain(pointerY) {
                        root.setGain(12 - Math.max(0, Math.min(height, pointerY))
                                     / height * 24)
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
        }

        Rectangle {
            objectName: root.objectName + "-valueChip"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.spacious ? 68 : 54
            Layout.preferredHeight: root.spacious ? 34 : 28
            radius: root.spacious ? 10 : 8
            color: Theme.surfaceElevated
            border.color: Theme.border

            Label {
                objectName: root.objectName + "-value"
                anchors.fill: parent
                text: (root.gainDb >= 0 ? "+" : "")
                      + root.gainDb.toFixed(1) + " dB"
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: root.spacious ? 13 : 12
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
}
