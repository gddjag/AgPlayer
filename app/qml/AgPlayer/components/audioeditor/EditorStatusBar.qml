import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    property bool showShortcutHint: false

    color: Theme.panel
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 18
        Text {
            text: AudioEditorController.errorMessage.length > 0
                  ? AudioEditorController.errorMessage
                  : AudioEditorController.hasDocument ? qsTr("就绪") : qsTr("未打开音频")
            color: AudioEditorController.errorMessage.length > 0
                   ? Theme.waveformRed : Theme.secondaryText
            font.pixelSize: 11
        }
        Item { Layout.fillWidth: true }
        Text {
            objectName: "editorStatusShortcutHint"
            text: showShortcutHint
                ? qsTr("播放：Phase 12 接入 · R：Phase 9 接入 · Ctrl+Shift+A 取消选区 · Ctrl+W 清空")
                : AudioEditorController.selectionStart >= 0
                ? qsTr("选区范围：%1 - %2（%3 帧）")
                    .arg(AudioEditorController.selectionStart)
                    .arg(AudioEditorController.selectionEnd)
                    .arg(AudioEditorController.selectionFrames)
                : qsTr("选区范围：--")
            color: Theme.secondaryText
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: showShortcutHint
        }
        Item { Layout.fillWidth: true }
        Text {
            text: AudioEditorController.hasDocument
                ? AudioEditorController.sampleRate + " Hz   "
                  + (AudioEditorController.bitsPerSample || "--") + " bit   "
                  + AudioEditorController.channels + qsTr(" 声道")
                : qsTr("采样率 --   位深 --   声道 --   时长 --")
            color: Theme.secondaryText
            font.pixelSize: 11
        }
    }
}
