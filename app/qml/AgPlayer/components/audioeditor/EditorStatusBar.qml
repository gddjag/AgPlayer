import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    property bool showShortcutHint: false
    property bool showSuccess: false

    color: Theme.panel
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 18
        Text {
            text: AudioEditorController.errorMessage.length > 0
                  ? AudioEditorController.errorMessage
                  : showSuccess ? qsTr("导出完成：%1")
                      .arg(AudioEditorController.lastExportPath)
                  : AudioEditorController.busy ? qsTr("正在处理… %1%")
                      .arg(Math.round(AudioEditorController.progress * 100))
                  : AudioEditorController.hasDocument ? qsTr("就绪") : qsTr("未打开音频")
            color: AudioEditorController.errorMessage.length > 0
                   ? Theme.waveformRed : showSuccess ? Theme.success : Theme.secondaryText
            font.pixelSize: Theme.fontSizeCaption
        }
        Item { Layout.fillWidth: true }
        Text {
            objectName: "editorStatusShortcutHint"
            text: showShortcutHint
                ? qsTr("空格：播放 / 暂停 · S：在播放头处分割 · Delete：删除片段")
                : AudioEditorController.selectionStart >= 0
                ? qsTr("选区范围：%1 - %2（%3 帧）")
                    .arg(AudioEditorController.selectionStart)
                    .arg(AudioEditorController.selectionEnd)
                    .arg(AudioEditorController.selectionFrames)
                : qsTr("选区范围：--")
            color: Theme.secondaryText
            font.pixelSize: Theme.fontSizeCaption
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
            font.pixelSize: Theme.fontSizeCaption
        }
    }
}
