import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm

    function timeText(milliseconds) {
        const value = Math.max(0, milliseconds)
        const minutes = Math.floor(value / 60000)
        const seconds = Math.floor((value % 60000) / 1000)
        const millis = value % 1000
        return String(minutes).padStart(2, "0") + ":"
                + String(seconds).padStart(2, "0") + "."
                + String(millis).padStart(3, "0")
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        spacing: 18

        Repeater {
            model: [qsTr("录音"), qsTr("停止"), qsTr("上一标记"), qsTr("下一标记")]
            ColumnLayout {
                spacing: 3
                ToolButton {
                    icon.source: Theme.icon(index === 0
                        ? "checkbox-blank-circle-fill"
                        : index === 1 ? "checkbox-blank-line"
                        : index === 2 ? "skip-back-fill" : "skip-forward-fill")
                    icon.color: index === 0 ? Theme.waveformRed : Theme.iconPrimary
                    enabled: index === 0
                        ? !AudioEditorController.recording
                        : index === 1
                          ? AudioEditorController.playing || AudioEditorController.recording
                          : AudioEditorController.hasDocument
                    onClicked: {
                        if (index === 0) AudioEditorController.triggerAction("editor.newRecording")
                        else if (index === 1) {
                            if (AudioEditorController.recording) AudioEditorController.stopRecording()
                            else AudioEditorController.stopPlayback()
                        }
                    }
                }
                Label { text: modelData; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
            }
        }

        ToolButton {
            icon.source: Theme.icon(AudioEditorController.playing ? "pause-fill" : "play-fill")
            icon.color: Theme.waveformGreen
            icon.width: 36
            icon.height: 36
            enabled: AudioEditorController.hasDocument
            onClicked: AudioEditorController.playPause()
            Layout.preferredWidth: 76
            Layout.preferredHeight: 76
        }
        ColumnLayout {
            ToolButton {
                checkable: true
                checked: AudioEditorController.loopEnabled
                icon.source: Theme.icon("repeat-fill")
                icon.color: checked ? Theme.cyan : Theme.iconPrimary
                enabled: AudioEditorController.hasDocument
                onToggled: AudioEditorController.setLoopEnabled(checked)
            }
            Label { text: qsTr("循环"); font.pixelSize: 11 }
        }
        ToolSeparator {}

        Repeater {
            model: [
                {label: qsTr("当前位置"), value: timeText(AudioEditorController.positionMs)},
                {label: qsTr("选区长度"), value: timeText(AudioEditorController.selectionFrames
                    * 1000 / Math.max(1, AudioEditorController.sampleRate))},
                {label: qsTr("总时长"), value: timeText(AudioEditorController.durationMs)}
            ]
            ColumnLayout {
                Layout.preferredWidth: 112
                Label { text: modelData.value; color: index === 0 ? Theme.waveformGreen : Theme.primaryText; font.pixelSize: 16 }
                Label { text: modelData.label; color: Theme.secondaryText; font.pixelSize: 11 }
            }
        }

        Item { Layout.fillWidth: true }
        ThemedIcon {
            source: Theme.icon("volume-up-fill")
            tint: Theme.iconPrimary
            sourceSize.width: 18
            sourceSize.height: 18
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
        }
        Slider {
            Layout.preferredWidth: 90
            from: 0; to: 1
            value: AudioEditorController.volume
            onMoved: AudioEditorController.setVolume(value)
        }
        Label { text: Math.round(AudioEditorController.volume * 100) + "%"; font.pixelSize: 11 }
        Slider { Layout.preferredWidth: 90; value: 0.5; enabled: false }
        Label { text: "×1.00"; font.pixelSize: 11 }
    }
}
