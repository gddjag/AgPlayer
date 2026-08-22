import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "audioEditorPage"
    color: "#031426"
    clip: true
    focus: true

    readonly property bool referenceLayout: width >= 1500 && height >= 800
    readonly property bool narrowLayout: width < 1000
    readonly property real mainWidth: referenceLayout ? 1300
        : narrowLayout ? width : Math.max(680, width - 320)
    readonly property real responsiveContentHeight: narrowLayout ? 720
        : Math.max(height, 660)
    property bool inspectorExpanded: false
    property int actionRevision: 0

    function textInputHasFocus() {
        const active = page.Window.window ? page.Window.window.activeFocusItem : null
        return active && (active.inputMethodComposing !== undefined
            || active.selectedText !== undefined)
    }
    function splitAtPlayhead() {
        AudioEditorController.triggerAction("editor.split")
    }
    function timeTextFromFrames(frames, includeMillis) {
        const sampleRate = Math.max(1, AudioEditorController.sampleRate)
        const totalMilliseconds = Math.max(0,
            Math.round(frames * 1000 / sampleRate))
        const hours = Math.floor(totalMilliseconds / 3600000)
        const minutes = Math.floor(totalMilliseconds % 3600000 / 60000)
        const seconds = Math.floor(totalMilliseconds % 60000 / 1000)
        const millis = totalMilliseconds % 1000
        return (hours > 0 ? String(hours).padStart(2, "0") + ":" : "")
            + String(minutes).padStart(2, "0") + ":"
            + String(seconds).padStart(2, "0")
            + (includeMillis ? "." + String(millis).padStart(3, "0") : "")
    }

    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.playPause()
    }
    Shortcut {
        sequence: "Ctrl+1"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.setActiveTool("select")
    }
    Shortcut {
        sequence: "Ctrl+2"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.setActiveTool("scissors")
    }
    Shortcut {
        sequence: "S"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: page.splitAtPlayhead()
    }
    Shortcut {
        sequence: "Ctrl+B"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: page.splitAtPlayhead()
    }
    Shortcut {
        sequence: "Delete"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.triggerAction("editor.deleteSelection")
    }
    Shortcut {
        sequence: "Ctrl+C"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.triggerAction("editor.copy")
    }
    Shortcut {
        sequence: "Ctrl+X"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.triggerAction("editor.cut")
    }
    Shortcut {
        sequence: "Ctrl+V"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.triggerAction("editor.paste")
    }
    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.triggerAction("editor.undo")
    }
    Shortcut {
        sequence: "Ctrl+Y"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
        onActivated: AudioEditorController.triggerAction("editor.redo")
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            AudioEditorController.clearTransientState()
            inspectorExpanded = false
            event.accepted = true
        }
    }

    FileDialog {
        id: openDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("音频或工程 (*.wav *.flac *.mp3 *.aac *.m4a *.ogg *.opus *.wma *.agproj)")]
        onAccepted: selectedFile.toString().toLowerCase().endsWith(".agproj")
            ? AudioEditorController.openProject(selectedFile)
            : AudioEditorController.openFile(selectedFile)
    }
    FileDialog {
        id: saveProjectDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "agproj"
        nameFilters: [qsTr("AgPlayer 工程 (*.agproj)")]
        onAccepted: AudioEditorController.saveProjectAs(selectedFile)
    }
    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: exportSettingsDialog.currentFormat.extension || "wav"
        nameFilters: [exportSettingsDialog.currentFormat.filter
            || qsTr("WAV 音频 (*.wav)")]
        onAccepted: AudioEditorController.exportTo(
            selectedFile, exportRangeBox.currentIndex === 1,
            exportSettingsDialog.currentFormat.codec || "",
            exportSampleRateBox.currentValue,
            exportChannelBox.currentValue,
            exportBitRateBox.currentValue,
            exportSettingsDialog.currentFormat.supportsMetadata === true,
            true, 80)
    }
    Dialog {
        id: exportSettingsDialog
        objectName: "audioEditorExportDialog"
        title: qsTr("导出音频")
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, page.width - 32)
        height: Math.min(390, page.height - 32)
        padding: 20
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var formats: AudioEditorController.exportFormats
        readonly property var currentFormat: formats.length > 0
            ? formats[Math.max(0, exportFormatBox.currentIndex)]
            : ({ extension: "wav", codec: "", filter: qsTr("WAV 音频 (*.wav)"),
                 supportsMetadata: false })
        onAccepted: exportDialog.open()
        contentItem: GridLayout {
            columns: 2
            rowSpacing: 10
            columnSpacing: 14
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
            }
            Label { text: qsTr("采样率") }
            ComboBox {
                id: exportSampleRateBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [{text: qsTr("保持原始"), value: 0},
                        {text: "44.1 kHz", value: 44100},
                        {text: "48 kHz", value: 48000}]
            }
            Label { text: qsTr("声道") }
            ComboBox {
                id: exportChannelBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [{text: qsTr("保持原始"), value: 0},
                        {text: qsTr("单声道"), value: 1},
                        {text: qsTr("立体声"), value: 2}]
            }
            Label { text: qsTr("目标码率") }
            ComboBox {
                id: exportBitRateBox
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [{text: qsTr("自动"), value: 0},
                        {text: "192 kbps", value: 192000},
                        {text: "320 kbps", value: 320000}]
            }
        }
        background: Rectangle {
            color: "#071a2d"
            border.color: "#294662"
            radius: 8
        }
    }
    Dialog {
        id: discardDialog
        title: qsTr("舍弃未保存更改？")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: AudioEditorController.confirmDiscardAndOpen()
        onRejected: AudioEditorController.cancelDiscardAndOpen()
        Label { text: qsTr("当前工程包含未保存更改。") }
    }
    Connections {
        target: AudioEditorController
        function onOpenRequested() { openDialog.open() }
        function onSaveProjectAsRequested() { saveProjectDialog.open() }
        function onSaveAsRequested() { exportSettingsDialog.open() }
        function onExportRequested() { exportSettingsDialog.open() }
        function onDiscardConfirmationRequested() { discardDialog.open() }
        function onNewRecordingRequested() {
            AudioEditorController.startRecordingToTemporaryFile(
                AudioEditorController.recordingDeviceId,
                AudioEditorController.recordingSampleRate,
                AudioEditorController.recordingChannels,
                AudioEditorController.recordingMonitor, false)
        }
    }
    Connections {
        target: AudioEditorController.actions
        function onDataChanged() { page.actionRevision += 1 }
    }

    Flickable {
        id: mainColumn
        objectName: "editorMainColumn"
        x: 0
        y: 0
        width: page.mainWidth
        height: page.height
        contentWidth: width
        contentHeight: page.referenceLayout ? 849
            : page.responsiveContentHeight
        clip: true
        interactive: !page.referenceLayout && contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: mainSurface
            width: mainColumn.width
            height: mainColumn.contentHeight

            EditorCommandBar {
                id: commandBar
                objectName: "editorCommandBar"
                x: 12
                y: page.referenceLayout ? 13 : 8
                width: mainSurface.width - 22
                height: page.referenceLayout ? 61 : 56
                onImportRequested: openDialog.open()
                onSaveProjectRequested: AudioEditorController.save()
            }

            FileSummaryBar {
                objectName: "fileSummaryBar"
                x: 12
                y: page.referenceLayout ? 88 : 72
                width: mainSurface.width - 22
                height: page.referenceLayout ? 48 : 44
            }

            Rectangle {
                objectName: "editorTimelineWorkspace"
                x: 12
                y: page.referenceLayout ? 152 : 124
                width: mainSurface.width - 24
                height: page.referenceLayout ? 364 : 276
                color: "#041628"
                border.color: "#23415d"
                border.width: 1
                radius: 5
            }

            Rectangle {
                id: trackHeader
                objectName: "editorTrackHeader"
                x: 12
                y: page.referenceLayout ? 202 : 168
                width: page.referenceLayout ? 96 : 84
                height: page.referenceLayout ? 284 : 204
                color: "#071a2d"
                border.color: "#23415d"
                border.width: 1
                radius: 5
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 9
                    Label {
                        text: AudioEditorController.channels === 1
                            ? qsTr("单声道") : qsTr("立体声")
                        color: "#f4f8ff"
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignHCenter
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        Button { text: "M"; enabled: false; Layout.preferredWidth: 31 }
                        Button { text: "S"; enabled: false; Layout.preferredWidth: 31 }
                    }
                    Slider {
                        orientation: Qt.Vertical
                        from: -24
                        to: 12
                        value: 0
                        enabled: false
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "0.0 dB"
                        color: "#f4f8ff"
                        font.pixelSize: 11
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            Rectangle {
                id: ruler
                objectName: "editorTimeRuler"
                x: page.referenceLayout ? 118 : trackHeader.x + trackHeader.width + 10
                y: page.referenceLayout ? 152 : 124
                width: mainSurface.width - x - 15
                height: page.referenceLayout ? 50 : 44
                color: "#06182a"
                border.color: "#23415d"
                Repeater {
                    model: 9
                    Item {
                        required property int index
                        x: index * ruler.width / 8
                        width: 1
                        height: ruler.height
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: 1
                            height: 10
                            color: "#45627c"
                        }
                        Text {
                            x: index === 8 ? -width : 4
                            y: 11
                            text: page.timeTextFromFrames(
                                AudioEditorController.viewport.visibleStartFrame
                                + AudioEditorController.viewport.visibleFrameCount
                                    * index / 8, false)
                            color: "#c8d5e3"
                            font.pixelSize: 11
                        }
                    }
                }
            }

            EditorWaveformCanvas {
                id: waveformCanvas
                objectName: "editorWaveformCanvas"
                x: ruler.x
                y: page.referenceLayout ? 202 : 168
                width: ruler.width
                height: page.referenceLayout ? 284 : 204
            }

            Slider {
                id: timelineScrollbar
                objectName: "editorTimelineScrollbar"
                x: ruler.x
                y: page.referenceLayout ? 500 : 378
                width: ruler.width
                height: 16
                from: 0
                to: Math.max(0, AudioEditorController.totalFrames
                    - AudioEditorController.viewport.visibleFrameCount)
                value: AudioEditorController.viewport.visibleStartFrame
                enabled: to > 0
                onMoved: AudioEditorController.viewport.setVisibleRange(
                    Math.round(value),
                    Math.round(value)
                        + AudioEditorController.viewport.visibleFrameCount)
                background: Rectangle {
                    x: timelineScrollbar.leftPadding
                    y: timelineScrollbar.topPadding
                    width: timelineScrollbar.availableWidth
                    height: 6
                    radius: 3
                    color: "#274159"
                    Rectangle {
                        x: timelineScrollbar.visualPosition
                            * (parent.width - width)
                        width: Math.max(42,
                            parent.width
                            * AudioEditorController.viewport.overviewWidthRatio)
                        height: parent.height
                        radius: 3
                        color: "#7d8c9b"
                    }
                }
                handle: Item { width: 0; height: 0 }
            }

            Rectangle {
                id: recordingTransport
                objectName: "editorRecordingTransport"
                x: 12
                y: page.referenceLayout ? 527 : 410
                width: page.referenceLayout ? 556
                    : Math.max(310, mainSurface.width * 0.43)
                height: page.referenceLayout ? 130 : 110
                color: "#071a2d"
                border.color: "#23415d"
                border.width: 1
                radius: 6
                Label {
                    x: 18; y: 12
                    text: qsTr("录音控制")
                    color: "#f4f8ff"
                    font.pixelSize: 14
                    font.bold: true
                }
                RowLayout {
                    x: 114
                    y: 28
                    width: parent.width - 136
                    height: 78
                    spacing: 14
                    RoundButton {
                        objectName: "recordingMicrophoneButton"
                        Layout.preferredWidth: 62
                        Layout.preferredHeight: 62
                        icon.source: Theme.icon("music-2-line")
                        icon.color: "#f4f8ff"
                        enabled: !AudioEditorController.busy
                        background: Rectangle {
                            radius: width / 2
                            color: "#0a2138"
                            border.color: "#294662"
                            border.width: 1
                        }
                        onClicked: AudioEditorController.startRecordingToTemporaryFile(
                            AudioEditorController.recordingDeviceId,
                            AudioEditorController.recordingSampleRate,
                            AudioEditorController.recordingChannels,
                            AudioEditorController.recordingMonitor,
                            AudioEditorController.hasDocument)
                    }
                    RoundButton {
                        objectName: "recordingToggleButton"
                        Layout.preferredWidth: 62
                        Layout.preferredHeight: 62
                        enabled: AudioEditorController.recording
                        onClicked: AudioEditorController.recordingPaused
                            ? AudioEditorController.resumeRecording()
                            : AudioEditorController.pauseRecording()
                        contentItem: Rectangle {
                            anchors.centerIn: parent
                            width: 34; height: 34; radius: 17
                            color: "#ff3d4f"
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: "#0a2138"
                            border.color: "#294662"
                            border.width: 1
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            text: page.timeTextFromFrames(
                                AudioEditorController.recordingFrames, false)
                            color: "#f4f8ff"
                            font.pixelSize: 24
                        }
                        Label {
                            text: AudioEditorController.recording
                                ? (AudioEditorController.recordingPaused
                                    ? qsTr("录音已暂停") : qsTr("正在录音"))
                                : qsTr("准备录音")
                            color: "#b5c8da"
                            font.pixelSize: 13
                        }
                    }
                    Button {
                        visible: AudioEditorController.recording
                        text: qsTr("停止")
                        onClicked: AudioEditorController.stopRecording()
                    }
                }
            }

            Rectangle {
                id: playbackTransport
                objectName: "editorPlaybackTransport"
                x: page.referenceLayout ? 580
                    : recordingTransport.x + recordingTransport.width + 12
                y: recordingTransport.y
                width: mainSurface.width - x - 12
                height: recordingTransport.height
                color: "#071a2d"
                border.color: "#23415d"
                border.width: 1
                radius: 6
                Label {
                    x: 18; y: 12
                    text: qsTr("播放控制")
                    color: "#f4f8ff"
                    font.pixelSize: 14
                    font.bold: true
                }
                RowLayout {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 10
                    spacing: 18
                    Button {
                        icon.source: Theme.icon("skip-back-fill")
                        enabled: AudioEditorController.hasDocument
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: "#0a2138"
                            border.color: "#294662"; border.width: 1
                        }
                        onClicked: AudioEditorController.seekFrame(0)
                    }
                    Button {
                        icon.source: Theme.icon("arrow-go-back-line")
                        enabled: AudioEditorController.hasDocument
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: "#0a2138"
                            border.color: "#294662"; border.width: 1
                        }
                        onClicked: AudioEditorController.seekFrame(Math.max(0,
                            AudioEditorController.playheadFrame
                                - AudioEditorController.sampleRate * 5))
                    }
                    RoundButton {
                        objectName: "editorPrimaryPlayButton"
                        icon.source: Theme.icon(AudioEditorController.playing
                            ? "pause-fill" : "play-fill")
                        icon.color: "#f4f8ff"
                        enabled: AudioEditorController.hasDocument
                        Layout.preferredWidth: 82; Layout.preferredHeight: 82
                        onClicked: AudioEditorController.playPause()
                        background: Rectangle {
                            radius: width / 2
                            color: "#10253a"
                            border.color: "#00e676"
                            border.width: 3
                        }
                    }
                    Button {
                        icon.source: Theme.icon("skip-forward-fill")
                        enabled: AudioEditorController.hasDocument
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: "#0a2138"
                            border.color: "#294662"; border.width: 1
                        }
                        onClicked: AudioEditorController.seekFrame(Math.min(
                            AudioEditorController.totalFrames,
                            AudioEditorController.playheadFrame
                                + AudioEditorController.sampleRate * 5))
                    }
                    Button {
                        icon.source: Theme.icon("checkbox-blank-line")
                        enabled: AudioEditorController.playing
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: "#0a2138"
                            border.color: "#294662"; border.width: 1
                        }
                        onClicked: AudioEditorController.stopPlayback()
                    }
                }
            }

            Rectangle {
                id: shortcutCard
                objectName: "editorShortcutCard"
                x: 12
                y: page.referenceLayout ? 667 : 532
                width: mainSurface.width - 24
                height: page.referenceLayout ? 157 : 103
                color: "#071a2d"
                border.color: "#23415d"
                border.width: 1
                radius: 6
                Label {
                    x: 20; y: 14
                    text: qsTr("快捷键与鼠标操作")
                    color: "#f4f8ff"
                    font.pixelSize: 14
                    font.bold: true
                }
                Rectangle { x: 16; y: 48; width: parent.width - 32; height: 1; color: "#34506c" }
                Text {
                    x: 22; y: 62; width: parent.width - 44
                    text: qsTr("空格 = 播放 / 暂停       S 或 Ctrl+B = 在播放头处分割       Delete = 删除片段       Ctrl+C / X / V = 复制 / 剪切 / 粘贴       Ctrl+Z / Y = 撤销 / 重做")
                    color: "#c4d2df"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: page.referenceLayout
                    x: 22; y: 105; width: parent.width - 44
                    text: qsTr("Ctrl+拖动 = 快速复制片段       Ctrl+鼠标滚轮 = 放大 / 缩小时间线       Shift+鼠标滚轮 = 横向滚动       拖拽片段边缘 = 修剪")
                    color: "#9fb1c2"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            EditorStatusBar {
                objectName: "editorStatusBar"
                x: 0
                y: page.referenceLayout ? 824 : mainSurface.height - 25
                width: mainSurface.width
                height: 25
            }
        }
    }

    Rectangle {
        id: inspector
        objectName: "editorInspector"
        x: page.referenceLayout ? 1300
            : page.narrowLayout ? page.width - width : page.mainWidth
        y: 0
        width: page.referenceLayout ? 372 : page.narrowLayout ? 350 : 320
        height: page.height
        visible: !page.narrowLayout || page.inspectorExpanded
        z: page.narrowLayout ? 30 : 2
        color: "#05172a"
        border.color: "#294662"
        border.width: 1

        Flickable {
            id: inspectorScroller
            objectName: "editorInspectorScroller"
            anchors.fill: parent
            anchors.margins: 6
            contentWidth: width
            contentHeight: inspectorGroups.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: inspectorGroups
                width: inspectorScroller.width
                spacing: 6

                Rectangle {
                    id: recordingGroup
                    objectName: "inspectorRecordingGroup"
                    width: parent.width
                    height: 184
                    color: "#071a2d"
                    border.color: "#294662"
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        Label { text: qsTr("A. 录音"); color: "#f4f8ff"; font.bold: true }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            Label { text: qsTr("输入设备"); color: "#c4d2df" }
                            ComboBox {
                                Layout.fillWidth: true
                                model: AudioEditorController.recordingDevices
                                textRole: "name"; valueRole: "id"
                                enabled: count > 0 && !AudioEditorController.recording
                                displayText: count > 0 ? currentText : qsTr("未检测到设备")
                            }
                            Label { text: qsTr("输入电平"); color: "#c4d2df" }
                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0; to: 1
                                value: AudioEditorController.inputLevel
                            }
                            Label { text: qsTr("监听"); color: "#c4d2df" }
                            Switch {
                                checked: AudioEditorController.recordingMonitor
                                enabled: !AudioEditorController.recording
                                onToggled: AudioEditorController.recordingMonitor = checked
                            }
                            Label { text: qsTr("录音格式"); color: "#c4d2df" }
                            Label {
                                text: "WAV · "
                                    + AudioEditorController.recordingSampleRate + " Hz · "
                                    + AudioEditorController.recordingChannels
                                    + qsTr(" 声道")
                                color: "#f4f8ff"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "inspectorTempoGroup"
                    width: parent.width
                    height: 126
                    color: "#071a2d"
                    border.color: "#294662"
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        Label { text: qsTr("B. 速度 / BPM"); color: "#f4f8ff"; font.bold: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("BPM  ") + (AudioEditorController.originalBpm > 0
                                    ? AudioEditorController.originalBpm.toFixed(2) : "--")
                                color: "#c4d2df"; Layout.fillWidth: true
                            }
                            Button {
                                text: qsTr("自动检测BPM")
                                enabled: AudioEditorController.hasDocument
                                onClicked: AudioEditorController.detectBpm()
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("速度"); color: "#c4d2df" }
                            Slider {
                                Layout.fillWidth: true; from: 50; to: 200
                                value: AudioEditorController.speedPercent
                                enabled: AudioEditorController.hasDocument
                                onMoved: AudioEditorController.setSpeedPercent(value)
                            }
                            Label { text: AudioEditorController.speedPercent.toFixed(0) + "%"; color: "#f4f8ff" }
                            Button {
                                text: qsTr("重置")
                                enabled: AudioEditorController.hasDocument
                                onClicked: AudioEditorController.setSpeedPercent(100)
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "inspectorPitchGroup"
                    width: parent.width
                    height: 104
                    color: "#071a2d"
                    border.color: "#294662"
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 8
                        Label { text: qsTr("C. 升调降调"); color: "#f4f8ff"; font.bold: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("半音"); color: "#c4d2df" }
                            Slider {
                                id: pitchSlider
                                Layout.fillWidth: true; from: -12; to: 12; stepSize: 1
                                value: Math.trunc(AudioEditorController.pitchCents / 100)
                                enabled: AudioEditorController.hasDocument
                                onMoved: AudioEditorController.setPitch(Math.round(value), 0)
                            }
                            Label {
                                text: (pitchSlider.value > 0 ? "+" : "")
                                    + pitchSlider.value.toFixed(0)
                                color: "#f4f8ff"
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "inspectorPreservePitchGroup"
                    width: parent.width
                    height: 96
                    color: "#071a2d"
                    border.color: "#294662"
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        Label { text: qsTr("D. 保持音调"); color: "#f4f8ff"; font.bold: true }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("变速时保持音调"); color: "#c4d2df"; Layout.fillWidth: true }
                            Switch {
                                checked: AudioEditorController.keepPitch
                                enabled: AudioEditorController.hasDocument
                                onToggled: AudioEditorController.setKeepPitch(checked)
                            }
                        }
                        Loader {
                            active: AudioEditorController.formantPreservationSupported
                            sourceComponent: RowLayout {
                                objectName: "inspectorFormantRow"
                                Label { text: qsTr("人声保真 / Formant 保护") }
                                Switch {}
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "inspectorExportGroup"
                    width: parent.width
                    height: 268
                    color: "#071a2d"
                    border.color: "#294662"
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 7
                        Label { text: qsTr("E. 导出设置"); color: "#f4f8ff"; font.bold: true }
                        GridLayout {
                            Layout.fillWidth: true; columns: 2; rowSpacing: 6
                            Label { text: qsTr("输出格式"); color: "#c4d2df" }
                            ComboBox {
                                Layout.fillWidth: true
                                model: AudioEditorController.exportFormats
                                textRole: "text"
                                enabled: count > 0
                            }
                            Label { text: qsTr("采样率"); color: "#c4d2df" }
                            ComboBox {
                                Layout.fillWidth: true
                                model: [qsTr("保持原始"), "44.1 kHz", "48 kHz"]
                            }
                            Label { text: qsTr("声道"); color: "#c4d2df" }
                            ComboBox {
                                Layout.fillWidth: true
                                model: [qsTr("保持原始"), qsTr("单声道"), qsTr("立体声")]
                            }
                            Label { text: qsTr("工程"); color: "#c4d2df" }
                            Label {
                                Layout.fillWidth: true
                                text: AudioEditorController.projectPath.length > 0
                                    ? AudioEditorController.projectPath : qsTr("尚未保存")
                                elide: Text.ElideMiddle
                                color: "#f4f8ff"
                            }
                        }
                        Item { Layout.fillHeight: true }
                        Button {
                            objectName: "editorExportButton"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 58
                            text: qsTr("导出音频")
                            icon.source: Theme.icon("download-line")
                            enabled: page.actionRevision >= 0
                                && AudioEditorController.actionEnabled("editor.export")
                            onClicked: AudioEditorController.triggerAction("editor.export")
                            background: Rectangle {
                                color: parent.enabled ? "#0867ed" : "#19334d"
                                radius: 5
                            }
                        }
                    }
                }
            }
        }
    }

    Button {
        id: narrowPlaybackAccess
        objectName: "editorNarrowPlaybackAccess"
        visible: page.narrowLayout
        z: 40
        x: page.width - inspectorAccess.width - width - 24
        y: 72
        width: 76
        height: 40
        text: AudioEditorController.playing ? qsTr("暂停") : qsTr("播放")
        icon.source: Theme.icon(AudioEditorController.playing
            ? "pause-fill" : "play-fill")
        enabled: AudioEditorController.hasDocument
        onClicked: AudioEditorController.playPause()
    }

    Button {
        id: inspectorAccess
        objectName: "editorInspectorAccess"
        visible: page.narrowLayout
        z: 40
        x: page.width - width - 12
        y: 72
        width: 112
        height: 40
        text: page.inspectorExpanded ? qsTr("收起设置") : qsTr("编辑设置")
        onClicked: page.inspectorExpanded = !page.inspectorExpanded
    }
}
