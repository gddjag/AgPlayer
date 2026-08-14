import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "audioEditorPage"
    color: Theme.background
    clip: true
    readonly property bool narrowLayout: width < 1100

    function textInputHasFocus() {
        const active = page.Window.window ? page.Window.window.activeFocusItem : null
        return active && (active.inputMethodComposing !== undefined
            || active.selectedText !== undefined)
    }
    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.playPause()
    }
    Shortcut {
        sequence: "R"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: {
            if (AudioEditorController.recording)
                AudioEditorController.stopRecording()
            else
                recordingInspector.requestRecording(false)
        }
    }

    FileDialog {
        id: openDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("音频文件 (*.wav *.flac *.mp3 *.aac *.m4a *.ogg *.opus *.wma)")]
        onAccepted: AudioEditorController.openFile(selectedFile)
    }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("WAV 音频 (*.wav)"), qsTr("FLAC 音频 (*.flac)"),
                      qsTr("MP3 音频 (*.mp3)"), qsTr("AAC 音频 (*.m4a *.aac)"),
                      qsTr("Ogg Vorbis (*.ogg)"), qsTr("Opus 音频 (*.opus)")]
        onAccepted: AudioEditorController.saveAs(selectedFile)
    }
    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: exportSettingsDialog.currentFormat.extension
        nameFilters: [exportSettingsDialog.currentFormat.filter]
        onAccepted: AudioEditorController.exportTo(
            selectedFile,
            exportRangeBox.currentIndex === 1,
            exportSettingsDialog.currentFormat.codec,
            exportSampleRateBox.currentValue,
            exportChannelBox.currentValue,
            exportBitRateBox.currentValue,
            exportSettingsDialog.currentFormat.supportsMetadata === true
                && exportMetadataCheck.checked,
            exportVbrCheck.checked,
            exportQualityBox.value)
    }
    Dialog {
        id: exportSettingsDialog
        objectName: "audioEditorExportDialog"
        title: qsTr("导出音频")
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, page.width - 32)
        height: Math.min(486, page.height - 32)
        padding: 20
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var formats: AudioEditorController.exportFormats
        readonly property var currentFormat: formats.length > 0
            ? formats[Math.max(0, exportFormatBox.currentIndex)]
            : ({ extension: "", codec: "", filter: "", lossy: false,
                 supportsMetadata: false, sampleRates: [] })
        function sampleRateOptions() {
            const options = [{ text: qsTr("保持原始"), value: 0 }]
            const rates = currentFormat.sampleRates || []
            for (let index = 0; index < rates.length; ++index) {
                const rate = Number(rates[index])
                options.push({ text: rate >= 1000
                    ? (rate / 1000).toFixed(rate % 1000 === 0 ? 0 : 1) + " kHz"
                    : rate + " Hz", value: rate })
            }
            return options
        }
        onAccepted: exportDialog.open()
        contentItem: GridLayout {
            columns: 2
            rowSpacing: 10
            columnSpacing: 16

            Label { text: qsTr("导出范围") }
            ComboBox {
                id: exportRangeBox
                Layout.fillWidth: true
                model: [qsTr("完整文档"), qsTr("当前选区")]
            }
            Label { text: qsTr("输出格式") }
            ComboBox {
                id: exportFormatBox
                Layout.fillWidth: true
                model: exportSettingsDialog.formats
                textRole: "text"
                enabled: count > 0
            }
            Label { text: qsTr("采样率") }
            ComboBox {
                id: exportSampleRateBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: exportSettingsDialog.sampleRateOptions()
            }
            Label { text: qsTr("声道") }
            ComboBox {
                id: exportChannelBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("保持原始"), value: 0 },
                    { text: qsTr("单声道"), value: 1 },
                    { text: qsTr("立体声"), value: 2 }
                ]
            }
            Label { text: qsTr("目标码率") }
            ComboBox {
                id: exportBitRateBox
                Layout.fillWidth: true
                enabled: exportSettingsDialog.currentFormat.lossy === true
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("自动"), value: 0 },
                    { text: "128 kbps", value: 128000 },
                    { text: "192 kbps", value: 192000 },
                    { text: "256 kbps", value: 256000 },
                    { text: "320 kbps", value: 320000 }
                ]
            }
            Label { text: qsTr("编码质量") }
            SpinBox {
                id: exportQualityBox
                Layout.fillWidth: true
                enabled: exportSettingsDialog.currentFormat.lossy === true
                from: 0
                to: 100
                value: 80
            }
            Label { text: qsTr("可变码率") }
            CheckBox {
                id: exportVbrCheck
                checked: true
                enabled: exportSettingsDialog.currentFormat.lossy === true
            }
            Label { text: qsTr("保留元数据") }
            CheckBox {
                id: exportMetadataCheck
                checked: true
                enabled: exportSettingsDialog.currentFormat.supportsMetadata === true
            }
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }
    Dialog {
        id: gainDialog
        objectName: "audioEditorGainDialog"
        title: qsTr("调整增益")
        modal: true
        anchors.centerIn: parent
        width: Math.min(340, page.width - 32)
        padding: 20
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: AudioEditorController.applyGain(gainValue.value)
        contentItem: RowLayout {
            Label { text: qsTr("增益") }
            SpinBox { id: gainValue; from: -60; to: 24; value: 0 }
            Label { text: "dB" }
        }
    }
    Dialog {
        id: discardOpenDialog
        objectName: "audioEditorDiscardDialog"
        title: qsTr("舍弃未保存更改？")
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, page.width - 32)
        padding: 20
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: AudioEditorController.confirmDiscardAndOpen()
        onRejected: AudioEditorController.cancelDiscardAndOpen()
        Label {
            text: qsTr("当前音频尚未保存。继续将舍弃这些更改。")
            color: Theme.primaryText
        }
    }
    Connections {
        target: AudioEditorController
        function onOpenRequested() { openDialog.open() }
        function onNewRecordingRequested() { recordingInspector.requestRecording(true) }
        function onSaveAsRequested() { saveDialog.open() }
        function onExportRequested() {
            exportRangeBox.currentIndex = 0
            exportSettingsDialog.open()
        }
        function onDiscardConfirmationRequested() { discardOpenDialog.open() }
    }

    focus: true
    Keys.onPressed: event => {
        const control = (event.modifiers & Qt.ControlModifier) !== 0
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        if (control && alt && event.key === Qt.Key_I) AudioEditorController.triggerAction("editor.fadeIn")
        else if (control && alt && event.key === Qt.Key_O) AudioEditorController.triggerAction("editor.fadeOut")
        else if (control && shift && event.key === Qt.Key_E) AudioEditorController.triggerAction("editor.export")
        else if (control && event.key === Qt.Key_O) openDialog.open()
        else if (control && event.key === Qt.Key_R) AudioEditorController.triggerAction("editor.newRecording")
        else if (control && event.key === Qt.Key_S) AudioEditorController.save()
        else if (control && event.key === Qt.Key_Z) AudioEditorController.triggerAction("editor.undo")
        else if (control && event.key === Qt.Key_Y) AudioEditorController.triggerAction("editor.redo")
        else if (control && event.key === Qt.Key_X) AudioEditorController.triggerAction("editor.cut")
        else if (control && event.key === Qt.Key_C) AudioEditorController.triggerAction("editor.copy")
        else if (control && event.key === Qt.Key_V) AudioEditorController.triggerAction("editor.paste")
        else if (control && event.key === Qt.Key_T) AudioEditorController.triggerAction("editor.cropToSelection")
        else if (control && event.key === Qt.Key_L) AudioEditorController.triggerAction("editor.silenceSelection")
        else if (control && shift && event.key === Qt.Key_A) AudioEditorController.clearSelection()
        else if (control && event.key === Qt.Key_W) AudioEditorController.clearDocument()
        else if (control && event.key === Qt.Key_A) AudioEditorController.setSelection(
            0, AudioEditorController.totalFrames)
        else if (event.key === Qt.Key_Delete) AudioEditorController.triggerAction("editor.deleteSelection")
        else if (event.key === Qt.Key_Left) AudioEditorController.seekMs(
            Math.max(0, AudioEditorController.positionMs - (shift ? 1000 : 10)))
        else if (event.key === Qt.Key_Right) AudioEditorController.seekMs(
            Math.min(AudioEditorController.durationMs,
                     AudioEditorController.positionMs + (shift ? 1000 : 10)))
        else if (event.key === Qt.Key_Home) AudioEditorController.seekMs(0)
        else if (event.key === Qt.Key_End) AudioEditorController.seekMs(
            AudioEditorController.durationMs)
        else if (event.key === Qt.Key_Escape) {
            if (AudioEditorController.recording) AudioEditorController.cancelRecording()
            else if (AudioEditorController.busy) AudioEditorController.cancelOperation()
            else AudioEditorController.stopPlayback()
        }
        else return
        event.accepted = true
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 36
        z: 50
        visible: AudioEditorController.busy
        width: 300
        height: 62
        radius: Theme.radiusSm
        color: Theme.elevated
        border.color: Theme.border
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            ProgressBar {
                Layout.fillWidth: true
                value: AudioEditorController.progress
            }
            Button {
                text: qsTr("取消")
                onClicked: AudioEditorController.cancelOperation()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EditorCommandBar {
            objectName: "editorCommandBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            onExportRequested: selectionOnly => {
                exportRangeBox.currentIndex = selectionOnly ? 1 : 0
                exportSettingsDialog.open()
            }
            onGainRequested: gainDialog.open()
        }

        FileSummaryBar {
            objectName: "fileSummaryBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 38
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 260
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 4
                spacing: 8

                EditorWaveformCanvas {
                    objectName: "editorWaveformCanvas"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                OverviewNavigator {
                    objectName: "overviewNavigator"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 66
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                    Layout.minimumHeight: 104
                    Layout.maximumHeight: 104
                    spacing: 12
                    EditorTransportBar {
                        id: editorTransportBar
                        objectName: "editorTransportBar"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onRecordingRequested: recordingInspector.requestRecording(false)
                    }
                    Rectangle {
                        objectName: "editorShortcutCard"
                        Layout.preferredWidth: page.width >= 1500 ? 300 : 220
                        visible: !page.narrowLayout
                        Layout.fillHeight: true
                        color: Theme.elevated
                        border.color: Theme.border
                        radius: Theme.radiusSm
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4
                            Label { text: qsTr("快捷键"); font.bold: true }
                            Label { text: qsTr("Space  播放 / 暂停"); color: Theme.secondaryText }
                            Label { text: qsTr("R  开始 / 停止录音"); color: Theme.secondaryText }
                            Label { text: qsTr("Ctrl+Shift+A  取消选区"); color: Theme.secondaryText }
                            Label { text: qsTr("Ctrl+W  清空当前文件"); color: Theme.secondaryText }
                        }
                    }
                }
            }

            Rectangle {
                id: inspector
                objectName: "editorInspector"
                readonly property int businessSectionCount: 2
                readonly property bool compact: page.height < 760 || page.width < 1280
                Layout.preferredWidth: 292
                Layout.minimumWidth: page.width < 1100 ? 280 : 208
                Layout.maximumWidth: 304
                Layout.fillHeight: true
                Layout.rightMargin: 4
                color: Theme.panel
                border.color: Theme.border
                radius: Theme.radiusSm

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8
                    TabBar {
                        id: inspectorTabs
                        objectName: "editorInspectorTabs"
                        visible: inspector.compact
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 36 : 0
                        TabButton { text: qsTr("录音") }
                        TabButton { text: qsTr("速度与音高") }
                    }
                    RecordingInspectorSection {
                        id: recordingInspector
                        objectName: "recordingInspector"
                        visible: !inspector.compact || inspectorTabs.currentIndex === 0
                        Layout.fillWidth: true
                        Layout.fillHeight: inspector.compact
                        collapsed: false
                    }
                    TimePitchInspectorSection {
                        objectName: "timePitchInspector"
                        visible: !inspector.compact || inspectorTabs.currentIndex === 1
                        Layout.fillWidth: true
                        Layout.fillHeight: inspector.compact
                        collapsed: false
                    }
                }
            }
        }

        EditorStatusBar {
            objectName: "editorStatusBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            showShortcutHint: page.narrowLayout
        }
    }
}
