import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: canvas
    color: Theme.isLight ? "#F7FAFA" : "#11191B"
    border.color: Theme.border
    radius: Theme.radiusSm
    clip: true

    function frameAt(x) {
        return Math.round(Math.max(0, Math.min(waveArea.width, x))
                          / Math.max(1, waveArea.width)
                          * AudioEditorController.totalFrames)
    }
    function timeText(frame) {
        if (AudioEditorController.sampleRate <= 0)
            return "0:00"
        const seconds = frame / AudioEditorController.sampleRate
        const minutes = Math.floor(seconds / 60)
        return minutes + ":" + String(Math.floor(seconds % 60)).padStart(2, "0")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: Theme.isLight ? "#EEF3F3" : "#151F21"
            border.color: Theme.border

            Row {
                anchors.fill: parent
                anchors.leftMargin: 26
                Repeater {
                    model: 9
                    Item {
                        width: Math.max(48, (canvas.width - 52) / 9)
                        height: 38
                        Rectangle {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            width: 1
                            height: 10
                            color: Theme.border
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: AudioEditorController.hasDocument
                                  ? canvas.timeText(AudioEditorController.totalFrames
                                                    * index / 8) : "--:--"
                            color: Theme.secondaryText
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        Item {
            id: waveArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            Repeater {
                model: Math.max(1, AudioEditorController.channels)
                Rectangle {
                    x: 26
                    y: index * waveArea.height / Math.max(1, AudioEditorController.channels)
                    width: waveArea.width - 38
                    height: 1
                    color: Theme.border
                    opacity: index > 0 ? 0.7 : 0
                }
            }

            AudioEditorWaveformItem {
                anchors.fill: parent
                anchors.leftMargin: 26
                anchors.rightMargin: 12
                anchors.topMargin: 10
                anchors.bottomMargin: 10
                channelPeaks: AudioEditorController.channelPeaks
                waveformColor: Theme.isLight ? "#169B97" : "#39C7C0"
            }

            Rectangle {
                visible: AudioEditorController.selectionStart >= 0
                x: 26 + (waveArea.width - 38)
                   * AudioEditorController.selectionStart
                   / Math.max(1, AudioEditorController.totalFrames)
                width: (waveArea.width - 38)
                       * AudioEditorController.selectionFrames
                       / Math.max(1, AudioEditorController.totalFrames)
                anchors.top: parent.top
                anchors.topMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                color: "#2639C7C0"
                border.color: Theme.cyan
                border.width: 1
            }

            Rectangle {
                visible: AudioEditorController.hasDocument
                x: 26 + (waveArea.width - 38)
                   * AudioEditorController.positionMs
                   / Math.max(1, AudioEditorController.durationMs)
                y: 4
                width: 1
                height: waveArea.height - 8
                color: Theme.waveformRed
            }

            Text {
                visible: !AudioEditorController.hasDocument
                anchors.centerIn: parent
                text: qsTr("打开音频或新建录音以开始编辑")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 14
            }

            Repeater {
                model: AudioEditorController.channels
                Text {
                    x: 8
                    y: (index + 0.5) * waveArea.height
                       / Math.max(1, AudioEditorController.channels) - height / 2
                    text: index === 0 ? "L" : index === 1 ? "R" : index + 1
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
            }

            MouseArea {
                anchors.fill: parent
                anchors.leftMargin: 26
                anchors.rightMargin: 12
                enabled: AudioEditorController.hasDocument
                property real pressX: 0
                onPressed: mouse => {
                    pressX = mouse.x
                    AudioEditorController.seekMs(
                        canvas.frameAt(mouse.x) * 1000
                        / Math.max(1, AudioEditorController.sampleRate))
                }
                onPositionChanged: mouse => {
                    if (!pressed) return
                    const first = canvas.frameAt(pressX)
                    const last = canvas.frameAt(mouse.x)
                    if (first !== last)
                        AudioEditorController.setSelection(
                            Math.min(first, last), Math.max(first, last))
                }
                onDoubleClicked: AudioEditorController.clearSelection()
            }
        }
    }
}
