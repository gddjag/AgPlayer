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
    implicitHeight: collapsed ? 38 : 360
    property bool collapsed: false
    property url recordingTarget
    property bool forceNewRecording: false
    function requestRecording(forceNew) {
        forceNewRecording = forceNew === true
        if (AudioEditorController.modified
                && (forceNewRecording || newMode.checked))
            discardRecordingDialog.open()
        else
            recordingFileDialog.open()
    }

    Dialog {
        id: discardRecordingDialog
        title: qsTr("舍弃未保存更改？")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: recordingFileDialog.open()
        Label {
            text: qsTr("新建录音将舍弃当前未保存的音频。")
            color: Theme.primaryText
        }
    }

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
                !section.forceNewRecording && insertMode.checked)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 5

        ToolButton {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            text: (section.collapsed ? "▸  " : "▾  ") + qsTr("录音")
            font.bold: true
            onClicked: section.collapsed = !section.collapsed
        }
        GridLayout {
            visible: !section.collapsed
            columns: 2
            Layout.fillWidth: true
            columnSpacing: 8
            rowSpacing: 3

            Label { text: qsTr("输入设备") }
            ComboBox {
                id: deviceCombo
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"
                model: AudioEditorController.recordingDevices
                enabled: count > 0 && !AudioEditorController.recording
                displayText: count > 0 ? currentText : qsTr("未检测到设备")
                Component.onCompleted: {
                    for (let index = 0; index < count; ++index) {
                        if (valueAt(index) === AudioEditorController.recordingDeviceId) {
                            currentIndex = index
                            break
                        }
                    }
                }
            }
            Label { text: qsTr("输入声道") }
            ComboBox {
                id: channelCombo
                Layout.fillWidth: true
                textRole: "text"; valueRole: "value"
                model: [{text: qsTr("单声道"), value: 1},
                        {text: qsTr("立体声"), value: 2}]
                currentIndex: AudioEditorController.recordingChannels === 1 ? 0 : 1
                enabled: !AudioEditorController.recording
            }
            Label { text: qsTr("采样率") }
            ComboBox {
                id: sampleRateCombo
                Layout.fillWidth: true
                textRole: "text"; valueRole: "value"
                model: [{text: "44100 Hz", value: 44100},
                        {text: "48000 Hz", value: 48000}]
                currentIndex: AudioEditorController.recordingSampleRate === 44100 ? 0 : 1
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
                    id: newMode
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
                checked: AudioEditorController.recordingMonitor
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
    }
}
