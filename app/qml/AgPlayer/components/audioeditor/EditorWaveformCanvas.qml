import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: canvas
    color: Theme.isLight ? "#F7FAFA" : "#11191B"
    border.color: Theme.border
    radius: Theme.radiusSm
    clip: true
    onWidthChanged: AudioEditorController.viewport.setViewportWidth(
        Math.max(1, width - 38))
    Component.onCompleted: AudioEditorController.viewport.setViewportWidth(
        Math.max(1, width - 38))

    function frameAt(x) {
        const ratio = Math.max(0, Math.min(waveArea.width - 38, x))
            / Math.max(1, waveArea.width - 38)
        return Math.round(AudioEditorController.viewport.visibleStartFrame
            + ratio * AudioEditorController.viewport.visibleFrameCount)
    }
    function xAtFrame(frame) {
        return 26 + (waveArea.width - 38)
            * (frame - AudioEditorController.viewport.visibleStartFrame)
            / Math.max(1, AudioEditorController.viewport.visibleFrameCount)
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
                                  ? canvas.timeText(AudioEditorController.viewport.visibleStartFrame
                                      + AudioEditorController.viewport.visibleFrameCount
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
                visibleStartRatio: AudioEditorController.viewport.overviewStartRatio
                visibleEndRatio: AudioEditorController.viewport.overviewStartRatio
                    + AudioEditorController.viewport.overviewWidthRatio
            }

            Rectangle {
                visible: AudioEditorController.selectionStart >= 0
                x: canvas.xAtFrame(AudioEditorController.selectionStart)
                width: (waveArea.width - 38)
                       * AudioEditorController.selectionFrames
                       / Math.max(1, AudioEditorController.viewport.visibleFrameCount)
                anchors.top: parent.top
                anchors.topMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                color: "#2639C7C0"
                border.color: Theme.cyan
                border.width: 1
            }

            Repeater {
                model: AudioEditorController.markers
                Item {
                    visible: modelData.frame >= AudioEditorController.viewport.visibleStartFrame
                        && modelData.frame <= AudioEditorController.viewport.visibleEndFrame
                    x: canvas.xAtFrame(modelData.frame) - 50
                    y: 4
                    width: 100
                    height: waveArea.height - 8
                    z: 3
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 1
                        height: parent.height
                        color: Theme.ratingGold
                        opacity: 0.8
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 1
                        width: 8
                        height: 8
                        rotation: 45
                        color: Theme.ratingGold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 11
                        text: modelData.name
                        color: Theme.ratingGold
                        font.pixelSize: 9
                    }
                }
            }

            Rectangle {
                x: canvas.xAtFrame(AudioEditorController.positionMs
                    * AudioEditorController.sampleRate / 1000)
                visible: AudioEditorController.hasDocument
                    && AudioEditorController.positionMs * AudioEditorController.sampleRate / 1000
                        >= AudioEditorController.viewport.visibleStartFrame
                    && AudioEditorController.positionMs * AudioEditorController.sampleRate / 1000
                        <= AudioEditorController.viewport.visibleEndFrame
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
