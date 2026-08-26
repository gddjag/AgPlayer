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
    focus: true

    Component.onCompleted: AudioEditorController.refreshRecordingDevices()

    readonly property bool referenceLayout: width >= 1500 && height >= 800
    readonly property bool narrowLayout: width < 1000
    readonly property real narrowActionBandHeight: narrowLayout ? 52 : 0
    readonly property real narrowActionBandY: 124
    readonly property real mainWidth: referenceLayout ? 1300
        : narrowLayout ? width : Math.max(680, width - 320)
    readonly property real responsiveContentHeight: narrowLayout ? 772
        : Math.max(height, 660)
    property bool inspectorExpanded: false
    property string pendingRelinkSourceId: ""
    readonly property var firstProjectIssue:
        AudioEditorController.projectIssues.length > 0
            ? AudioEditorController.projectIssues[0] : null
    readonly property var persistedExportSettings:
        AudioEditorController.projectExportSettings

    function textInputHasFocus() {
        const active = page.Window.window ? page.Window.window.activeFocusItem : null
        return active && (active.inputMethodComposing !== undefined
            || active.selectedText !== undefined)
    }
    function splitAtPlayhead() {
        AudioEditorController.triggerAction("editor.split")
    }
    function updateExportSetting(name, value) {
        const settings = Object.assign({}, page.persistedExportSettings)
        settings[name] = value
        AudioEditorController.setProjectExportSettingsMap(settings)
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
    function recordingTimeText(frames) {
        const sampleRate = Math.max(1, AudioEditorController.sampleRate)
        const totalSeconds = Math.max(0, Math.floor(frames / sampleRate))
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor(totalSeconds % 3600 / 60)
        const seconds = totalSeconds % 60
        return String(hours).padStart(2, "0") + ":"
            + String(minutes).padStart(2, "0") + ":"
            + String(seconds).padStart(2, "0")
    }
    function startOrResumeRecording() {
        if (AudioEditorController.recordingPaused) {
            AudioEditorController.resumeRecording()
            return
        }
        if (!AudioEditorController.recording) {
            AudioEditorController.startRecordingToTemporaryFile(
                recordingDeviceCombo.currentValue || "",
                44100, 2, recordingMonitorSwitch.checked, false)
        }
    }
    function pauseOrResumeRecording() {
        if (AudioEditorController.recordingPaused)
            AudioEditorController.resumeRecording()
        else if (AudioEditorController.recording)
            AudioEditorController.pauseRecording()
    }
    function openRecordingDevicePicker() {
        recordingDeviceCombo.forceActiveFocus()
        if (recordingDeviceCombo.count > 0)
            recordingDeviceCombo.popup.open()
    }

    Shortcut {
        objectName: "editorDeviceShortcut"
        sequence: "Alt+R"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.recordingSupported
            && !AudioEditorController.recording
            && !AudioEditorController.busy
        onActivated: page.openRecordingDevicePicker()
    }
    Shortcut {
        objectName: "editorSpaceShortcut"
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.playPause()
    }
    Shortcut {
        objectName: "editorToStartShortcut"
        sequence: "Home"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.seekMs(0)
    }
    Shortcut {
        objectName: "editorRewindShortcut"
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.seekMs(Math.max(0,
            AudioEditorController.positionMs - 5000))
    }
    Shortcut {
        objectName: "editorForwardShortcut"
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.seekMs(Math.min(
            AudioEditorController.durationMs,
            AudioEditorController.positionMs + 5000))
    }
    Shortcut {
        objectName: "editorStopPlaybackShortcut"
        sequence: "Ctrl+Space"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.stopPlayback()
    }
    Shortcut {
        objectName: "editorRecordShortcut"
        sequence: "R"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.recordingSupported
            && !AudioEditorController.busy
            && (!AudioEditorController.recording
                || AudioEditorController.recordingPaused)
        onActivated: page.startOrResumeRecording()
    }
    Shortcut {
        objectName: "editorPauseRecordingShortcut"
        sequence: "Shift+R"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.recordingSupported
            && AudioEditorController.recording
            && !AudioEditorController.busy
        onActivated: page.pauseOrResumeRecording()
    }
    Shortcut {
        objectName: "editorStopRecordingShortcut"
        sequence: "Ctrl+R"
        context: Qt.WindowShortcut
        enabled: page.visible && !page.textInputHasFocus()
            && AudioEditorController.recordingSupported
            && AudioEditorController.recording
            && !AudioEditorController.busy
        onActivated: AudioEditorController.stopRecording()
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
        id: relinkSourceDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("音频文件 (*.wav *.flac *.mp3 *.aac *.m4a *.ogg *.opus *.wma)")]
        onAccepted: {
            AudioEditorController.relinkProjectSource(
                page.pendingRelinkSourceId, selectedFile)
            page.pendingRelinkSourceId = ""
        }
        onRejected: page.pendingRelinkSourceId = ""
    }
    FolderDialog {
        id: exportDirectoryDialog
        title: qsTr("选择音频导出目录")
        onAccepted: page.updateExportSetting(
            "outputDirectory", selectedFolder.toLocalFile())
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
        function onExportDirectoryRequested() { exportDirectoryDialog.open() }
        function onDiscardConfirmationRequested() { discardDialog.open() }
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
                objectName: "editorOfflineSourceBanner"
                x: 12
                y: page.referenceLayout ? 88 : 72
                width: mainSurface.width - 22
                height: page.referenceLayout ? 48 : 44
                visible: page.firstProjectIssue !== null
                color: Theme.surfacePressed
                border.color: Theme.warning
                radius: 6
                z: 3

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 10
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: qsTr("音频源不可用：")
                            + (page.firstProjectIssue
                                ? page.firstProjectIssue.path : "")
                        color: Theme.warning
                        font.pixelSize: 12
                    }
                    Button {
                        objectName: "editorRelinkSourceButton"
                        text: qsTr("重新定位文件")
                        enabled: !AudioEditorController.busy
                        onClicked: {
                            page.pendingRelinkSourceId = String(
                                page.firstProjectIssue.sourceId)
                            relinkSourceDialog.open()
                        }
                    }
                }
            }

            Rectangle {
                objectName: "editorTimelineWorkspace"
                x: 12
                y: page.referenceLayout ? 152
                    : 124 + page.narrowActionBandHeight
                width: mainSurface.width - 24
                height: page.referenceLayout ? 364 : 276
                color: Theme.editorCanvas
                border.color: Theme.divider
                border.width: 1
                radius: 5
            }

            Rectangle {
                id: trackHeader
                objectName: "editorTrackHeader"
                x: 12
                y: page.referenceLayout ? 202
                    : 168 + page.narrowActionBandHeight
                width: page.referenceLayout ? 96 : 84
                height: page.referenceLayout ? 284 : 204
                color: Theme.surfaceElevated
                border.color: Theme.divider
                border.width: 1
                radius: 5
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 9
                    Label {
                        text: AudioEditorController.channels === 1
                            ? qsTr("（单声道）") : qsTr("（立体声）")
                        color: Theme.textPrimary
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignHCenter
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        Button {
                            objectName: "editorTrackMute"
                            text: "M"
                            checkable: true
                            checked: AudioEditorController.trackMuted
                            enabled: AudioEditorController.hasDocument
                                && !AudioEditorController.busy
                            Layout.preferredWidth: 31
                            onClicked: AudioEditorController.setTrackMuted(checked)
                        }
                        Button {
                            objectName: "editorTrackSolo"
                            text: "S"
                            checkable: true
                            checked: AudioEditorController.trackSolo
                            enabled: AudioEditorController.hasDocument
                                && !AudioEditorController.busy
                            Layout.preferredWidth: 31
                            onClicked: AudioEditorController.setTrackSolo(checked)
                        }
                    }
                    EditorSlider {
                        id: trackGainSlider
                        objectName: "editorTrackGain"
                        orientation: Qt.Vertical
                        from: -60
                        to: 12
                        value: AudioEditorController.trackGainDb
                        enabled: AudioEditorController.hasDocument
                            && !AudioEditorController.busy
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignHCenter
                        onMoved: AudioEditorController.setTrackGainDb(value)
                    }
                    Label {
                        objectName: "editorTrackGainLabel"
                        text: (trackGainSlider.value > 0 ? "+" : "")
                            + trackGainSlider.value.toFixed(1) + " dB"
                        color: Theme.textPrimary
                        font.pixelSize: 11
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            Rectangle {
                id: ruler
                objectName: "editorTimeRuler"
                x: page.referenceLayout ? 118 : trackHeader.x + trackHeader.width + 10
                y: page.referenceLayout ? 152
                    : 124 + page.narrowActionBandHeight
                width: mainSurface.width - x - 15
                height: page.referenceLayout ? 50 : 44
                color: Theme.surface
                border.color: Theme.divider
                Repeater {
                    model: 33
                    Rectangle {
                        required property int index
                        objectName: index % 4 !== 0
                            ? "editorRulerMinorTick" : ""
                        visible: index % 4 !== 0
                        x: index * ruler.width / 32
                        anchors.bottom: parent.bottom
                        width: 1
                        height: index % 2 === 0 ? 7 : 5
                        color: Theme.borderStrong
                    }
                }
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
                            color: Theme.borderStrong
                        }
                        Text {
                            x: index === 8 ? -width : 4
                            y: 11
                            text: page.timeTextFromFrames(
                                AudioEditorController.viewport.frameAtPixel(
                                    index * ruler.width / 8), false)
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                    }
                }
                Item {
                    id: rulerPlayhead
                    objectName: "editorPlayheadRulerMarker"
                    x: AudioEditorController.viewport.pixelAtFrame(
                        AudioEditorController.playheadFrame)
                    y: 0
                    width: 1
                    height: ruler.height
                    visible: AudioEditorController.hasDocument
                        && AudioEditorController.playheadFrame
                            >= AudioEditorController.viewport.visibleStartFrame
                        && AudioEditorController.playheadFrame
                            <= AudioEditorController.viewport.visibleEndFrame
                    z: 4

                    Rectangle {
                        objectName: "editorPlayheadTimeCapsule"
                        property alias text: playheadTimeText.text
                        x: -width / 2
                        y: -7
                        width: playheadTimeText.implicitWidth + 12
                        height: 22
                        radius: 4
                        color: Theme.warning
                        visible: parent.visible
                        Text {
                            id: playheadTimeText
                            anchors.centerIn: parent
                            text: page.timeTextFromFrames(
                                AudioEditorController.playheadFrame, true)
                            color: Theme.textPrimary
                            font.pixelSize: 12
                        }
                    }
                    Rectangle {
                        objectName: "editorPlayheadRulerLine"
                        x: 0
                        y: 27
                        width: 2
                        height: Math.max(0, parent.height - y)
                        color: Theme.warning
                    }
                    Rectangle {
                        objectName: "editorPlayheadRulerCircle"
                        x: -7
                        y: 20
                        width: 14
                        height: 14
                        radius: 7
                        color: Theme.warning
                        border.color: Theme.textPrimary
                        border.width: 2
                    }
                }
            }

            EditorWaveformCanvas {
                id: waveformCanvas
                objectName: "editorWaveformCanvas"
                x: ruler.x
                y: page.referenceLayout ? 202
                    : 168 + page.narrowActionBandHeight
                width: ruler.width
                height: page.referenceLayout ? 284 : 204
            }

            EditorSlider {
                id: timelineScrollbar
                objectName: "editorTimelineScrollbar"
                x: ruler.x
                y: page.referenceLayout ? 500
                    : 378 + page.narrowActionBandHeight
                width: ruler.width
                height: 16
                pointerHitExtent: 16
                from: 0
                to: Math.max(0,
                    AudioEditorController.viewport.pixelAtFrame(
                        AudioEditorController.totalFrames) - width)
                value: Math.max(0,
                    -AudioEditorController.viewport.pixelAtFrame(0))
                enabled: to > 0
                onMoved: AudioEditorController.viewport.panByPixels(
                    value - Math.max(0,
                        -AudioEditorController.viewport.pixelAtFrame(0)))
            }

            Rectangle {
                id: recordingTransport
                objectName: "editorRecordingTransport"
                x: 12
                y: page.referenceLayout ? 527
                    : 410 + page.narrowActionBandHeight
                width: page.referenceLayout ? 556
                    : Math.max(310, mainSurface.width * 0.43)
                height: page.referenceLayout ? 130 : 110
                color: Theme.surfaceElevated
                border.color: Theme.divider
                border.width: 1
                radius: 6
                readonly property real recordingButtonStartX:
                    page.referenceLayout ? 151 : 135
                readonly property real recordingButtonSize:
                    page.referenceLayout ? 74
                        : Math.max(40, Math.min(74,
                            (width - recordingButtonStartX - 36) / 4))
                readonly property real recordingButtonGap:
                    page.referenceLayout ? 28
                        : Math.max(4, (width - recordingButtonStartX
                            - 4 * recordingButtonSize - 12) / 3)
                Label {
                    x: 18; y: 12
                    text: qsTr("录音控制")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
                Label {
                    objectName: "recordingTimeText"
                    x: 18; y: 48
                    text: AudioEditorController.recordingFrames <= 0
                        ? "00:00:00"
                        : page.recordingTimeText(
                            AudioEditorController.recordingFrames)
                    color: Theme.textPrimary
                    font.pixelSize: 24
                }
                Label {
                    objectName: "recordingStateText"
                    x: 18; y: 88
                    text: AudioEditorController.recording
                        ? (AudioEditorController.recordingPaused
                            ? qsTr("录音已暂停") : qsTr("正在录音"))
                        : qsTr("准备录音")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                RoundButton {
                    id: recordingMicrophoneButton
                    objectName: "recordingMicrophoneButton"
                    x: recordingTransport.recordingButtonStartX
                    y: 31
                    width: recordingTransport.recordingButtonSize
                    height: width
                    enabled: AudioEditorController.recordingSupported
                        && !AudioEditorController.recording
                        && !AudioEditorController.busy
                    opacity: 1.0
                    property string toolTipText: qsTr("选择录音设备（Alt+R）")
                    property int toolTipDelay: 500
                    Accessible.name: qsTr("选择录音设备")
                    Accessible.role: Accessible.Button
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: -2
                            width: 8; height: 23; radius: 4
                            color: Theme.success
                        }
                        ThemedIcon {
                            anchors.centerIn: parent
                            width: 44; height: 44
                            source: Theme.icon("mic-line")
                            tint: Theme.textPrimary
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: toolTipDelay
                    ToolTip.text: toolTipText
                    onClicked: page.openRecordingDevicePicker()
                    background: Rectangle {
                        radius: width / 2
                        color: Theme.surface
                        border.color: Theme.borderStrong
                        border.width: 1
                    }
                }
                RoundButton {
                    objectName: "recordingPauseButton"
                    x: page.referenceLayout ? 253
                        : recordingTransport.recordingButtonStartX
                            + recordingTransport.recordingButtonSize
                            + recordingTransport.recordingButtonGap
                    y: 31
                    width: recordingTransport.recordingButtonSize
                    height: width
                    enabled: AudioEditorController.recordingSupported
                        && AudioEditorController.recording
                        && !AudioEditorController.busy
                    opacity: 1.0
                    property string toolTipText: qsTr("暂停 / 继续录音（Shift+R）")
                    property int toolTipDelay: 500
                    Accessible.name: AudioEditorController.recordingPaused
                        ? qsTr("继续录音") : qsTr("暂停录音")
                    Accessible.role: Accessible.Button
                    contentItem: Item {
                        ThemedIcon {
                            anchors.centerIn: parent
                            source: Theme.icon("pause-fill")
                            tint: Theme.textPrimary
                            width: 44; height: 44
                        }
                    }
                    onClicked: page.pauseOrResumeRecording()
                    ToolTip.visible: hovered
                    ToolTip.delay: toolTipDelay
                    ToolTip.text: toolTipText
                    background: Rectangle {
                        radius: width / 2
                        color: Theme.surface
                        border.color: Theme.borderStrong
                        border.width: 1
                    }
                }
                RoundButton {
                    objectName: "recordingRecordButton"
                    x: page.referenceLayout ? 356
                        : recordingTransport.recordingButtonStartX
                            + 2 * (recordingTransport.recordingButtonSize
                                + recordingTransport.recordingButtonGap)
                    y: 31
                    width: recordingTransport.recordingButtonSize
                    height: width
                    enabled: AudioEditorController.recordingSupported
                        && !AudioEditorController.busy
                        && (!AudioEditorController.recording
                            || AudioEditorController.recordingPaused)
                    opacity: 1.0
                    property string toolTipText: qsTr("开始 / 继续录音（R）")
                    property int toolTipDelay: 500
                    Accessible.name: AudioEditorController.recordingPaused
                        ? qsTr("继续录音") : qsTr("开始录音")
                    Accessible.role: Accessible.Button
                    contentItem: Rectangle {
                        color: "transparent"
                        Rectangle {
                            objectName: "recordingToggleIndicator"
                            anchors.centerIn: parent
                            width: 34; height: 34; radius: 17
                            color: Theme.recording
                        }
                    }
                    background: Rectangle {
                        radius: width / 2
                        color: Theme.surface
                        border.color: Theme.borderStrong
                        border.width: 1
                    }
                    onClicked: page.startOrResumeRecording()
                    ToolTip.visible: hovered
                    ToolTip.delay: toolTipDelay
                    ToolTip.text: toolTipText
                }
                RoundButton {
                    objectName: "recordingStopButton"
                    x: page.referenceLayout ? 458
                        : recordingTransport.recordingButtonStartX
                            + 3 * (recordingTransport.recordingButtonSize
                                + recordingTransport.recordingButtonGap)
                    y: 31
                    width: recordingTransport.recordingButtonSize
                    height: width
                    enabled: AudioEditorController.recordingSupported
                        && AudioEditorController.recording
                        && !AudioEditorController.busy
                    opacity: 1.0
                    property string toolTipText: qsTr("停止并保存录音（Ctrl+R）")
                    property int toolTipDelay: 500
                    Accessible.name: qsTr("停止并保存录音")
                    Accessible.role: Accessible.Button
                    contentItem: Item {
                        ThemedIcon {
                            anchors.centerIn: parent
                            source: Theme.icon("stop-fill")
                            tint: Theme.textPrimary
                            width: 42; height: 42
                        }
                    }
                    onClicked: AudioEditorController.stopRecording()
                    ToolTip.visible: hovered
                    ToolTip.delay: toolTipDelay
                    ToolTip.text: toolTipText
                    background: Rectangle {
                        radius: width / 2
                        color: Theme.surface
                        border.color: Theme.borderStrong
                        border.width: 1
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
                color: Theme.surfaceElevated
                border.color: Theme.divider
                border.width: 1
                radius: 6
                Label {
                    x: 18; y: 12
                    text: qsTr("播放控制")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
                RowLayout {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 10
                    spacing: 18
                    Button {
                        objectName: "editorPlaybackToStartButton"
                        icon.source: Theme.icon("skip-back-fill")
                        icon.width: 32
                        icon.height: 32
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.hasDocument
                        opacity: 1.0
                        property string toolTipText: qsTr("跳到开头（Home）")
                        property int toolTipDelay: 500
                        Accessible.name: qsTr("跳到开头")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: Theme.surface
                            border.color: Theme.borderStrong; border.width: 1
                        }
                        onClicked: AudioEditorController.seekMs(0)
                        ToolTip.visible: hovered
                        ToolTip.delay: toolTipDelay
                        ToolTip.text: toolTipText
                    }
                    Button {
                        objectName: "editorPlaybackRewindButton"
                        icon.source: Theme.icon("rewind-fill")
                        icon.width: 32
                        icon.height: 32
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.hasDocument
                        opacity: 1.0
                        property string toolTipText: qsTr("后退 5 秒（←）")
                        property int toolTipDelay: 500
                        Accessible.name: qsTr("后退五秒")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: Theme.surface
                            border.color: Theme.borderStrong; border.width: 1
                        }
                        onClicked: AudioEditorController.seekMs(
                            Math.max(0, AudioEditorController.positionMs - 5000))
                        ToolTip.visible: hovered
                        ToolTip.delay: toolTipDelay
                        ToolTip.text: toolTipText
                    }
                    RoundButton {
                        id: primaryPlayButton
                        objectName: "editorPrimaryPlayButton"
                        icon.source: Theme.icon(AudioEditorController.playing
                            ? "pause-fill" : "play-fill")
                        icon.color: Theme.textPrimary
                        icon.width: 42
                        icon.height: 42
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.hasDocument
                        opacity: 1.0
                        property string toolTipText: qsTr("播放 / 暂停（Space）")
                        property int toolTipDelay: 500
                        Accessible.name: AudioEditorController.playing
                            ? qsTr("暂停") : qsTr("播放")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 82; Layout.preferredHeight: 82
                        background: Rectangle {
                            objectName: "editorPrimaryPlayBackground"
                            radius: width / 2
                            color: Theme.surfaceElevated
                            border.color: Theme.success
                            border.width: 3
                        }
                        onClicked: AudioEditorController.playPause()
                        ToolTip.visible: hovered
                        ToolTip.delay: toolTipDelay
                        ToolTip.text: toolTipText
                    }
                    Button {
                        objectName: "editorPlaybackForwardButton"
                        icon.source: Theme.icon("speed-fill")
                        icon.width: 32
                        icon.height: 32
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.hasDocument
                        opacity: 1.0
                        property string toolTipText: qsTr("前进 5 秒（→）")
                        property int toolTipDelay: 500
                        Accessible.name: qsTr("快进 5 秒")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: Theme.surface
                            border.color: Theme.borderStrong; border.width: 1
                        }
                        onClicked: AudioEditorController.seekMs(Math.min(
                            AudioEditorController.durationMs,
                            AudioEditorController.positionMs + 5000))
                        ToolTip.visible: hovered
                        ToolTip.delay: toolTipDelay
                        ToolTip.text: toolTipText
                    }
                    Button {
                        objectName: "editorPlaybackStopButton"
                        icon.source: Theme.icon("stop-fill")
                        icon.width: 32
                        icon.height: 32
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.playing
                        opacity: 1.0
                        property string toolTipText: qsTr("停止（Ctrl+Space）")
                        property int toolTipDelay: 500
                        Accessible.name: qsTr("停止")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: Theme.surface
                            border.color: Theme.borderStrong; border.width: 1
                        }
                        onClicked: AudioEditorController.stopPlayback()
                        ToolTip.visible: hovered
                        ToolTip.delay: toolTipDelay
                        ToolTip.text: toolTipText
                    }
                }
            }

            Rectangle {
                id: shortcutCard
                objectName: "editorShortcutCard"
                x: 12
                y: page.referenceLayout ? 667
                    : 532 + page.narrowActionBandHeight
                width: mainSurface.width - 24
                height: page.referenceLayout ? 157 : 103
                color: Theme.surfaceElevated
                border.color: Theme.divider
                border.width: 1
                radius: 6
                ThemedIcon {
                    objectName: "editorShortcutKeyboardIcon"
                    x: 20; y: 13
                    width: 24; height: 24
                    source: Theme.icon("keyboard-box-line")
                    tint: Theme.textPrimary
                }
                Label {
                    x: 52; y: 14
                    text: qsTr("快捷键与鼠标操作")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
                Rectangle { x: 16; y: 48; width: parent.width - 32; height: 1; color: Theme.borderStrong }
                RowLayout {
                    id: shortcutFirstRow
                    objectName: "editorShortcutFirstRow"
                    property int groupCount: 5
                    property int dividerCount: 4
                    readonly property var columnWeights: [0.16, 0.19, 0.17, 0.31, 0.17]
                    x: 22; y: 62
                    width: parent.width - 44
                    height: 18
                    spacing: 0
                    Repeater {
                        model: [
                            qsTr("空格 = 播放 / 暂停"),
                            qsTr("S = 在播放头处分割"),
                            qsTr("Delete = 删除片段"),
                            qsTr("Ctrl+C / X / V = 复制 / 剪切 / 粘贴"),
                            qsTr("Ctrl+Z / Y = 撤销 / 重做")
                        ]
                        delegate: Item {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: firstGroupText.implicitWidth + 2
                            Layout.preferredWidth: Math.max(Layout.minimumWidth,
                                shortcutFirstRow.width
                                * shortcutFirstRow.columnWeights[index])
                            Layout.fillHeight: true
                            Text {
                                id: firstGroupText
                                objectName: "editorShortcutFirstGroup_" + index
                                anchors.centerIn: parent
                                text: modelData
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                            Rectangle {
                                objectName: index === 0
                                    ? "editorShortcutDivider"
                                    : "editorShortcutFirstDivider_" + index
                                visible: index < shortcutFirstRow.groupCount - 1
                                x: parent.width - width
                                y: 0; width: 1; height: 18
                                color: Theme.borderStrong
                            }
                        }
                    }
                }
                RowLayout {
                    id: shortcutSecondRow
                    objectName: "editorShortcutSecondRow"
                    visible: page.referenceLayout
                    property int groupCount: 6
                    property int dividerCount: 5
                    readonly property var columnWeights: [0.16, 0.22, 0.18, 0.16, 0.15, 0.17]
                    x: 22; y: 105
                    width: parent.width - 44
                    height: 18
                    spacing: 0
                    Repeater {
                        model: [
                            qsTr("Ctrl+拖动 = 快速复制片段"),
                            qsTr("Ctrl+鼠标滚轮 = 放大 / 缩小时间线"),
                            qsTr("Shift+鼠标滚轮 = 横向滚动"),
                            qsTr("拖拽片段边缘 = 修剪"),
                            qsTr("拖拽右上角 = 调整淡出"),
                            qsTr("双击音量线 = 添加控制点")
                        ]
                        delegate: Item {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: secondGroupText.implicitWidth + 2
                            Layout.preferredWidth: Math.max(Layout.minimumWidth,
                                shortcutSecondRow.width
                                * shortcutSecondRow.columnWeights[index])
                            Layout.fillHeight: true
                            Text {
                                id: secondGroupText
                                objectName: "editorShortcutSecondGroup_" + index
                                anchors.centerIn: parent
                                text: modelData
                                color: Theme.textTertiary
                                font.pixelSize: 11
                            }
                            Rectangle {
                                objectName: index === 0
                                    ? "editorShortcutSecondDivider"
                                    : "editorShortcutSecondRowDivider_" + index
                                visible: index < shortcutSecondRow.groupCount - 1
                                x: parent.width - width
                                y: 0; width: 1; height: 18
                                color: Theme.borderStrong
                            }
                        }
                    }
                }
            }

            EditorStatusBar {
                objectName: "editorStatusBar"
                visible: false
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
        color: Theme.background
        border.color: Theme.borderStrong
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
                    property bool collapsed: false
                    height: collapsed ? 38 : 186
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: recordingGroup.collapsed ? 6 : 12
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("A. 录音"); color: Theme.textPrimary; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
                            ToolButton {
                                objectName: "inspectorRecordingCollapse"
                                Accessible.name: recordingGroup.collapsed ? qsTr("展开录音设置") : qsTr("折叠录音设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                rotation: recordingGroup.collapsed ? 0 : 180
                                Layout.preferredWidth: 28; Layout.preferredHeight: 24
                                onClicked: recordingGroup.collapsed = !recordingGroup.collapsed
                            }
                        }
                        GridLayout {
                            visible: !recordingGroup.collapsed
                            Layout.fillWidth: true
                            columns: 2
                            Label { text: qsTr("输入设备"); color: Theme.textSecondary }
                            ComboBox {
                                id: recordingDeviceCombo
                                objectName: "inspectorRecordingDevice"
                                Layout.fillWidth: true
                                model: AudioEditorController.recordingDevices
                                textRole: "name"; valueRole: "id"
                                enabled: AudioEditorController.recordingSupported
                                    && !AudioEditorController.recording
                                displayText: count > 0 ? currentText
                                    : qsTr("未检测到输入设备")
                                Component.onCompleted: {
                                    for (let index = 0; index < count; ++index) {
                                        if (valueAt(index)
                                                === AudioEditorController.recordingDeviceId) {
                                            currentIndex = index
                                            break
                                        }
                                    }
                                }
                            }
                            Label { text: qsTr("输入电平"); color: Theme.textSecondary }
                            Rectangle {
                                objectName: "inspectorInputMeter"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 18
                                color: "transparent"
                                Row {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Repeater {
                                        model: 14
                                        Rectangle {
                                            required property int index
                                            width: 8; height: 13; radius: 1
                                            color: index < 9 ? Theme.success
                                                : index < 12 ? Theme.warning : Theme.error
                                            opacity: index / 14
                                                <= AudioEditorController.inputLevel
                                                ? 1.0 : 0.18
                                        }
                                    }
                                }
                                Label {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: AudioEditorController.inputLevel > 0
                                        ? (20 * Math.log(
                                            AudioEditorController.inputLevel)
                                            / Math.LN10).toFixed(1) + " dB"
                                        : "−∞ dB"
                                    color: Theme.textPrimary; font.pixelSize: 11
                                }
                            }
                            Label { text: qsTr("监听"); color: Theme.textSecondary }
                            ThemedSwitch {
                                id: recordingMonitorSwitch
                                objectName: "inspectorMonitorSwitch"
                                checked: AudioEditorController.recordingMonitor
                                enabled: AudioEditorController.recordingSupported
                                    && !AudioEditorController.recording
                                checkedColor: Theme.accent
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 22
                            }
                            Label { text: qsTr("录音格式"); color: Theme.textSecondary }
                            ComboBox {
                                objectName: "inspectorRecordingFormat"
                                model: ["WAV (24-bit, 44.1 kHz)"]
                                Layout.fillWidth: true
                                enabled: AudioEditorController.recordingSupported
                                    && !AudioEditorController.recording
                            }
                        }
                    }
                }

                Rectangle {
                    id: tempoGroup
                    objectName: "inspectorTempoGroup"
                    width: parent.width
                    property bool collapsed: false
                    height: collapsed ? 38 : 129
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: tempoGroup.collapsed ? 6 : 12; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("B. 速度 / BPM"); color: Theme.textPrimary; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
                            ToolButton {
                                objectName: "inspectorTempoCollapse"
                                Accessible.name: tempoGroup.collapsed ? qsTr("展开速度设置") : qsTr("折叠速度设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                rotation: tempoGroup.collapsed ? 0 : 180
                                Layout.preferredWidth: 28; Layout.preferredHeight: 24
                                onClicked: tempoGroup.collapsed = !tempoGroup.collapsed
                            }
                        }
                        RowLayout {
                            visible: !tempoGroup.collapsed
                            Layout.fillWidth: true
                            Label { text: qsTr("BPM"); color: Theme.textSecondary }
                            TextField {
                                objectName: "inspectorBpmInput"
                                Layout.fillWidth: true
                                text: AudioEditorController.originalBpm > 0
                                    ? AudioEditorController.originalBpm.toFixed(0) : ""
                                horizontalAlignment: TextInput.AlignHCenter
                                validator: IntValidator { bottom: 20; top: 400 }
                                onEditingFinished: {
                                    const bpm = Number(text)
                                    if (bpm >= 20 && bpm <= 400)
                                        AudioEditorController.setOriginalBpm(bpm)
                                }
                            }
                            Button {
                                objectName: "inspectorDetectBpmButton"
                                text: qsTr("自动检测BPM")
                                enabled: AudioEditorController.bpmDetectionSupported
                                    && AudioEditorController.hasDocument
                                onClicked: AudioEditorController.detectBpm()
                            }
                        }
                        RowLayout {
                            visible: !tempoGroup.collapsed
                            Layout.fillWidth: true
                            Label { text: qsTr("速度"); color: Theme.textSecondary }
                            EditorSlider {
                                id: speedSlider
                                objectName: "inspectorSpeedSlider"
                                Layout.fillWidth: true; from: 0.5; to: 2.0
                                stepSize: 0.01
                                value: AudioEditorController.speedPercent / 100
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                onMoved: AudioEditorController.setSpeedPercent(
                                    value * 100)
                            }
                            Label {
                                objectName: "inspectorSpeedValue"
                                text: speedSlider.value.toFixed(2) + "x"
                                color: Theme.textPrimary
                            }
                            Button {
                                objectName: "inspectorSpeedResetButton"
                                text: qsTr("重置")
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                onClicked: AudioEditorController.setSpeedPercent(100)
                            }
                        }
                    }
                }

                Rectangle {
                    id: pitchGroup
                    objectName: "inspectorPitchGroup"
                    width: parent.width
                    property bool collapsed: false
                    height: collapsed ? 38 : 107
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: pitchGroup.collapsed ? 6 : 12; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("C. 升调降调"); color: Theme.textPrimary; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
                            ToolButton {
                                objectName: "inspectorPitchCollapse"
                                Accessible.name: pitchGroup.collapsed ? qsTr("展开升降调设置") : qsTr("折叠升降调设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                rotation: pitchGroup.collapsed ? 0 : 180
                                Layout.preferredWidth: 28; Layout.preferredHeight: 24
                                onClicked: pitchGroup.collapsed = !pitchGroup.collapsed
                            }
                        }
                        RowLayout {
                            visible: !pitchGroup.collapsed
                            Layout.fillWidth: true
                            Label { text: qsTr("半音"); color: Theme.textSecondary }
                            Button {
                                objectName: "inspectorPitchMinus"
                                text: "−"
                                Layout.preferredWidth: 28
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                    && pitchSlider.value > pitchSlider.from
                                onClicked: {
                                    pitchSlider.value -= 1
                                    AudioEditorController.setPitch(
                                        Math.round(pitchSlider.value), 0)
                                }
                            }
                            Label { text: "−12"; color: Theme.textSecondary }
                            EditorSlider {
                                id: pitchSlider
                                objectName: "inspectorPitchSlider"
                                Layout.fillWidth: true; from: -12; to: 12; stepSize: 1
                                value: Math.trunc(AudioEditorController.pitchCents / 100)
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                onMoved: AudioEditorController.setPitch(
                                    Math.round(value), 0)
                            }
                            Label {
                                text: "+12"
                                color: Theme.textSecondary
                            }
                            Button {
                                objectName: "inspectorPitchPlus"
                                text: "+"
                                Layout.preferredWidth: 28
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                    && pitchSlider.value < pitchSlider.to
                                onClicked: {
                                    pitchSlider.value += 1
                                    AudioEditorController.setPitch(
                                        Math.round(pitchSlider.value), 0)
                                }
                            }
                            Label {
                                objectName: "inspectorPitchValue"
                                text: (pitchSlider.value > 0 ? "+" : "")
                                    + pitchSlider.value.toFixed(0)
                                color: Theme.textPrimary
                            }
                        }
                    }
                }

                Rectangle {
                    id: preservePitchGroup
                    objectName: "inspectorPreservePitchGroup"
                    width: parent.width
                    property bool collapsed: false
                    height: collapsed ? 38 : 104
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: preservePitchGroup.collapsed ? 6 : 8; spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("D. 保持音调"); color: Theme.textPrimary; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
                            ToolButton {
                                objectName: "inspectorPreservePitchCollapse"
                                Accessible.name: preservePitchGroup.collapsed ? qsTr("展开音调保护设置") : qsTr("折叠音调保护设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                rotation: preservePitchGroup.collapsed ? 0 : 180
                                Layout.preferredWidth: 28; Layout.preferredHeight: 24
                                onClicked: preservePitchGroup.collapsed = !preservePitchGroup.collapsed
                            }
                        }
                        RowLayout {
                            visible: !preservePitchGroup.collapsed
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            Label { text: qsTr("变速时保持音调"); color: Theme.textSecondary; Layout.fillWidth: true }
                            ThemedSwitch {
                                objectName: "inspectorPreservePitchSwitch"
                                checked: AudioEditorController.keepPitch
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                onToggled: AudioEditorController.setKeepPitch(checked)
                                checkedColor: Theme.accent
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 22
                            }
                        }
                        RowLayout {
                            objectName: "inspectorFormantRow"
                            visible: !preservePitchGroup.collapsed
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            Label {
                                text: qsTr("人声保真 / Formant保护")
                                color: Theme.textSecondary
                                Layout.fillWidth: true
                            }
                            ThemedSwitch {
                                objectName: "inspectorFormantSwitch"
                                checked: AudioEditorController.formantPreservation
                                enabled: AudioEditorController.formantPreservationSupported
                                    && AudioEditorController.hasDocument
                                onToggled: AudioEditorController.setFormantPreservation(
                                    checked)
                                checkedColor: Theme.accent
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 22
                            }
                        }
                    }
                }

                Rectangle {
                    id: exportGroup
                    objectName: "inspectorExportGroup"
                    width: parent.width
                    property bool collapsed: false
                    height: collapsed ? 38 : 276
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: exportGroup.collapsed ? 6 : 12; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("E. 导出设置"); color: Theme.textPrimary; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
                            ToolButton {
                                objectName: "inspectorExportCollapse"
                                Accessible.name: exportGroup.collapsed ? qsTr("展开导出设置") : qsTr("折叠导出设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                rotation: exportGroup.collapsed ? 0 : 180
                                Layout.preferredWidth: 28; Layout.preferredHeight: 24
                                onClicked: exportGroup.collapsed = !exportGroup.collapsed
                            }
                        }
                        GridLayout {
                            visible: !exportGroup.collapsed
                            Layout.fillWidth: true; columns: 4
                            columnSpacing: 6; rowSpacing: 6
                            Label { text: qsTr("输出格式"); color: Theme.textSecondary }
                            ComboBox {
                                objectName: "editorExportCodec"
                                Layout.columnSpan: 3
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: ["WAV", "FLAC", "MP3", "AAC"]
                                currentIndex: Math.max(0, model.indexOf(
                                    page.persistedExportSettings.codecName || "WAV"))
                                onActivated: page.updateExportSetting(
                                    "codecName", currentText)
                            }
                            Label { text: qsTr("采样率"); color: Theme.textSecondary }
                            ComboBox {
                                objectName: "editorExportSampleRate"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: ["44.1 kHz", "48 kHz", "96 kHz"]
                                currentIndex: page.persistedExportSettings.sampleRate === 48000
                                    ? 1 : page.persistedExportSettings.sampleRate === 96000
                                    ? 2 : 0
                                onActivated: page.updateExportSetting(
                                    "sampleRate", [44100, 48000, 96000][currentIndex])
                            }
                            Label { text: qsTr("位深"); color: Theme.textSecondary }
                            ComboBox {
                                objectName: "editorExportBitDepth"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: ["16-bit", "24-bit", "32-bit"]
                                currentIndex: page.persistedExportSettings.bitDepth === 16
                                    ? 0 : page.persistedExportSettings.bitDepth === 32
                                    ? 2 : 1
                                onActivated: page.updateExportSetting(
                                    "bitDepth", [16, 24, 32][currentIndex])
                            }
                            Label { text: qsTr("声道"); color: Theme.textSecondary }
                            ComboBox {
                                objectName: "editorExportChannels"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: [qsTr("单声道"), qsTr("立体声")]
                                currentIndex: page.persistedExportSettings.channels === 1
                                    ? 0 : 1
                                onActivated: page.updateExportSetting(
                                    "channels", currentIndex + 1)
                            }
                            Label { text: qsTr("比特率"); color: Theme.textSecondary }
                            ComboBox {
                                objectName: "editorExportBitRate"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: ["128 kbps", "192 kbps", "256 kbps", "320 kbps"]
                                currentIndex: page.persistedExportSettings.bitRate <= 128000
                                    ? 0 : page.persistedExportSettings.bitRate <= 192000
                                    ? 1 : page.persistedExportSettings.bitRate <= 256000
                                    ? 2 : 3
                                onActivated: page.updateExportSetting(
                                    "bitRate", [128000, 192000, 256000, 320000][currentIndex])
                            }
                            Label { text: qsTr("输出目录"); color: Theme.textSecondary }
                            TextField {
                                objectName: "editorExportDirectory"
                                Layout.columnSpan: 2
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                text: page.persistedExportSettings.outputDirectory || "--"
                                readOnly: true
                                selectByMouse: true
                            }
                            Button {
                                objectName: "editorExportBrowseButton"
                                Layout.preferredHeight: 32
                                text: qsTr("浏览")
                                onClicked: exportDirectoryDialog.open()
                            }
                        }
                        Item { Layout.fillHeight: true }
                        Button {
                            objectName: "editorExportButton"
                            visible: !exportGroup.collapsed
                            Layout.fillWidth: true
                            Layout.preferredHeight: 58
                            text: qsTr("导出音频")
                            icon.source: Theme.icon("download-line")
                            enabled: AudioEditorController.exportSupported
                                && AudioEditorController.hasDocument
                                && AudioEditorController.actionEnabled("editor.export")
                            Accessible.name: text
                            Accessible.role: Accessible.Button
                            background: Rectangle {
                                color: parent.enabled ? Theme.accentPressed : Theme.disabled
                                radius: 5
                            }
                            onClicked: AudioEditorController.exportToConfiguredDirectory()
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
        y: page.narrowActionBandY
        width: 76
        height: 40
        text: AudioEditorController.playing ? qsTr("暂停") : qsTr("播放")
        icon.source: Theme.icon(AudioEditorController.playing
            ? "pause-fill" : "play-fill")
        enabled: AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        opacity: 1.0
        Accessible.name: text
        Accessible.role: Accessible.Button
        onClicked: AudioEditorController.playPause()
    }

    Button {
        id: inspectorAccess
        objectName: "editorInspectorAccess"
        visible: page.narrowLayout
        z: 40
        x: page.width - width - 12
        y: page.narrowActionBandY
        width: 112
        height: 40
        text: page.inspectorExpanded ? qsTr("收起设置") : qsTr("编辑设置")
        onClicked: page.inspectorExpanded = !page.inspectorExpanded
    }
}
