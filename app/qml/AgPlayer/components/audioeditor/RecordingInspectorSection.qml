import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: section
    color: Theme.elevated
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: collapsed ? 42 : 372
    property bool collapsed: false
    property url recordingTarget

    FileDialog {
        id: recordingFileDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("WAV 音频 (*.wav)")]
        onAccepted: {
            section.recordingTarget = selectedFile
            AudioEditorController.startRecording(
                selectedFile,
                deviceCombo.currentValue || "",
                Number(sampleRateCombo.currentValue),
                Number(channelCombo.currentValue),
                monitorSwitch.checked,
                insertMode.checked)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        ToolButton {
            Layout.fillWidth: true
            text: (section.collapsed ? "▸  " : "▾  ") + qsTr("录音")
            font.bold: true
            onClicked: section.collapsed = !section.collapsed
        }
        GridLayout {
            visible: !section.collapsed
            columns: 2
            Layout.fillWidth: true
            columnSpacing: 8
            rowSpacing: 6

            Label { text: qsTr("输入设备") }
            ComboBox {
                id: deviceCombo
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"
                model: AudioEditorController.recordingDevices
                enabled: count > 0 && !AudioEditorController.recording
                displayText: count > 0 ? currentText : qsTr("未检测到设备")
            }
            Label { text: qsTr("输入声道") }
            ComboBox {
                id: channelCombo
                Layout.fillWidth: true
                textRole: "text"; valueRole: "value"
                model: [{text: qsTr("单声道"), value: 1},
                        {text: qsTr("立体声"), value: 2}]
                currentIndex: 1
                enabled: !AudioEditorController.recording
            }
            Label { text: qsTr("采样率") }
            ComboBox {
                id: sampleRateCombo
                Layout.fillWidth: true
                textRole: "text"; valueRole: "value"
                model: [{text: "44100 Hz", value: 44100},
                        {text: "48000 Hz", value: 48000}]
                currentIndex: 1
                enabled: !AudioEditorController.recording
            }
            Label { text: qsTr("输入电平") }
            ProgressBar {
                Layout.fillWidth: true
                from: 0; to: 1
                value: AudioEditorController.inputLevel
            }
            Label { text: qsTr("录音模式") }
            RowLayout {
                ButtonGroup { id: recordingMode }
                RadioButton {
                    text: qsTr("新建录音"); checked: true
                    ButtonGroup.group: recordingMode
                }
                RadioButton {
                    id: insertMode
                    text: qsTr("插入到光标")
                    ButtonGroup.group: recordingMode
                    enabled: AudioEditorController.hasDocument
                }
            }
            Label { text: qsTr("监听") }
            Switch {
                id: monitorSwitch
                checked: false
                enabled: !AudioEditorController.recording
            }
            Label { text: qsTr("输出格式") }
            ComboBox { Layout.fillWidth: true; model: ["WAV (PCM 24 bit)"]; enabled: false }
            Label { text: qsTr("保存位置") }
            TextField {
                Layout.fillWidth: true
                readOnly: true
                text: section.recordingTarget.toString().replace("file:///", "")
                placeholderText: qsTr("开始录音时选择")
            }
        }
        RowLayout {
            visible: !section.collapsed
            Layout.fillWidth: true
            Button {
                Layout.fillWidth: true
                text: !AudioEditorController.recording ? qsTr("开始录音")
                    : AudioEditorController.recordingPaused ? qsTr("继续") : qsTr("暂停")
                enabled: AudioEditorController.recordingDevices.length > 0
                onClicked: {
                    if (!AudioEditorController.recording) recordingFileDialog.open()
                    else if (AudioEditorController.recordingPaused) AudioEditorController.resumeRecording()
                    else AudioEditorController.pauseRecording()
                }
            }
            Button {
                text: qsTr("停止")
                enabled: AudioEditorController.recording
                onClicked: AudioEditorController.stopRecording()
            }
        }
    }
}
