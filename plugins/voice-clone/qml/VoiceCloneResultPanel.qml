import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AgPlayer

Rectangle {
    id: root
    objectName: "voiceCloneResultPanel"
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: 190

    property string resultPath: ""
    readonly property bool hasResult: resultPath !== ""
    signal saveRequested(string path, string destinationPath)
    signal deleteRequested(string path)
    signal sendToEditorRequested(string path)

    function saveTo(destinationPath) {
        if (!hasResult || destinationPath === "") return
        saveRequested(resultPath, destinationPath)
        saveDialog.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        Label {
            text: qsTr("5  生成结果")
            color: Theme.primaryText
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusSm
            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12
                ThemedIcon {
                    source: Theme.icon(root.hasResult ? "music-2-fill" : "information-line")
                    tint: root.hasResult ? Theme.iconAccent : Theme.iconSecondary
                    sourceSize.width: 28
                    sourceSize.height: 28
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: root.hasResult ? qsTr("已生成真实音频文件") : qsTr("暂无生成结果")
                        color: Theme.primaryText
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.hasResult ? root.resultPath : qsTr("生成成功后在此显示实际文件；不绘制占位波形")
                        color: Theme.secondaryText
                        elide: Text.ElideMiddle
                    }
                }
                Button {
                    objectName: "voiceCloneSendToEditorButton"
                    visible: root.hasResult
                    text: qsTr("发送到剪辑")
                    onClicked: root.sendToEditorRequested(root.resultPath)
                }
                Button {
                    objectName: "voiceCloneSaveResultButton"
                    visible: root.hasResult
                    text: qsTr("保存文件")
                    icon.source: Theme.icon("download-line")
                    onClicked: saveDialog.open()
                }
                Button {
                    objectName: "voiceCloneDeleteResultButton"
                    visible: root.hasResult
                    text: qsTr("删除")
                    icon.source: Theme.icon("delete-bin-line")
                    onClicked: root.deleteRequested(root.resultPath)
                }
            }
        }
    }

    FileDialog {
        id: saveDialog
        objectName: "voiceCloneSaveDialog"
        title: qsTr("保存生成人声")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("WAV 音频 (*.wav)")]
        onAccepted: root.saveTo(decodeURIComponent(
                        selectedFile.toString().replace(/^file:\/\/\//, "")))
    }
}
