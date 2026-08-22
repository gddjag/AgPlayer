import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: "#071a2d"
    border.color: "#294662"
    border.width: 1
    radius: 6

    function durationText(milliseconds) {
        const value = Math.max(0, milliseconds)
        const hours = Math.floor(value / 3600000)
        const minutes = Math.floor((value % 3600000) / 60000)
        const seconds = Math.floor((value % 60000) / 1000)
        const millis = value % 1000
        return (hours > 0 ? String(hours).padStart(2, "0") + ":" : "")
                + String(minutes).padStart(2, "0") + ":"
                + String(seconds).padStart(2, "0") + "."
                + String(millis).padStart(3, "0")
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 18

        Rectangle {
            objectName: "fileSummaryIcon"
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            radius: 5
            color: "#0a2138"
            border.color: "#294662"
            ThemedIcon {
                anchors.centerIn: parent
                source: Theme.icon("music-2-fill")
                tint: "#f4f8ff"
                sourceSize.width: 18
                sourceSize.height: 18
            }
        }

        Text {
            Layout.maximumWidth: 250
            text: AudioEditorController.hasDocument
                  ? AudioEditorController.fileName : qsTr("未打开音频")
            elide: Text.ElideMiddle
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 13
        }
        Repeater {
            model: AudioEditorController.hasDocument ? [
                qsTr("时长：") + durationText(AudioEditorController.durationMs),
                qsTr("采样率：")
                    + (AudioEditorController.sampleRate >= 1000
                        ? (AudioEditorController.sampleRate / 1000).toFixed(1)
                            + " kHz"
                        : AudioEditorController.sampleRate + " Hz"),
                qsTr("位深度：") + (AudioEditorController.bitsPerSample > 0
                    ? AudioEditorController.bitsPerSample + "-bit" : "--"),
                qsTr("声道：") + (AudioEditorController.channels === 1 ? qsTr("单声道")
                    : AudioEditorController.channels === 2 ? qsTr("立体声")
                    : AudioEditorController.channels + qsTr(" 声道")),
                qsTr("BPM：") + (AudioEditorController.originalBpm > 0
                    ? AudioEditorController.originalBpm.toFixed(0) : "--")
            ] : []
            RowLayout {
                spacing: 18
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 20
                    color: "#34506c"
                }
                Text {
                    text: modelData
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }
            }
        }
        Item { Layout.fillWidth: true }
    }
}
