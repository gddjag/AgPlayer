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
        width: 430
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
            rowSpacing: 8
            columnSpacing: 12

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
    Menu {
        id: moreMenu
        MenuItem { text: qsTr("增益…"); onTriggered: gainDialog.open() }
        MenuItem { text: qsTr("峰值归一化"); onTriggered: AudioEditorController.normalize() }
        MenuItem { text: qsTr("插入 1 秒静音"); onTriggered: AudioEditorController.insertSilence(
                AudioEditorController.positionMs * AudioEditorController.sampleRate / 1000,
                AudioEditorController.sampleRate) }
        MenuSeparator {}
        MenuItem {
            text: qsTr("标记管理…")
            enabled: AudioEditorController.markers.length > 0
            onTriggered: markerManagementDialog.open()
        }
        Instantiator {
            model: AudioEditorController.markers
            delegate: MenuItem {
                required property var modelData
                text: modelData.name + "  "
                      + editorTransportBar.timeText(modelData.positionMs)
                onTriggered: AudioEditorController.seekMs(modelData.positionMs)
            }
            onObjectAdded: function(index, object) { moreMenu.insertItem(index + 5, object) }
            onObjectRemoved: function(index, object) { moreMenu.removeItem(object) }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("导出选区…")
            enabled: AudioEditorController.selectionStart >= 0
            onTriggered: {
                exportRangeBox.currentIndex = 1
                exportSettingsDialog.open()
            }
        }
        MenuSeparator { visible: AudioEditorController.recording }
        MenuItem {
            text: qsTr("取消录音")
            visible: AudioEditorController.recording
            enabled: AudioEditorController.recording
            onTriggered: AudioEditorController.cancelRecording()
        }
    }
    Dialog {
        id: markerManagementDialog
        objectName: "markerManagementDialog"
        title: qsTr("标记管理")
        modal: true
        anchors.centerIn: parent
        width: 420
        height: Math.min(420, 116 + AudioEditorController.markers.length * 42)
        standardButtons: Dialog.Close
        contentItem: ListView {
            id: markerList
            clip: true
            spacing: 4
            model: AudioEditorController.markers
            delegate: RowLayout {
                required property int index
                required property var modelData
                width: markerList.width
                height: 38
                TextField {
                    Layout.fillWidth: true
                    text: modelData.name
                    selectByMouse: true
                    onEditingFinished: AudioEditorController.renameMarker(index, text)
                }
                Label {
                    text: editorTransportBar.timeText(modelData.positionMs)
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
                ToolButton {
                    icon.source: Theme.icon("skip-forward-fill")
                    Accessible.name: qsTr("跳转")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: AudioEditorController.seekMs(modelData.positionMs)
                }
                ToolButton {
                    icon.source: Theme.icon("delete-bin-line")
                    icon.color: Theme.waveformRed
                    Accessible.name: qsTr("删除标记")
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: AudioEditorController.removeMarker(index)
                }
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
        title: qsTr("调整增益")
        modal: true
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
        title: qsTr("舍弃未保存更改？")
        modal: true
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
        function onMoreMenuRequested() { moreMenu.popup() }
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
        else if (control && event.key === Qt.Key_M) AudioEditorController.addMarker(
            qsTr("标记 %1").arg(AudioEditorController.markers.length + 1),
            AudioEditorController.positionMs * AudioEditorController.sampleRate / 1000)
        else if (control && shift && event.key === Qt.Key_A) AudioEditorController.clearSelection()
        else if (control && event.key === Qt.Key_W) AudioEditorController.clearDocument()
        else if (control && event.key === Qt.Key_A) AudioEditorController.setSelection(
            0, AudioEditorController.totalFrames)
        else if (control && event.key === Qt.Key_Left) AudioEditorController.seekPreviousMarker()
        else if (control && event.key === Qt.Key_Right) AudioEditorController.seekNextMarker()
        else if (event.key === Qt.Key_Delete) AudioEditorController.triggerAction("editor.deleteSelection")
        else if (event.key === Qt.Key_Space) AudioEditorController.playPause()
        else if (event.key === Qt.Key_R) recordingInspector.requestRecording(false)
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
            onExportSelectionRequested: {
                exportRangeBox.currentIndex = 1
                exportSettingsDialog.open()
            }
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

                EditorTransportBar {
                    id: editorTransportBar
                    objectName: "editorTransportBar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                    onRecordingRequested: recordingInspector.requestRecording(false)
                }
            }

            Rectangle {
                id: inspector
                objectName: "editorInspector"
                readonly property int businessSectionCount: 2
                Layout.preferredWidth: page.width < 1100 ? 248 : 284
                Layout.minimumWidth: 224
                Layout.maximumWidth: 304
                Layout.fillHeight: true
                Layout.rightMargin: 4
                color: Theme.panel
                border.color: Theme.border
                radius: Theme.radiusSm

                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: inspector.width
                        spacing: 10

                        RecordingInspectorSection {
                            id: recordingInspector
                            objectName: "recordingInspector"
                            Layout.fillWidth: true
                        }
                        TimePitchInspectorSection {
                            objectName: "timePitchInspector"
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        EditorStatusBar {
            objectName: "editorStatusBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 28
        }
    }
}
