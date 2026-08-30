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

    readonly property bool narrowLayout: width < 1000
    readonly property real narrowActionBandHeight: narrowLayout ? 52 : 0
    readonly property real narrowActionBandY: 124
    readonly property real inspectorWidth: narrowLayout ? 350
        : Math.max(320, Math.min(372, width - 1328))
    readonly property real mainWidth: narrowLayout ? width
        : width - inspectorWidth
    readonly property real responsiveContentHeight: narrowLayout ? 772
        : Math.max(height, 660)
    readonly property real roomyLayoutFactor: narrowLayout ? 0
        : Math.max(0, Math.min(1,
            (responsiveContentHeight - 660) / 162))
    property bool inspectorExpanded: false
    property bool controlModifierHeld: false
    property bool pendingExportAfterDirectory: false
    property string pendingRelinkSourceId: ""
    readonly property bool modalInputActive: openDialog.visible
        || saveProjectDialog.visible || relinkSourceDialog.visible
        || exportDirectoryDialog.visible || discardDialog.visible
    readonly property var firstProjectIssue:
        AudioEditorController.projectIssues.length > 0
            ? AudioEditorController.projectIssues[0] : null
    readonly property var persistedExportSettings:
        AudioEditorController.projectExportSettings
    onPersistedExportSettingsChanged: Qt.callLater(function() {
        page.ensureExportSettingsConsistent()
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
    function interpolateLayout(compactValue, referenceValue) {
        return compactValue
            + (referenceValue - compactValue) * page.roomyLayoutFactor
    }
    function seekPreviousSegment() {
        const events = AudioEditorController.timelineEventViews
        const playhead = AudioEditorController.playheadFrame
        let target = 0
        for (let index = 0; index < events.length; ++index) {
            const start = Number(events[index].timelineStart)
            if (start >= playhead)
                break
            target = start
        }
        return AudioEditorController.seekFrame(target)
    }
    function seekNextSegment() {
        const events = AudioEditorController.timelineEventViews
        const playhead = AudioEditorController.playheadFrame
        for (let index = 0; index < events.length; ++index) {
            const start = Number(events[index].timelineStart)
            if (start > playhead)
                return AudioEditorController.seekFrame(start)
        }
        return AudioEditorController.seekFrame(AudioEditorController.totalFrames)
    }
    function updateExportSetting(name, value) {
        const settings = Object.assign({}, page.persistedExportSettings)
        settings[name] = value
        AudioEditorController.setProjectExportSettingsMap(settings)
    }
    function normalizedExportCodec() {
        return String(page.persistedExportSettings.codecName || "WAV")
            .toUpperCase()
    }
    function defaultBitRateForCodec(codec) {
        return codec === "MP3" ? 320000 : codec === "AAC" ? 256000 : 0
    }
    function selectExportCodec(codec) {
        const normalized = String(codec || "WAV").toUpperCase()
        const settings = Object.assign({}, page.persistedExportSettings)
        const previous = String(settings.codecName || "WAV").toUpperCase()
        settings.codecName = normalized
        if ((normalized === "MP3" || normalized === "AAC")
                && (previous !== normalized
                    || Number(settings.bitRate || 0) <= 0)) {
            settings.bitRate = defaultBitRateForCodec(normalized)
        }
        AudioEditorController.setProjectExportSettingsMap(settings)
    }
    function ensureExportSettingsConsistent() {
        const normalized = normalizedExportCodec()
        const bitRate = Number(page.persistedExportSettings.bitRate || 0)
        if ((normalized === "MP3" || normalized === "AAC")
                && bitRate <= 0) {
            selectExportCodec(normalized)
        }
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
        objectName: "editorToStartShortcut"
        sequence: "Home"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.seekMs(0)
    }
    Shortcut {
        objectName: "editorRewindShortcut"
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.seekMs(Math.max(0,
            AudioEditorController.positionMs - 5000))
    }
    Shortcut {
        objectName: "editorForwardShortcut"
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: page.editorShortcutAvailable()
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
        enabled: page.editorShortcutAvailable()
            && AudioEditorController.playbackSupported
            && AudioEditorController.hasDocument
        onActivated: AudioEditorController.stopPlayback()
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

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Control) {
            controlModifierHeld = true
            return
        }
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
        onAccepted: {
            page.updateExportSetting(
                "outputDirectory", selectedFolder.toLocalFile())
            const continueExport = page.pendingExportAfterDirectory
            page.pendingExportAfterDirectory = false
            if (continueExport) {
                Qt.callLater(function() {
                    AudioEditorController.exportToConfiguredDirectory()
                })
            }
        }
        onRejected: page.pendingExportAfterDirectory = false
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
        function onExportDirectoryRequested() {
            page.pendingExportAfterDirectory = true
            exportDirectoryDialog.open()
        }
        function onDiscardConfirmationRequested() { discardDialog.open() }
        function onProjectChanged() {
            Qt.callLater(function() {
                page.ensureExportSettingsConsistent()
            })
        }
    }
    Component.onCompleted: Qt.callLater(function() {
        page.ensureExportSettingsConsistent()
    })
    DropArea {
        id: editorAudioDropArea
        objectName: "editorAudioDropArea"
        anchors.fill: parent
        z: 100
        onDropped: function(drop) {
            if (drop.urls.length !== 1) {
                if (typeof AudioEditorController.openDroppedUrls === "function")
                    AudioEditorController.openDroppedUrls(drop.urls)
                return
            }
            if (typeof AudioEditorController.openDroppedUrls === "function")
                AudioEditorController.openDroppedUrls(drop.urls)
            else
                AudioEditorController.openFile(drop.urls[0])
        }
    }
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Control)
            controlModifierHeld = false
    }
    Flickable {
        id: mainColumn
        objectName: "editorMainColumn"
        x: 0
        y: 0
        width: page.mainWidth
        height: page.height
        contentWidth: width
        contentHeight: page.responsiveContentHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: mainSurface
            width: mainColumn.width
            height: mainColumn.contentHeight

            EditorCommandBar {
                id: commandBar
                objectName: "editorCommandBar"
                x: 12
                y: page.interpolateLayout(8, 13)
                width: mainSurface.width - 22
                height: page.interpolateLayout(56, 64)
                onImportRequested: openDialog.open()
                onSaveProjectRequested: AudioEditorController.save()
            }

            FileSummaryBar {
                objectName: "fileSummaryBar"
                x: 12
                y: page.interpolateLayout(72, 88)
                width: mainSurface.width - 22
                height: page.interpolateLayout(44, 52)
            }

            Rectangle {
                objectName: "editorOfflineSourceBanner"
                x: 12
                y: page.interpolateLayout(72, 88)
                width: mainSurface.width - 22
                height: page.interpolateLayout(44, 52)
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
                        focusPolicy: Qt.TabFocus
                        Keys.onSpacePressed: function(event) { event.accepted = true }
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
                y: page.interpolateLayout(124, 152)
                    + page.narrowActionBandHeight
                width: mainSurface.width - 24
                height: page.interpolateLayout(276, 451)
                color: Theme.editorCanvas
                border.color: Theme.divider
                border.width: 1
                radius: 5
            }

            Rectangle {
                id: trackHeader
                objectName: "editorTrackHeader"
                x: 12
                y: page.interpolateLayout(168, 188)
                    + page.narrowActionBandHeight
                width: page.interpolateLayout(84, 96)
                height: page.interpolateLayout(204, 387)
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
                            focusPolicy: Qt.TabFocus
                            Keys.onSpacePressed: function(event) { event.accepted = true }
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
                            focusPolicy: Qt.TabFocus
                            Keys.onSpacePressed: function(event) { event.accepted = true }
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
                        Keys.onSpacePressed: function(event) {
                            event.accepted = true
                        }
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
                x: trackHeader.x + trackHeader.width + 12
                y: page.interpolateLayout(124, 152)
                    + page.narrowActionBandHeight
                width: mainSurface.width - x - 10
                height: page.interpolateLayout(44, 52)
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
                MouseArea {
                    objectName: "editorRulerSelectionInteraction"
                    anchors.fill: parent
                    z: 3
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property double pressFrame: 0
                    property bool selecting: false
                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            waveformCanvas.cancelSelectionPreview()
                            AudioEditorController.clearSelection()
                            return
                        }
                        pressFrame = AudioEditorController.viewport.frameAtPixel(mouse.x)
                        selecting = false
                        waveformCanvas.cancelSelectionPreview()
                        AudioEditorController.clearSelection()
                        AudioEditorController.seekFrame(pressFrame)
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed || (pressedButtons & Qt.LeftButton) === 0)
                            return
                        const frame = AudioEditorController.viewport.frameAtPixel(mouse.x)
                        selecting = frame !== pressFrame
                        if (selecting) waveformCanvas.previewSelection(pressFrame, frame)
                    }
                    onReleased: {
                        if (selecting) waveformCanvas.commitSelection()
                        selecting = false
                    }
                    onCanceled: waveformCanvas.cancelSelectionPreview()
                }
            }

            EditorWaveformCanvas {
                id: waveformCanvas
                objectName: "editorWaveformCanvas"
                controlModifierHeld: page.controlModifierHeld
                x: ruler.x
                y: page.interpolateLayout(168, 188)
                    + page.narrowActionBandHeight
                width: ruler.width
                height: page.interpolateLayout(204, 387)
            }

            EditorSlider {
                id: timelineScrollbar
                objectName: "editorTimelineScrollbar"
                x: ruler.x
                y: page.interpolateLayout(378, 589)
                    + page.narrowActionBandHeight
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
                id: playbackTransport
                objectName: "editorPlaybackTransport"
                x: 12
                y: page.interpolateLayout(410, 618)
                    + page.narrowActionBandHeight
                width: mainSurface.width - 24
                height: page.interpolateLayout(110, 112)
                color: Theme.surfaceElevated
                border.color: Theme.divider
                border.width: 1
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 18

                    ColumnLayout {
                        Layout.preferredWidth: 270
                        Layout.fillHeight: true
                        spacing: 2
                        Item { Layout.fillHeight: true }
                        Label {
                            text: qsTr("当前时间")
                            color: Theme.textTertiary
                            font.pixelSize: 12
                        }
                        Label {
                            objectName: "editorCurrentTime"
                            text: page.timeTextFromFrames(
                                AudioEditorController.playheadFrame, true)
                            color: Theme.accent
                            font.pixelSize: 22
                        }
                        Label {
                            text: qsTr("总时长  ")
                                + page.timeTextFromFrames(
                                    AudioEditorController.totalFrames, true)
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Item { Layout.fillHeight: true }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 84
                        color: Theme.divider
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Math.max(8, Math.min(18,
                            (width - 6 * 74) / 8))

                        ColumnLayout {
                            spacing: 2
                            Button {
                                objectName: "editorPlaybackToStartButton"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                icon.source: Theme.icon("skip-back-fill")
                                icon.width: 32; icon.height: 32
                                enabled: AudioEditorController.playbackSupported
                                    && AudioEditorController.hasDocument
                                opacity: 1.0
                                property string toolTipText: qsTr("上一段（Home）")
                                property int toolTipDelay: 500
                                Accessible.name: qsTr("上一段")
                                Accessible.role: Accessible.Button
                                Layout.preferredWidth: 74
                                Layout.preferredHeight: 56
                                background: Rectangle {
                                    radius: 6; color: Theme.surface
                                    border.color: Theme.borderStrong
                                    border.width: 1
                                }
                                onClicked: page.seekPreviousSegment()
                                ToolTip.visible: hovered
                                ToolTip.delay: toolTipDelay
                                ToolTip.text: toolTipText
                            }
                            Label {
                                text: qsTr("上一段")
                                color: Theme.textSecondary
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }

                        ColumnLayout {
                            spacing: 2
                            Button {
                                objectName: "editorPlaybackRewindButton"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                icon.source: Theme.icon("rewind-fill")
                                icon.width: 32; icon.height: 32
                                enabled: AudioEditorController.playbackSupported
                                    && AudioEditorController.hasDocument
                                opacity: 1.0
                                property string toolTipText: qsTr("后退 5 秒（←）")
                                property int toolTipDelay: 500
                                Accessible.name: qsTr("后退五秒")
                                Accessible.role: Accessible.Button
                                Layout.preferredWidth: 74
                                Layout.preferredHeight: 56
                                background: Rectangle {
                                    radius: 6; color: Theme.surface
                                    border.color: Theme.borderStrong
                                    border.width: 1
                                }
                                onClicked: AudioEditorController.seekMs(
                                    Math.max(0,
                                        AudioEditorController.positionMs - 5000))
                                ToolTip.visible: hovered
                                ToolTip.delay: toolTipDelay
                                ToolTip.text: toolTipText
                            }
                            Label {
                                text: qsTr("后退")
                                color: Theme.textSecondary
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }

                        ColumnLayout {
                            spacing: 0
                            RoundButton {
                                id: primaryPlayButton
                                objectName: "editorPrimaryPlayButton"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                icon.source: Theme.icon(
                                    AudioEditorController.playing
                                        ? "pause-fill" : "play-fill")
                                icon.color: Theme.textPrimary
                                icon.width: 42; icon.height: 42
                                enabled: AudioEditorController.playbackSupported
                                    && AudioEditorController.hasDocument
                                opacity: 1.0
                                property string toolTipText:
                                    qsTr("播放 / 暂停（Space）")
                                property int toolTipDelay: 500
                                Accessible.name: AudioEditorController.playing
                                    ? qsTr("暂停") : qsTr("播放")
                                Accessible.role: Accessible.Button
                                Layout.preferredWidth: 82
                                Layout.preferredHeight: 82
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
                            Label {
                                text: AudioEditorController.playing
                                    ? qsTr("暂停") : qsTr("播放")
                                color: Theme.textPrimary
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }

                        ColumnLayout {
                            spacing: 2
                            Button {
                                objectName: "editorPlaybackForwardButton"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                icon.source: Theme.icon("speed-fill")
                                icon.width: 32; icon.height: 32
                                enabled: AudioEditorController.playbackSupported
                                    && AudioEditorController.hasDocument
                                opacity: 1.0
                                property string toolTipText: qsTr("前进 5 秒（→）")
                                property int toolTipDelay: 500
                                Accessible.name: qsTr("快进 5 秒")
                                Accessible.role: Accessible.Button
                                Layout.preferredWidth: 74
                                Layout.preferredHeight: 56
                                background: Rectangle {
                                    radius: 6; color: Theme.surface
                                    border.color: Theme.borderStrong
                                    border.width: 1
                                }
                                onClicked: AudioEditorController.seekMs(Math.min(
                                    AudioEditorController.durationMs,
                                    AudioEditorController.positionMs + 5000))
                                ToolTip.visible: hovered
                                ToolTip.delay: toolTipDelay
                                ToolTip.text: toolTipText
                            }
                            Label {
                                text: qsTr("前进")
                                color: Theme.textSecondary
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }

                        ColumnLayout {
                            spacing: 2
                            Button {
                                objectName: "editorPlaybackNextButton"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                icon.source: Theme.icon("skip-forward-fill")
                                icon.width: 32; icon.height: 32
                                enabled: AudioEditorController.playbackSupported
                                    && AudioEditorController.hasDocument
                                opacity: 1.0
                                property string toolTipText: qsTr("下一段")
                                property int toolTipDelay: 500
                                Accessible.name: qsTr("下一段")
                                Accessible.role: Accessible.Button
                                Layout.preferredWidth: 74
                                Layout.preferredHeight: 56
                                background: Rectangle {
                                    radius: 6; color: Theme.surface
                                    border.color: Theme.borderStrong
                                    border.width: 1
                                }
                                onClicked: page.seekNextSegment()
                                ToolTip.visible: hovered
                                ToolTip.delay: toolTipDelay
                                ToolTip.text: toolTipText
                            }
                            Label {
                                text: qsTr("下一段")
                                color: Theme.textSecondary
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: 84
                            color: Theme.divider
                        }

                        ColumnLayout {
                            spacing: 2
                            Button {
                                objectName: "editorPlaybackStopButton"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                icon.source: Theme.icon("stop-fill")
                                icon.width: 32; icon.height: 32
                                enabled: AudioEditorController.playbackSupported
                                    && AudioEditorController.playing
                                opacity: 1.0
                                property string toolTipText:
                                    qsTr("停止（Ctrl+Space）")
                                property int toolTipDelay: 500
                                Accessible.name: qsTr("停止")
                                Accessible.role: Accessible.Button
                                Layout.preferredWidth: 74
                                Layout.preferredHeight: 56
                                background: Rectangle {
                                    radius: 6; color: Theme.surface
                                    border.color: Theme.borderStrong
                                    border.width: 1
                                }
                                onClicked: AudioEditorController.stopPlayback()
                                ToolTip.visible: hovered
                                ToolTip.delay: toolTipDelay
                                ToolTip.text: toolTipText
                            }
                            Label {
                                text: qsTr("停止")
                                color: Theme.textSecondary
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: shortcutCard
                objectName: "editorShortcutCard"
                x: 12
                y: page.interpolateLayout(532, 746)
                    + page.narrowActionBandHeight
                width: mainSurface.width - 24
                height: page.interpolateLayout(103, 67)
                color: Theme.surfaceElevated
                border.color: Theme.divider
                border.width: 1
                radius: 6
                ThemedIcon {
                    objectName: "editorShortcutKeyboardIcon"
                    x: 20; y: 8
                    width: 22; height: 22
                    source: Theme.icon("keyboard-box-line")
                    tint: Theme.textPrimary
                }
                Label {
                    x: 50; y: 9
                    text: qsTr("快捷键与鼠标操作")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
                Rectangle { x: 16; y: 38; width: parent.width - 32; height: 1; color: Theme.borderStrong }
                Flickable {
                    id: shortcutFirstRow
                    objectName: "editorShortcutFirstRow"
                    property int groupCount: 9
                    property int dividerCount: 8
                    x: 18; y: 44
                    width: parent.width - 36
                    height: 16
                    clip: true
                    interactive: contentWidth > width
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: shortcutFirstRowContent.width
                    contentHeight: height

                    Row {
                        id: shortcutFirstRowContent
                        height: shortcutFirstRow.height
                        spacing: 0
                        Repeater {
                            model: [
                                qsTr("空格 = 播放 / 暂停"),
                                qsTr("S = 在播放头处分割"),
                                qsTr("Delete = 删除片段"),
                                qsTr("Ctrl+C / X / V = 复制 / 剪切 / 粘贴"),
                                qsTr("Ctrl+Z / Y = 撤销 / 重做"),
                                qsTr("Ctrl+鼠标滚轮 = 放大 / 缩小时间线"),
                                qsTr("Shift+鼠标滚轮 = 横向滚动"),
                                qsTr("拖拽片段边缘 = 修剪"),
                                qsTr("双击音量线 = 添加控制点")
                            ]
                            delegate: Item {
                                required property int index
                                required property string modelData
                                width: firstGroupText.implicitWidth + 16
                                height: shortcutFirstRow.height
                                Text {
                                    id: firstGroupText
                                    objectName: "editorShortcutFirstGroup_" + index
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                }
                                Rectangle {
                                    objectName: index === 0
                                        ? "editorShortcutDivider"
                                        : "editorShortcutFirstDivider_" + index
                                    visible: index < shortcutFirstRow.groupCount - 1
                                    x: parent.width - width
                                    y: 0; width: 1; height: 16
                                    color: Theme.borderStrong
                                }
                            }
                        }
                    }

                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse
                            | PointerDevice.TouchPad
                        onWheel: function(event) {
                            const delta = event.angleDelta.x !== 0
                                ? event.angleDelta.x : event.angleDelta.y
                            shortcutFirstRow.contentX = Math.max(0,
                                Math.min(shortcutFirstRow.contentWidth
                                             - shortcutFirstRow.width,
                                         shortcutFirstRow.contentX
                                             - Math.sign(delta) * 80))
                            event.accepted = true
                        }
                    }
                }
            }

            EditorStatusBar {
                objectName: "editorStatusBar"
                showSuccess: statusSuccessTimer.running
                visible: AudioEditorController.busy
                    || AudioEditorController.errorMessage.length > 0
                    || statusSuccessTimer.running
                x: 0
                y: mainSurface.height - 25
                width: mainSurface.width
                height: 25
            }
            Timer {
                id: statusSuccessTimer
                interval: 3200
                repeat: false
            }
            Connections {
                target: AudioEditorController
                function onExportSucceeded(path) { statusSuccessTimer.restart() }
            }
        }
    }

    Rectangle {
        id: inspector
        objectName: "editorInspector"
        x: page.narrowLayout ? page.width - width : page.mainWidth
        y: page.narrowLayout ? 0 : 7
        width: page.inspectorWidth
        height: page.narrowLayout ? page.height : page.height - 14
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
                spacing: 8

                Rectangle {
                    id: tempoGroup
                    objectName: "inspectorTempoGroup"
                    width: parent.width
                    property bool collapsed: false
                    height: collapsed ? 38 : 153
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: tempoGroup.collapsed ? 6 : 12; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                objectName: "inspectorTempoTitle"
                                text: qsTr("A. 速度 / BPM")
                                color: Theme.textPrimary
                                font.bold: true
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                            ToolButton {
                                objectName: "inspectorTempoCollapse"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
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
                                Layout.preferredHeight: 34
                                text: AudioEditorController.targetBpm > 0
                                    ? AudioEditorController.targetBpm.toFixed(0) : ""
                                horizontalAlignment: TextInput.AlignHCenter
                                validator: IntValidator { bottom: 20; top: 400 }
                                color: Theme.textPrimary
                                background: Rectangle {
                                    color: Theme.elevated
                                    border.width: 1
                                    border.color: parent.activeFocus ? Theme.focus
                                        : Theme.borderStrong
                                    radius: 5
                                }
                                onEditingFinished: {
                                    const bpm = Number(text)
                                    if (bpm >= 20 && bpm <= 400)
                                        AudioEditorController.setTargetBpm(bpm)
                                }
                            }
                            Button {
                                objectName: "inspectorDetectBpmButton"
                                focusPolicy: Qt.TabFocus
                                Layout.preferredHeight: 34
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                text: bpmStatusLabel.text
                                enabled: AudioEditorController.bpmDetectionSupported
                                    && AudioEditorController.hasDocument
                                    && !AudioEditorController.bpmBusy
                                onClicked: AudioEditorController.detectBpm()
                                contentItem: Label {
                                    id: bpmStatusLabel
                                    objectName: "inspectorBpmStatus"
                                    text: AudioEditorController.bpmBusy
                                        ? qsTr("检测中…")
                                        : AudioEditorController.originalBpm > 0
                                        ? qsTr("原始 %1 BPM").arg(
                                            AudioEditorController.originalBpm.toFixed(0))
                                        : AudioEditorController.bpmError.length > 0
                                        ? AudioEditorController.bpmError
                                        : qsTr("自动检测BPM")
                                    color: parent.enabled
                                        ? Theme.textPrimary : Theme.textDisabled
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                ToolTip.visible: hovered
                                    && AudioEditorController.bpmError.length > 0
                                ToolTip.text: AudioEditorController.bpmError
                            }
                        }
                        RowLayout {
                            visible: !tempoGroup.collapsed
                            Layout.fillWidth: true
                            Label { text: qsTr("速度"); color: Theme.textSecondary }
                            EditorSlider {
                                id: speedSlider
                                objectName: "inspectorSpeedSlider"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true; from: 0.5; to: 2.0
                                stepSize: 0.01
                                value: AudioEditorController.speedPercent / 100
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                onPressedChanged: {
                                    if (!pressed)
                                        AudioEditorController.setSpeedPercent(value * 100)
                                }
                                onValueChanged: {
                                    if (!pressed && Math.abs(value * 100
                                            - AudioEditorController.speedPercent) > 0.001)
                                        AudioEditorController.setSpeedPercent(value * 100)
                                }
                            }
                            Label {
                                objectName: "inspectorSpeedValue"
                                text: speedSlider.value.toFixed(2) + "x"
                                color: Theme.textPrimary
                            }
                            Button {
                                objectName: "inspectorSpeedResetButton"
                                focusPolicy: Qt.TabFocus
                                Layout.preferredHeight: 34
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                text: qsTr("重置")
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                onClicked: AudioEditorController.resetTimePitch()
                            }
                        }
                    }
                }

                Rectangle {
                    id: pitchGroup
                    objectName: "inspectorPitchGroup"
                    width: parent.width
                    property bool collapsed: false
                    height: collapsed ? 38 : 114
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: pitchGroup.collapsed ? 6 : 12; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                objectName: "inspectorPitchTitle"
                                text: qsTr("B. 升调降调")
                                color: Theme.textPrimary
                                font.bold: true
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                            ToolButton {
                                objectName: "inspectorPitchCollapse"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
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
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
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
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true; from: -12; to: 12; stepSize: 1
                                value: Math.trunc(AudioEditorController.pitchCents / 100)
                                enabled: AudioEditorController.timePitchSupported
                                    && AudioEditorController.hasDocument
                                onPressedChanged: {
                                    if (!pressed)
                                        AudioEditorController.setPitch(
                                            Math.round(value), 0)
                                }
                                onValueChanged: {
                                    if (!pressed && Math.round(value) * 100
                                        !== AudioEditorController.pitchCents)
                                        AudioEditorController.setPitch(
                                            Math.round(value), 0)
                                }
                            }
                            Label {
                                text: "+12"
                                color: Theme.textSecondary
                            }
                            Button {
                                objectName: "inspectorPitchPlus"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
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
                    height: collapsed ? 38 : 143
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: preservePitchGroup.collapsed ? 6 : 8; spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                objectName: "inspectorPreservePitchTitle"
                                text: qsTr("C. 保持音调")
                                color: Theme.textPrimary
                                font.bold: true
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                            ToolButton {
                                objectName: "inspectorPreservePitchCollapse"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
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
                    height: collapsed ? 38 : 336
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: exportGroup.collapsed ? 6 : 12; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                objectName: "inspectorExportTitle"
                                text: qsTr("D. 导出设置")
                                color: Theme.textPrimary
                                font.bold: true
                                font.pixelSize: 14
                                Layout.fillWidth: true
                            }
                            ToolButton {
                                objectName: "inspectorExportCollapse"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
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
                            ThemedComboBox {
                                id: exportCodec
                                objectName: "editorExportCodec"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.columnSpan: 3
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                readonly property string text: displayText
                                model: ["WAV", "FLAC", "MP3", "AAC"]
                                currentIndex: Math.max(0, model.indexOf(
                                    page.normalizedExportCodec()))
                                onActivated: page.selectExportCodec(currentText)
                                Connections {
                                    target: AudioEditorController
                                    function onProjectChanged() {
                                        exportCodec.currentIndex = Math.max(0,
                                            exportCodec.model.indexOf(
                                                page.normalizedExportCodec()))
                                    }
                                }
                                background: Rectangle {
                                    color: !parent.enabled ? Theme.panel
                                        : parent.pressed ? Theme.surfacePressed
                                        : parent.hovered ? Theme.surfaceHover
                                        : Theme.elevated
                                    border.width: 1
                                    border.color: parent.activeFocus ? Theme.focus
                                        : Theme.borderStrong
                                    radius: 5
                                }
                            }
                            Label { text: qsTr("采样率"); color: Theme.textSecondary }
                            ThemedComboBox {
                                objectName: "editorExportSampleRate"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                readonly property string text: displayText
                                readonly property int selectedSampleRate: Math.max(1,
                                    Number(page.persistedExportSettings.sampleRate)
                                    || Number(AudioEditorController.sampleRate) || 44100)
                                readonly property var sampleRateValues: {
                                    const values = [44100, 48000, 96000]
                                    if (values.indexOf(selectedSampleRate) < 0)
                                        values.push(selectedSampleRate)
                                    values.sort(function(first, second) {
                                        return first - second
                                    })
                                    return values
                                }
                                model: sampleRateValues.map(function(value) {
                                    return value % 1000 === 0
                                        ? String(value / 1000) + " kHz"
                                        : String(value / 1000) + " kHz"
                                })
                                currentIndex: sampleRateValues.indexOf(selectedSampleRate)
                                onActivated: page.updateExportSetting(
                                    "sampleRate", sampleRateValues[currentIndex])
                                background: Rectangle {
                                    color: !parent.enabled ? Theme.panel
                                        : parent.pressed ? Theme.surfacePressed
                                        : parent.hovered ? Theme.surfaceHover
                                        : Theme.elevated
                                    border.width: 1
                                    border.color: parent.activeFocus ? Theme.focus
                                        : Theme.borderStrong
                                    radius: 5
                                }
                            }
                            Label { text: qsTr("位深"); color: Theme.textSecondary }
                            ThemedComboBox {
                                objectName: "editorExportBitDepth"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                readonly property string text: displayText
                                readonly property bool codecUsesBitDepth:
                                    page.normalizedExportCodec() === "WAV"
                                enabled: codecUsesBitDepth
                                model: codecUsesBitDepth
                                    ? ["16-bit", "24-bit", "32-bit"]
                                    : [qsTr("— / 不适用")]
                                currentIndex: !codecUsesBitDepth ? 0
                                    : page.persistedExportSettings.bitDepth === 16
                                    ? 0 : page.persistedExportSettings.bitDepth === 32
                                    ? 2 : 1
                                onActivated: {
                                    if (codecUsesBitDepth)
                                        page.updateExportSetting(
                                            "bitDepth", [16, 24, 32][currentIndex])
                                }
                                background: Rectangle {
                                    color: !parent.enabled ? Theme.panel
                                        : parent.pressed ? Theme.surfacePressed
                                        : parent.hovered ? Theme.surfaceHover
                                        : Theme.elevated
                                    border.width: 1
                                    border.color: parent.activeFocus ? Theme.focus
                                        : Theme.borderStrong
                                    radius: 5
                                }
                            }
                            Label { text: qsTr("声道"); color: Theme.textSecondary }
                            ThemedComboBox {
                                objectName: "editorExportChannels"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                readonly property string text: displayText
                                readonly property int selectedChannels: Math.max(1,
                                    Math.min(8, Number(
                                        page.persistedExportSettings.channels)
                                        || Number(AudioEditorController.channels)
                                        || 2))
                                readonly property int maximumChannels: Math.max(2,
                                    Math.min(8, Math.max(selectedChannels,
                                        Number(AudioEditorController.channels) || 2)))
                                readonly property var channelValues: {
                                    const values = []
                                    for (let channel = 1;
                                            channel <= maximumChannels; ++channel) {
                                        values.push(channel)
                                    }
                                    return values
                                }
                                model: channelValues.map(function(channel) {
                                    return channel === 1 ? qsTr("单声道")
                                        : channel === 2 ? qsTr("立体声")
                                        : qsTr("%1 声道").arg(channel)
                                })
                                currentIndex: Math.max(0,
                                    channelValues.indexOf(selectedChannels))
                                onActivated: page.updateExportSetting(
                                    "channels", channelValues[currentIndex])
                                background: Rectangle {
                                    color: !parent.enabled ? Theme.panel
                                        : parent.pressed ? Theme.surfacePressed
                                        : parent.hovered ? Theme.surfaceHover
                                        : Theme.elevated
                                    border.width: 1
                                    border.color: parent.activeFocus ? Theme.focus
                                        : Theme.borderStrong
                                    radius: 5
                                }
                            }
                            Label { text: qsTr("比特率"); color: Theme.textSecondary }
                            ThemedComboBox {
                                objectName: "editorExportBitRate"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                readonly property string text: displayText
                                readonly property bool codecUsesBitRate:
                                    page.normalizedExportCodec() === "MP3"
                                    || page.normalizedExportCodec() === "AAC"
                                enabled: codecUsesBitRate
                                model: codecUsesBitRate
                                    ? ["128 kbps", "192 kbps", "256 kbps", "320 kbps"]
                                    : [qsTr("— / 不适用")]
                                currentIndex: !codecUsesBitRate ? 0
                                    : page.persistedExportSettings.bitRate <= 128000
                                    ? 0 : page.persistedExportSettings.bitRate <= 192000
                                    ? 1 : page.persistedExportSettings.bitRate <= 256000
                                    ? 2 : 3
                                onActivated: {
                                    if (codecUsesBitRate)
                                        page.updateExportSetting("bitRate",
                                            [128000, 192000, 256000, 320000][currentIndex])
                                }
                                background: Rectangle {
                                    color: !parent.enabled ? Theme.panel
                                        : parent.pressed ? Theme.surfacePressed
                                        : parent.hovered ? Theme.surfaceHover
                                        : Theme.elevated
                                    border.width: 1
                                    border.color: parent.activeFocus ? Theme.focus
                                        : Theme.borderStrong
                                    radius: 5
                                }
                            }
                            Label { text: qsTr("输出目录"); color: Theme.textSecondary }
                            TextField {
                                objectName: "editorExportDirectory"
                                Layout.columnSpan: 2
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                text: page.persistedExportSettings.outputDirectory || "--"
                                readOnly: true
                                selectByMouse: true
                                color: Theme.textPrimary
                                background: Rectangle {
                                    color: Theme.elevated
                                    border.width: 1
                                    border.color: parent.activeFocus ? Theme.focus
                                        : Theme.borderStrong
                                    radius: 5
                                }
                            }
                            Button {
                                objectName: "editorExportBrowseButton"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.preferredHeight: 34
                                text: qsTr("浏览")
                                onClicked: {
                                    page.pendingExportAfterDirectory = false
                                    exportDirectoryDialog.open()
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled ? Theme.textPrimary
                                        : Theme.textDisabled
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 13
                                }
                                background: Rectangle {
                                    color: !parent.enabled ? Theme.panel
                                        : parent.down ? Theme.surfacePressed
                                        : parent.hovered ? Theme.surfaceHover
                                        : Theme.elevated
                                    border.width: 1
                                    border.color: parent.activeFocus ? Theme.focus
                                        : Theme.borderStrong
                                    radius: 5
                                }
                            }
                        }
                        Item { Layout.fillHeight: true }
                        Button {
                            objectName: "editorExportButton"
                            focusPolicy: Qt.TabFocus
                            Keys.onSpacePressed: function(event) {
                                event.accepted = true
                            }
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
        focusPolicy: Qt.TabFocus
        Keys.onSpacePressed: function(event) { event.accepted = true }
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
        focusPolicy: Qt.TabFocus
        Keys.onSpacePressed: function(event) { event.accepted = true }
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
