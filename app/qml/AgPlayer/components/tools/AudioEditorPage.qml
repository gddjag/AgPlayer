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

    readonly property bool narrowLayout: width < 1100
    readonly property real inspectorWidth: narrowLayout ? 300 : 380
    readonly property real mainWidth: width - inspectorWidth
    property bool pendingExportAfterDirectory: false
    property bool pendingSelectionExport: false
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
        if (page)
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
        if (event.key === Qt.Key_Escape) {
            mainColumn.cancelGesture()
            AudioEditorController.clearTransientState()
            event.accepted = true
        }
    }

    FileDialog {
        id: openDialog
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("音频、视频或工程 (*.wav *.flac *.mp3 *.aac *.m4a *.ogg *.opus *.wma *.ape *.aif *.aiff *.mp4 *.mkv *.webm *.mov *.avi *.m4v *.agproj)")]
        onAccepted: {
            if (selectedFiles.length === 1 && selectedFiles[0].toString().toLowerCase().endsWith(".agproj"))
                AudioEditorController.openProject(selectedFiles[0])
            else AudioEditorController.addFiles(selectedFiles)
        }
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
        objectName: "editorRelinkSourceDialog"
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
            const selectionOnly = page.pendingSelectionExport
            page.pendingExportAfterDirectory = false
            page.pendingSelectionExport = false
            if (continueExport) {
                Qt.callLater(function() {
                    AudioEditorController.exportToConfiguredDirectory(selectionOnly)
                })
            }
        }
        onRejected: {
            page.pendingExportAfterDirectory = false
            page.pendingSelectionExport = false
        }
    }
    ThemedDialog {
        id: discardDialog
        objectName: "editorDiscardDialog"
        title: qsTr("舍弃未保存更改？")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: AudioEditorController.confirmDiscardAndOpen()
        onRejected: AudioEditorController.cancelDiscardAndOpen()
        contentItem: Label { text: qsTr("当前工程包含未保存更改。"); wrapMode: Text.Wrap }
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
                if (page)
                    page.ensureExportSettingsConsistent()
            })
        }
    }
    Component.onCompleted: Qt.callLater(function() {
        if (page)
            page.ensureExportSettingsConsistent()
    })
    DropArea {
        id: editorAudioDropArea
        objectName: "editorAudioDropArea"
        anchors.fill: parent
        z: 100
        onDropped: function(drop) {
            if (drop.urls.length === 1 && drop.urls[0].toString().toLowerCase().endsWith(".agproj"))
                AudioEditorController.openProject(drop.urls[0])
            else AudioEditorController.addFiles(drop.urls)
        }
    }
    EditorSixTrackWorkspace {
        id: mainColumn
        objectName: "editorMainColumn"
        width: page.mainWidth
        height: page.height
        onImportRequested: openDialog.open()
        onSaveProjectRequested: AudioEditorController.save()
    }

    Rectangle {
        objectName: "editorOfflineSourceBanner"
        x: mainColumn.rulerGeometry.x; y: mainColumn.rulerGeometry.y
        width: mainColumn.rulerGeometry.width; height: mainColumn.rulerGeometry.height
        visible: page.firstProjectIssue !== null
        color: Theme.surfacePressed; border.color: Theme.warning; radius: 5
        z: 3
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8; spacing: 8
            Label {
                Layout.fillWidth: true; elide: Text.ElideMiddle
                text: qsTr("音频源不可用：") + (page.firstProjectIssue ? page.firstProjectIssue.path : "")
                color: Theme.warning; font.pixelSize: Theme.fontSizeCaption
            }
            ThemedButton {
                objectName: "editorRelinkSourceButton"
                compact: true; focusPolicy: Qt.TabFocus
                text: qsTr("重新定位文件")
                available: !AudioEditorController.busy
                onClicked: {
                    page.pendingRelinkSourceId = String(page.firstProjectIssue.sourceId)
                    relinkSourceDialog.open()
                }
            }
        }
    }

    Rectangle {
        id: inspector
        objectName: "editorInspector"
        x: page.mainWidth
        y: 0
        width: page.inspectorWidth
        height: page.height
        z: 2
        color: Theme.background
        border.color: Theme.borderStrong
        border.width: 1

        Flickable {
            id: inspectorScroller
            objectName: "editorInspectorScroller"
            anchors.fill: parent
            anchors.margins: 8
            anchors.rightMargin: 14
            contentWidth: width
            contentHeight: inspectorGroups.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: inspectorGroups
                width: inspectorScroller.width
                spacing: Theme.spacingSm

                Rectangle {
                    id: tempoGroup
                    objectName: "inspectorTempoGroup"
                    width: parent.width
                    property bool collapsed: false
                    height: collapsed ? 38 : 156
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
                                font.pixelSize: Theme.fontSizeBody
                                Layout.fillWidth: true
                            }
                            ThemedIconButton {
                                objectName: "inspectorTempoCollapse"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Accessible.name: tempoGroup.collapsed ? qsTr("展开速度设置") : qsTr("折叠速度设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                iconSource: icon.source
                                iconSize: 18
                                accessibleName: tempoGroup.collapsed
                                    ? qsTr("展开速度设置") : qsTr("折叠速度设置")
                                rotation: tempoGroup.collapsed ? 0 : 180
                                Layout.preferredWidth: 28; Layout.preferredHeight: 24
                                onClicked: tempoGroup.collapsed = !tempoGroup.collapsed
                            }
                        }
                        RowLayout {
                            visible: !tempoGroup.collapsed
                            Layout.fillWidth: true
                            Label { text: qsTr("BPM"); color: Theme.textSecondary }
                            ThemedTextField {
                                objectName: "inspectorBpmInput"
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.controlHeight
                                text: AudioEditorController.targetBpm > 0
                                    ? AudioEditorController.targetBpm.toFixed(0) : ""
                                horizontalAlignment: TextInput.AlignHCenter
                                validator: IntValidator { bottom: 20; top: 400 }
                                onEditingFinished: {
                                    const bpm = Number(text)
                                    if (bpm >= 20 && bpm <= 400)
                                        AudioEditorController.setTargetBpm(bpm)
                                }
                            }
                            ThemedButton {
                                objectName: "inspectorDetectBpmButton"
                                focusPolicy: Qt.TabFocus
                                Layout.preferredHeight: Theme.controlHeight
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                text: bpmStatusLabel.text
                                available: AudioEditorController.bpmDetectionSupported
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
                            ThemedButton {
                                objectName: "inspectorSpeedResetButton"
                                compact: true
                                focusPolicy: Qt.TabFocus
                                Layout.preferredHeight: Theme.controlHeight
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                text: qsTr("重置")
                                available: AudioEditorController.timePitchSupported
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
                    height: collapsed ? 38 : 105
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
                                font.pixelSize: Theme.fontSizeBody
                                Layout.fillWidth: true
                            }
                            ThemedIconButton {
                                objectName: "inspectorPitchCollapse"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Accessible.name: pitchGroup.collapsed ? qsTr("展开升降调设置") : qsTr("折叠升降调设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                iconSource: icon.source
                                iconSize: 18
                                accessibleName: pitchGroup.collapsed
                                    ? qsTr("展开升降调设置") : qsTr("折叠升降调设置")
                                rotation: pitchGroup.collapsed ? 0 : 180
                                Layout.preferredWidth: 28; Layout.preferredHeight: 24
                                onClicked: pitchGroup.collapsed = !pitchGroup.collapsed
                            }
                        }
                        RowLayout {
                            visible: !pitchGroup.collapsed
                            Layout.fillWidth: true
                            Label { text: qsTr("半音"); color: Theme.textSecondary }
                            ThemedButton {
                                objectName: "inspectorPitchMinus"
                                compact: true
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                text: "−"
                                Layout.preferredWidth: 28
                                available: AudioEditorController.timePitchSupported
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
                            ThemedButton {
                                objectName: "inspectorPitchPlus"
                                compact: true
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                text: "+"
                                Layout.preferredWidth: 28
                                available: AudioEditorController.timePitchSupported
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
                    height: collapsed ? 38 : 130
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
                                font.pixelSize: Theme.fontSizeBody
                                Layout.fillWidth: true
                            }
                            ThemedIconButton {
                                objectName: "inspectorPreservePitchCollapse"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Accessible.name: preservePitchGroup.collapsed ? qsTr("展开音调保护设置") : qsTr("折叠音调保护设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                iconSource: icon.source
                                iconSize: 18
                                accessibleName: preservePitchGroup.collapsed
                                    ? qsTr("展开音调保护设置") : qsTr("折叠音调保护设置")
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
                    property bool exportInProgress: false
                    property bool exportCompleted: false
                    height: collapsed ? 38 : 388
                    clip: true
                    color: Theme.surfaceElevated
                    border.color: Theme.borderStrong
                    radius: 6
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: exportGroup.collapsed ? 6 : 16; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                objectName: "inspectorExportTitle"
                                text: qsTr("D. 导出设置")
                                color: Theme.textPrimary
                                font.bold: true
                                font.pixelSize: Theme.fontSizeBody
                                Layout.fillWidth: true
                            }
                            ThemedIconButton {
                                objectName: "inspectorExportCollapse"
                                focusPolicy: Qt.TabFocus
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Accessible.name: exportGroup.collapsed ? qsTr("展开导出设置") : qsTr("折叠导出设置")
                                Accessible.role: Accessible.Button
                                icon.source: Theme.icon("arrow-down-s-line")
                                iconSource: icon.source
                                iconSize: 18
                                accessibleName: exportGroup.collapsed
                                    ? qsTr("展开导出设置") : qsTr("折叠导出设置")
                                rotation: exportGroup.collapsed ? 0 : 180
                                Layout.preferredWidth: 28; Layout.preferredHeight: 24
                                onClicked: exportGroup.collapsed = !exportGroup.collapsed
                            }
                        }
                        GridLayout {
                            visible: !exportGroup.collapsed
                            Layout.fillWidth: true; columns: 2
                            columnSpacing: 6; rowSpacing: 6
                            Label {
                                Layout.preferredWidth: 58
                                text: qsTr("输出格式")
                                color: Theme.textSecondary
                            }
                            ThemedComboBox {
                                id: exportCodec
                                objectName: "editorExportCodec"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.controlHeight
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
                            Label { Layout.preferredWidth: 58; text: qsTr("采样率"); color: Theme.textSecondary }
                            ThemedComboBox {
                                objectName: "editorExportSampleRate"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.controlHeight
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
                            Label { Layout.preferredWidth: 58; text: qsTr("位深"); color: Theme.textSecondary }
                            ThemedComboBox {
                                objectName: "editorExportBitDepth"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.controlHeight
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
                            Label { Layout.preferredWidth: 58; text: qsTr("声道"); color: Theme.textSecondary }
                            ThemedComboBox {
                                objectName: "editorExportChannels"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.controlHeight
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
                            Label { Layout.preferredWidth: 58; text: qsTr("比特率"); color: Theme.textSecondary }
                            ThemedComboBox {
                                objectName: "editorExportBitRate"
                                Keys.onSpacePressed: function(event) {
                                    event.accepted = true
                                }
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.controlHeight
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
                            Label { Layout.preferredWidth: 58; text: qsTr("输出目录"); color: Theme.textSecondary }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingXs
                                ThemedTextField {
                                    objectName: "editorExportDirectory"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Theme.controlHeight
                                    text: page.persistedExportSettings.outputDirectory || "--"
                                    readOnly: true
                                    selectByMouse: true
                                    autoScroll: false
                                    ToolTip.visible: hovered
                                    ToolTip.text: text
                                }
                                ThemedButton {
                                    objectName: "editorExportBrowseButton"
                                    compact: true
                                    focusPolicy: Qt.TabFocus
                                    Keys.onSpacePressed: function(event) {
                                        event.accepted = true
                                    }
                                    Layout.preferredHeight: Theme.controlHeight
                                    text: qsTr("浏览")
                                    onClicked: {
                                        page.pendingExportAfterDirectory = false
                                        exportDirectoryDialog.open()
                                    }
                                }
                            }
                        }
                        ThemedCheckBox {
                            id: exportSelectionOnly
                            objectName: "editorExportSelectionOnly"
                            visible: !exportGroup.collapsed
                            Layout.fillWidth: true
                            text: qsTr("仅导出选区")
                            enabled: AudioEditorController.hasDocument
                                && !AudioEditorController.busy
                                && AudioEditorController.selectionStart >= 0
                                && AudioEditorController.selectionEnd
                                    > AudioEditorController.selectionStart
                            onEnabledChanged: {
                                if (!enabled)
                                    checked = false
                            }
                        }
                        ThemedButton {
                            id: exportActionButton
                            objectName: "editorExportButton"
                            focusPolicy: Qt.TabFocus
                            Keys.onSpacePressed: function(event) {
                                event.accepted = true
                            }
                            visible: !exportGroup.collapsed
                            Layout.fillWidth: true
                            Layout.preferredHeight: 48
                            text: exportGroup.exportInProgress
                                ? qsTr("导出中 %1%").arg(
                                    Math.round(AudioEditorController.progress * 100))
                                : exportGroup.exportCompleted
                                    ? qsTr("✔ 已导出") : qsTr("导出音频")
                            available: AudioEditorController.exportSupported
                                && AudioEditorController.hasDocument
                                && !AudioEditorController.busy
                            font.pixelSize: Theme.fontSizeBody
                            font.bold: true
                            contentItem: Text {
                                text: parent.text
                                color: exportActionButton.enabled || exportGroup.exportInProgress || exportGroup.exportCompleted
                                    ? Theme.accentText : Theme.textDisabled
                                font: parent.font
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            Accessible.name: text
                            Accessible.role: Accessible.Button
                            background: Rectangle {
                                color: parent.enabled || exportGroup.exportInProgress
                                    || exportGroup.exportCompleted
                                    ? Theme.accent : Theme.disabled
                                radius: 5
                                clip: true

                                Rectangle {
                                    id: exportProgressFill
                                    objectName: "editorExportProgressFill"
                                    x: 0
                                    y: 0
                                    width: exportGroup.exportInProgress
                                        ? parent.width * Math.max(0, Math.min(1,
                                            AudioEditorController.progress))
                                        : exportGroup.exportCompleted
                                            ? parent.width : 0
                                    height: parent.height
                                    color: Theme.success
                                    radius: parent.radius
                                    Behavior on width {
                                        NumberAnimation {
                                            duration: 120
                                            easing.type: Easing.OutCubic
                                        }
                                    }
                                }
                            }
                            onClicked: {
                                exportGroup.exportCompleted = false
                                page.pendingSelectionExport =
                                    exportSelectionOnly.checked
                                exportGroup.exportInProgress =
                                    AudioEditorController.exportToConfiguredDirectory(
                                        exportSelectionOnly.checked)
                            }
                        }
                    }

                    Connections {
                        target: AudioEditorController
                        function onStateChanged() {
                            if (exportGroup.exportInProgress
                                    && !AudioEditorController.busy) {
                                exportGroup.exportInProgress = false
                                if (AudioEditorController.lastExportPath.length === 0)
                                    exportGroup.exportCompleted = false
                            }
                        }
                        function onExportSucceeded(path) {
                            exportGroup.exportInProgress = false
                            exportGroup.exportCompleted = path.length > 0
                        }
                        function onDocumentChanged() {
                            if (!exportGroup.exportInProgress)
                                exportGroup.exportCompleted = false
                        }
                    }
                }
            }
        }
    }

}
