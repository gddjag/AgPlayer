import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: transport
    signal recordingRequested()
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
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 10

        Repeater {
            model: [
                { key: "record", label: AudioEditorController.recording
                    ? (AudioEditorController.recordingPaused ? qsTr("继续") : qsTr("暂停"))
                    : qsTr("录音"), icon: "checkbox-blank-circle-fill",
                  objectName: "transportRecord", shortcut: "Ctrl+R" },
                { key: "stop", label: qsTr("停止"), icon: "checkbox-blank-line",
                  objectName: "transportStop", shortcut: "" }
            ]
            ColumnLayout {
                required property var modelData
                Layout.preferredWidth: 46
                spacing: 3
                ToolButton {
                    objectName: modelData.objectName
                    Layout.alignment: Qt.AlignHCenter
                    icon.source: Theme.icon(modelData.icon)
                    icon.color: modelData.key === "record" ? Theme.waveformRed : Theme.iconPrimary
                    enabled: modelData.key === "record"
                        ? !AudioEditorController.busy
                        : modelData.key === "stop"
                          ? AudioEditorController.playing || AudioEditorController.recording
                          : false
                    Accessible.name: modelData.label
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.label + (modelData.shortcut.length > 0
                        ? "  (" + modelData.shortcut + ")" : "")
                    onClicked: {
                        if (modelData.key === "record") {
                            if (!AudioEditorController.recording) transport.recordingRequested()
                            else if (AudioEditorController.recordingPaused) AudioEditorController.resumeRecording()
                            else AudioEditorController.pauseRecording()
                        } else if (modelData.key === "stop") {
                            if (AudioEditorController.recording) AudioEditorController.stopRecording()
                            else AudioEditorController.stopPlayback()
                        }
                    }
                }
                Label {
                    text: modelData.label
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        ToolButton {
            icon.source: Theme.icon(AudioEditorController.playing ? "pause-fill" : "play-fill")
            icon.color: Theme.waveformGreen
            icon.width: 36
            icon.height: 36
            enabled: AudioEditorController.hasDocument
            Accessible.name: AudioEditorController.playing ? qsTr("暂停") : qsTr("播放")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: AudioEditorController.playPause()
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
        }
        ColumnLayout {
            ToolButton {
                checkable: true
                checked: AudioEditorController.loopEnabled
                icon.source: Theme.icon("repeat-fill")
                icon.color: checked ? Theme.cyan : Theme.iconPrimary
                enabled: AudioEditorController.hasDocument
                Accessible.name: qsTr("循环")
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onToggled: AudioEditorController.setLoopEnabled(checked)
            }
            Label { text: qsTr("循环"); font.pixelSize: 10 }
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
                Layout.preferredWidth: 104
                Label {
                    text: modelData.value
                    color: index === 0 ? Theme.waveformGreen : Theme.primaryText
                    font.pixelSize: 16
                    Layout.alignment: Qt.AlignHCenter
                }
                Label { text: modelData.label; color: Theme.secondaryText; font.pixelSize: 11 }
            }
        }
    }
}
