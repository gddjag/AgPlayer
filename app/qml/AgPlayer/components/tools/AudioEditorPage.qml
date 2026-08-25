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

    readonly property bool referenceLayout: width >= 1500 && height >= 800
    readonly property bool narrowLayout: width < 1000
    readonly property real mainWidth: referenceLayout ? 1300
        : narrowLayout ? width : Math.max(680, width - 320)
    readonly property real responsiveContentHeight: narrowLayout ? 720
        : Math.max(height, 660)
    property bool inspectorExpanded: false
    readonly property bool modalInputActive: openDialog.visible
        || saveProjectDialog.visible || exportDirectoryDialog.visible
        || discardDialog.visible
    readonly property var persistedExportSettings:
        AudioEditorController.projectExportSettings
    readonly property var sharedExportSettings: ({
        "codecName": SettingsController.transcodeFormat,
        "sampleRate": SettingsController.transcodeSampleRateHz,
        "channels": SettingsController.transcodeChannels,
        "bitRate": SettingsController.transcodeBitrateKbps * 1000,
        "outputDirectory": SettingsController.defaultOutputDirectory
    })

    function textInputHasFocus() {
        const active = page.Window.window ? page.Window.window.activeFocusItem : null
        return active && (active.inputMethodComposing !== undefined
            || active.selectedText !== undefined)
    }
    function editorShortcutAvailable() {
        return page.visible && !page.textInputHasFocus()
            && !page.modalInputActive
    }
    function splitAtPlayhead() {
        AudioEditorController.triggerAction("editor.split")
    }
    function updateExportSetting(name, value) {
        if (name === "codecName") SettingsController.transcodeFormat = value
        else if (name === "sampleRate") SettingsController.transcodeSampleRateHz = value
        else if (name === "channels") SettingsController.transcodeChannels = value
        else if (name === "bitRate") SettingsController.transcodeBitrateKbps = Math.round(value / 1000)
        else if (name === "outputDirectory") SettingsController.defaultOutputDirectory = value
        const settings = Object.assign({}, page.persistedExportSettings,
                                     page.sharedExportSettings)
        settings[name] = value
        AudioEditorController.setProjectExportSettingsMap(settings)
    }
    function applySharedAudioDefaults() {
        AudioEditorController.setProjectExportSettingsMap(
            Object.assign({}, page.persistedExportSettings,
                          page.sharedExportSettings))
        AudioEditorController.setKeepPitch(SettingsController.keepPitchWhileSpeedChange)
        AudioEditorController.setFormantPreservation(SettingsController.vocalProtection)
    }

    Connections {
        target: SettingsController
        function onTranscodeFormatChanged() { page.applySharedAudioDefaults() }
        function onTranscodeSampleRateHzChanged() { page.applySharedAudioDefaults() }
        function onTranscodeChannelsChanged() { page.applySharedAudioDefaults() }
        function onTranscodeBitrateKbpsChanged() { page.applySharedAudioDefaults() }
        function onDefaultOutputDirectoryChanged() { page.applySharedAudioDefaults() }
        function onKeepPitchWhileSpeedChangeChanged() { page.applySharedAudioDefaults() }
        function onVocalProtectionChanged() { page.applySharedAudioDefaults() }
    }
    Component.onCompleted: applySharedAudioDefaults()
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

    Shortcut {
        objectName: "editorSpaceShortcut"
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.playPause()
    }
    Shortcut {
        sequence: "Ctrl+1"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.setActiveTool("select")
    }
    Shortcut {
        sequence: "Ctrl+2"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.setActiveTool("scissors")
    }
    Shortcut {
        sequence: "S"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: page.splitAtPlayhead()
    }
    Shortcut {
        sequence: "Ctrl+B"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: page.splitAtPlayhead()
    }
    Shortcut {
        sequence: "Delete"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.deleteSelection")
    }
    Shortcut {
        sequence: "Ctrl+C"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.copy")
    }
    Shortcut {
        sequence: "Ctrl+X"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.cut")
    }
    Shortcut {
        sequence: "Ctrl+V"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.paste")
    }
    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.undo")
    }
    Shortcut {
        sequence: "Ctrl+Y"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.redo")
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: openDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.save()
    }
    Shortcut {
        sequence: "Shift+C"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.cropToSelection")
    }
    Shortcut {
        sequence: "I"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.fadeIn")
    }
    Shortcut {
        sequence: "O"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.fadeOut")
    }
    Shortcut {
        sequence: "M"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
        onActivated: AudioEditorController.triggerAction("editor.silenceSelection")
    }
    Shortcut {
        sequence: "Ctrl+N"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
            && AudioEditorController.hasDocument
            && !AudioEditorController.busy
        onActivated: AudioEditorController.reduceNoise()
    }
    Shortcut {
        sequence: "Ctrl+Backspace"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
            && AudioEditorController.hasDocument
            && !AudioEditorController.busy
        onActivated: AudioEditorController.clearDocument()
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
    FolderDialog {
        id: exportDirectoryDialog
        title: qsTr("选择音频导出目录")
        onAccepted: page.updateExportSetting(
            "outputDirectory", selectedFolder.toLocalFile())
    }
    Dialog {
        id: discardDialog
        objectName: "editorDiscardDialog"
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
                objectName: "editorTimelineWorkspace"
                x: 12
                y: page.referenceLayout ? 152 : 124
                width: mainSurface.width - 24
                height: page.referenceLayout ? 364 : 276
                color: Theme.background
                border.color: Theme.border
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
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: 5
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 9
                    Label {
                        text: AudioEditorController.channels === 1
                            ? qsTr("单声道") : qsTr("立体声")
                        color: Theme.primaryText
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
                    Slider {
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
                        color: Theme.primaryText
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
                color: Theme.panel
                border.color: Theme.border
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
                        color: Theme.border
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
                            color: Theme.border
                        }
                        Text {
                            x: index === 8 ? -width : 4
                            y: 11
                            text: page.timeTextFromFrames(
                                AudioEditorController.viewport.frameAtPixel(
                                    index * ruler.width / 8), false)
                            color: Theme.secondaryText
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
                to: Math.max(0,
                    AudioEditorController.viewport.pixelAtFrame(
                        AudioEditorController.totalFrames) - width)
                value: Math.max(0,
                    -AudioEditorController.viewport.pixelAtFrame(0))
                enabled: to > 0
                onMoved: AudioEditorController.viewport.panByPixels(
                    value - Math.max(0,
                        -AudioEditorController.viewport.pixelAtFrame(0)))
                background: Rectangle {
                    x: timelineScrollbar.leftPadding
                    y: timelineScrollbar.topPadding
                    width: timelineScrollbar.availableWidth
                    height: 6
                    radius: 3
                    color: Theme.border
                    Rectangle {
                        x: timelineScrollbar.visualPosition
                            * (parent.width - width)
                        width: Math.max(42,
                            parent.width
                            * AudioEditorController.viewport.overviewWidthRatio)
                        height: parent.height
                        radius: 3
                        color: Theme.secondaryText
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
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: 6
                Label {
                    x: 18; y: 12
                    text: qsTr("录音控制")
                    color: Theme.primaryText
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
                        icon.source: Theme.icon("mic-line")
                        icon.color: Theme.iconPrimary
                        enabled: AudioEditorController.recordingSupported
                            && !AudioEditorController.busy
                        opacity: 1.0
                        Accessible.name: qsTr("选择录音设备")
                        Accessible.role: Accessible.Button
                        onClicked: {
                            recordingDeviceCombo.forceActiveFocus()
                            if (recordingDeviceCombo.count > 0)
                                recordingDeviceCombo.popup.open()
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: Theme.elevated
                            border.color: Theme.border
                            border.width: 1
                        }
                    }
                    RoundButton {
                        id: recordingToggle
                        objectName: "recordingToggleButton"
                        Layout.preferredWidth: 62
                        Layout.preferredHeight: 62
                        enabled: AudioEditorController.recordingSupported
                            && !AudioEditorController.busy
                        opacity: 1.0
                        Accessible.name: !AudioEditorController.recording
                            ? qsTr("开始录音")
                            : AudioEditorController.recordingPaused
                            ? qsTr("继续录音") : qsTr("暂停录音")
                        Accessible.role: Accessible.Button
                        contentItem: Rectangle {
                            objectName: "recordingToggleIndicator"
                            anchors.centerIn: parent
                            width: 34; height: 34; radius: 17
                            color: "#ff3d4f"
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: Theme.elevated
                            border.color: Theme.border
                            border.width: 1
                        }
                        onClicked: {
                            if (!AudioEditorController.recording) {
                                AudioEditorController.startRecordingToTemporaryFile(
                                    recordingDeviceCombo.currentValue || "",
                                    AudioEditorController.recordingSampleRate,
                                    AudioEditorController.recordingChannels,
                                    recordingMonitorSwitch.checked, false)
                            } else if (AudioEditorController.recordingPaused) {
                                AudioEditorController.resumeRecording()
                            } else {
                                AudioEditorController.pauseRecording()
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            objectName: "recordingTimeText"
                            text: AudioEditorController.recordingFrames <= 0
                                ? "00:00:00"
                                : page.recordingTimeText(
                                    AudioEditorController.recordingFrames)
                            color: Theme.primaryText
                            font.pixelSize: 24
                        }
                        Label {
                            objectName: "recordingStateText"
                            text: AudioEditorController.recording
                                ? (AudioEditorController.recordingPaused
                                    ? qsTr("录音已暂停") : qsTr("正在录音"))
                                : qsTr("准备录音")
                            color: Theme.secondaryText
                            font.pixelSize: 13
                        }
                    }
                    Button {
                        visible: AudioEditorController.recording
                        text: qsTr("停止")
                        enabled: AudioEditorController.recordingSupported
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
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: 6
                Label {
                    x: 18; y: 12
                    text: qsTr("播放控制")
                    color: Theme.primaryText
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
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.hasDocument
                        opacity: 1.0
                        Accessible.name: qsTr("跳到开头")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: Theme.elevated
                            border.color: Theme.border; border.width: 1
                        }
                        onClicked: AudioEditorController.seekMs(0)
                    }
                    Button {
                        objectName: "editorPlaybackRewindButton"
                        icon.source: Theme.icon("arrow-go-back-line")
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.hasDocument
                        opacity: 1.0
                        Accessible.name: qsTr("后退五秒")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: Theme.elevated
                            border.color: Theme.border; border.width: 1
                        }
                        onClicked: AudioEditorController.seekMs(
                            Math.max(0, AudioEditorController.positionMs - 5000))
                    }
                    RoundButton {
                        id: primaryPlayButton
                        objectName: "editorPrimaryPlayButton"
                        icon.source: Theme.icon(AudioEditorController.playing
                            ? "pause-fill" : "play-fill")
                        icon.color: Theme.iconPrimary
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.hasDocument
                        opacity: 1.0
                        Accessible.name: AudioEditorController.playing
                            ? qsTr("暂停") : qsTr("播放")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 82; Layout.preferredHeight: 82
                        background: Rectangle {
                            objectName: "editorPrimaryPlayBackground"
                            radius: width / 2
                            color: Theme.elevated
                            border.color: "#00e676"
                            border.width: 3
                        }
                        onClicked: AudioEditorController.playPause()
                    }
                    Button {
                        objectName: "editorPlaybackForwardButton"
                        icon.source: Theme.icon("skip-forward-fill")
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.hasDocument
                        opacity: 1.0
                        Accessible.name: qsTr("跳到末尾")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: Theme.elevated
                            border.color: Theme.border; border.width: 1
                        }
                        onClicked: AudioEditorController.seekMs(
                            AudioEditorController.durationMs)
                    }
                    Button {
                        objectName: "editorPlaybackStopButton"
                        icon.source: Theme.icon("checkbox-blank-line")
                        enabled: AudioEditorController.playbackSupported
                            && AudioEditorController.playing
                        opacity: 1.0
                        Accessible.name: qsTr("停止")
                        Accessible.role: Accessible.Button
                        Layout.preferredWidth: 74; Layout.preferredHeight: 56
                        background: Rectangle {
                            radius: 6; color: Theme.elevated
                            border.color: Theme.border; border.width: 1
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
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: 6
                Label {
                    x: 20; y: 14
                    text: qsTr("快捷键与鼠标操作")
                    color: Theme.primaryText
                    font.pixelSize: 14
                    font.bold: true
                }
                Rectangle { x: 16; y: 48; width: parent.width - 32; height: 1; color: Theme.border }
                Text {
                    objectName: "editorShortcutText"
                    x: 22; y: 62; width: parent.width - 44
                    text: qsTr("空格 = 播放 / 暂停       S = 在播放头处分割       Delete = 删除片段       Ctrl+C / X / V = 复制 / 剪切 / 粘贴       Ctrl+Z / Y = 撤销 / 重做")
                    color: Theme.secondaryText
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
                Text {
                    objectName: "editorShortcutTextSecondRow"
                    visible: page.referenceLayout
                    x: 22; y: 105; width: parent.width - 44
                    text: qsTr("Ctrl+拖动 = 快速复制片段       Ctrl+鼠标滚轮 = 放大 / 缩小时间线       Shift+鼠标滚轮 = 横向滚动       拖拽片段边缘 = 修剪       拖拽右上角 = 调整淡出       双击音量线 = 添加控制点")
                    color: Theme.secondaryText
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
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
        border.color: Theme.border
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
                    color: Theme.panel
                    border.color: Theme.border
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: recordingGroup.collapsed ? 6 : 12
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("A. 录音"); color: Theme.primaryText; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
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
                            Label { text: qsTr("输入设备"); color: Theme.secondaryText }
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
                            Label { text: qsTr("输入电平"); color: Theme.secondaryText }
                            Rectangle {
                                objectName: "inspectorInputMeter"
                                readonly property real level:
                                    AudioEditorController.inputLevel
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
                                            objectName:
                                                "inspectorInputMeterSegment" + index
                                            width: 8; height: 13; radius: 1
                                            color: index < 9 ? "#16e969"
                                                : index < 12 ? "#ffca28" : "#d9364f"
                                            opacity: index / 14
                                                <= parent.parent.level
                                                ? 1.0 : 0.18
                                        }
                                    }
                                }
                                Label {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: parent.level > 0
                                        ? (20 * Math.log(
                                            parent.level)
                                            / Math.LN10).toFixed(1) + " dB"
                                        : "−∞ dB"
                                    color: Theme.primaryText; font.pixelSize: 11
                                }
                            }
                            Label { text: qsTr("监听"); color: Theme.secondaryText }
                            Switch {
                                id: recordingMonitorSwitch
                                objectName: "inspectorMonitorSwitch"
                                checked: AudioEditorController.recordingMonitor
                                enabled: AudioEditorController.recordingSupported
                                    && !AudioEditorController.recording
                            }
                            Label { text: qsTr("录音格式"); color: Theme.secondaryText }
                            ComboBox {
                                objectName: "inspectorRecordingFormat"
                                model: [qsTr("WAV (24-bit, %1 kHz)").arg(
                                    (AudioEditorController.recordingSampleRate
                                        / 1000).toFixed(1))]
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
                    color: Theme.panel
                    border.color: Theme.border
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: tempoGroup.collapsed ? 6 : 12; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("B. 速度 / BPM"); color: Theme.primaryText; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
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
                            Label { text: qsTr("BPM"); color: Theme.secondaryText }
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
                            Label { text: qsTr("速度"); color: Theme.secondaryText }
                            Slider {
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
                                color: Theme.primaryText
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
                    color: Theme.panel
                    border.color: Theme.border
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: pitchGroup.collapsed ? 6 : 12; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("C. 升调降调"); color: Theme.primaryText; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
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
                            Label { text: qsTr("半音"); color: Theme.secondaryText }
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
                            Label { text: "−12"; color: Theme.secondaryText }
                            Slider {
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
                                color: Theme.secondaryText
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
                                color: Theme.primaryText
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
                    color: Theme.panel
                    border.color: Theme.border
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: preservePitchGroup.collapsed ? 6 : 8; spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("D. 保持音调"); color: Theme.primaryText; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
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
                            Label { text: qsTr("变速时保持音调"); color: Theme.secondaryText; Layout.fillWidth: true }
                            Switch {
                                objectName: "inspectorPreservePitchSwitch"
                                checked: SettingsController.keepPitchWhileSpeedChange
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                onToggled: SettingsController.keepPitchWhileSpeedChange = checked
                                Layout.preferredWidth: 44
                                Layout.preferredHeight: 24
                            }
                        }
                        RowLayout {
                            objectName: "inspectorFormantRow"
                            visible: !preservePitchGroup.collapsed
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            Label {
                                text: qsTr("人声保真 / Formant保护")
                                color: Theme.secondaryText
                                Layout.fillWidth: true
                            }
                            Switch {
                                objectName: "inspectorFormantSwitch"
                                checked: SettingsController.vocalProtection
                                enabled: AudioEditorController.formantPreservationSupported
                                    && AudioEditorController.hasDocument
                                onToggled: SettingsController.vocalProtection = checked
                                Layout.preferredWidth: 44
                                Layout.preferredHeight: 24
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
                    color: Theme.panel
                    border.color: Theme.border
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: exportGroup.collapsed ? 6 : 12; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("E. 导出设置"); color: Theme.primaryText; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true }
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
                            Label { text: qsTr("输出格式"); color: Theme.secondaryText }
                            ComboBox {
                                objectName: "editorExportCodec"
                                Layout.columnSpan: 3
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: ["MP3", "FLAC", "WAV", "AAC", "Opus", "OGG", "ALAC", "AIFF"]
                                currentIndex: Math.max(0, model.indexOf(
                                    page.sharedExportSettings.codecName || "MP3"))
                                onActivated: page.updateExportSetting(
                                    "codecName", currentText)
                            }
                            Label { text: qsTr("采样率"); color: Theme.secondaryText }
                            ComboBox {
                                objectName: "editorExportSampleRate"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: ["44.1 kHz", "48 kHz", "96 kHz"]
                                currentIndex: page.sharedExportSettings.sampleRate === 48000
                                    ? 1 : page.sharedExportSettings.sampleRate === 96000
                                    ? 2 : 0
                                onActivated: page.updateExportSetting(
                                    "sampleRate", [44100, 48000, 96000][currentIndex])
                            }
                            Label { text: qsTr("位深"); color: Theme.secondaryText }
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
                            Label { text: qsTr("声道"); color: Theme.secondaryText }
                            ComboBox {
                                objectName: "editorExportChannels"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: [qsTr("单声道"), qsTr("立体声")]
                                currentIndex: page.sharedExportSettings.channels === 1
                                    ? 0 : 1
                                onActivated: page.updateExportSetting(
                                    "channels", currentIndex + 1)
                            }
                            Label { text: qsTr("比特率"); color: Theme.secondaryText }
                            ComboBox {
                                objectName: "editorExportBitRate"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                readonly property string text: displayText
                                model: ["128 kbps", "192 kbps", "256 kbps", "320 kbps"]
                                currentIndex: page.sharedExportSettings.bitRate <= 128000
                                    ? 0 : page.sharedExportSettings.bitRate <= 192000
                                    ? 1 : page.sharedExportSettings.bitRate <= 256000
                                    ? 2 : 3
                                onActivated: page.updateExportSetting(
                                    "bitRate", [128000, 192000, 256000, 320000][currentIndex])
                            }
                            Label { text: qsTr("输出目录"); color: Theme.secondaryText }
                            TextField {
                                objectName: "editorExportDirectory"
                                Layout.columnSpan: 2
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                text: page.sharedExportSettings.outputDirectory || "--"
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
                                color: parent.enabled ? Theme.accent : Theme.border
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
        y: 72
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
        y: 72
        width: 112
        height: 40
        text: page.inspectorExpanded ? qsTr("收起设置") : qsTr("编辑设置")
        onClicked: page.inspectorExpanded = !page.inspectorExpanded
    }
}
