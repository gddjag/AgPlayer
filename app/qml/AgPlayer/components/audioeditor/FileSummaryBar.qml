import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: Theme.background
    border.color: Theme.border

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
        spacing: 30

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
                AudioEditorController.formatName || "--",
                AudioEditorController.sampleRate + " Hz",
                AudioEditorController.bitsPerSample > 0
                    ? AudioEditorController.bitsPerSample + " bit" : "-- bit",
                AudioEditorController.channels === 1 ? qsTr("单声道")
                    : AudioEditorController.channels === 2 ? qsTr("立体声")
                    : AudioEditorController.channels + qsTr(" 声道"),
                durationText(AudioEditorController.durationMs),
                qsTr("BPM --")
            ] : []
            Text {
                text: modelData
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
            }
        }
        Item { Layout.fillWidth: true }
    }
}
