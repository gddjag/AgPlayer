import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: section
    objectName: "recordingInspectorSection"
    color: Theme.elevated
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: collapsed ? 38 : 360
    property bool collapsed: false
    readonly property int recordingActionCount: 4
    property url recordingTarget
    property bool forceNewRecording: false
    function requestRecording(forceNew) {
        forceNewRecording = forceNew === true
        if (AudioEditorController.modified
                && (forceNewRecording || newMode.checked))
            discardRecordingDialog.open()
        else if (forceNewRecording)
            createBlankRecordingDocument()
        else
            beginRecording()
    }
    function createBlankRecordingDocument() {
        AudioEditorController.createRecordingDocument(
            Number(sampleRateCombo.currentValue),
            Number(channelCombo.currentValue))
    }
    function beginRecording() {
        AudioEditorController.startRecordingToTemporaryFile(
            deviceCombo.currentValue || "",
            Number(sampleRateCombo.currentValue),
            Number(channelCombo.currentValue),
            monitorSwitch.checked,
            insertMode.checked && AudioEditorController.hasDocument)
    }
    function pauseOrResume() {
        if (AudioEditorController.recordingPaused)
            AudioEditorController.resumeRecording()
        else
            AudioEditorController.pauseRecording()
    }

    Dialog {
        id: discardRecordingDialog
        objectName: "recordingDiscardDialog"
        title: qsTr("舍弃未保存更改？")
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, Math.max(260, section.width + 100))
        padding: 20
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: forceNewRecording
            ? section.createBlankRecordingDocument()
            : section.beginRecording()
        Label {
            text: qsTr("新建录音将舍弃当前未保存的音频。")
            color: Theme.primaryText
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
            Label { text: qsTr("录音文件") }
            TextField {
                Layout.fillWidth: true
                readOnly: true
                text: AudioEditorController.recording
                    ? qsTr("临时 WAV · 保存时选择位置") : ""
                placeholderText: qsTr("使用私有临时 WAV")
            }
            Button {
                objectName: "recordingRefreshDevicesButton"
                Layout.columnSpan: 2
                Layout.fillWidth: true
                text: qsTr("刷新输入设备")
                enabled: !AudioEditorController.recording
                onClicked: AudioEditorController.refreshRecordingDevices()
            }
            RowLayout {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Button {
                    objectName: "recordingStartButton"
                    Layout.fillWidth: true
                    text: qsTr("开始录音")
                    enabled: !AudioEditorController.recording
                             && !AudioEditorController.busy
                             && deviceCombo.count > 0
                    onClicked: section.requestRecording(false)
                }
                Button {
                    objectName: "recordingPauseResumeButton"
                    Layout.fillWidth: true
                    text: AudioEditorController.recordingPaused
                          ? qsTr("继续") : qsTr("暂停")
                    enabled: AudioEditorController.recording
                    onClicked: section.pauseOrResume()
                }
                Button {
                    objectName: "recordingStopButton"
                    Layout.fillWidth: true
                    text: qsTr("停止")
                    enabled: AudioEditorController.recording
                    onClicked: AudioEditorController.stopRecording()
                }
                Button {
                    objectName: "recordingCancelButton"
                    Layout.fillWidth: true
                    text: qsTr("取消录音")
                    enabled: AudioEditorController.recording
                    onClicked: AudioEditorController.cancelRecording()
                }
            }
        }
    }
}
