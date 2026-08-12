import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "lightEditPage"
    color: Theme.background
    focus: true

    property var editor: LightEditor
    property real zoomScale: 1.0
    property int viewportOffsetMs: 0
    property int playheadMs: 0
    property bool keepPitch: editor.keepPitch
    readonly property int trackControlWidth: Math.max(250, Math.round(page.width * 0.17))
    readonly property int trackLaneSpacing: 4
    // A six-lane workbench is the product limit, not a visual shortcut.
    readonly property int visibleEmptyTrackCount: 6
    readonly property real workbenchTrackHeight: Math.max(48, Math.floor(
        (tracksViewport.height - trackLaneSpacing * (visibleEmptyTrackCount - 1))
        / visibleEmptyTrackCount))
    readonly property real timelineDrawingWidth:
        Math.max(320, tracksViewport.width - trackControlWidth - 12)
    readonly property int fittedTimelineDurationMs: {
        const projectEnd = projectDurationMs()
        if (projectEnd <= 0)
            return 30000
        return Math.max(10000, Math.ceil(projectEnd * 1.05))
    }
    readonly property real pixelsPerMs:
        timelineDrawingWidth / Math.max(1, fittedTimelineDurationMs)
    property int selectedTrack: editor.selectedTrack
    property var selectedTrackIndexes: [editor.selectedTrack]
    property int selectionAnchor: editor.selectedTrack
    readonly property bool rippleEditing: editor.rippleEditing
    readonly property bool loopEnabled: editor.loopEnabled
    readonly property var snapDivisions: [1, 2, 4, 8, 16, 32, 12, 24]
    property bool previewAutoResume: false
    property bool exportPanelOpen: false
    property bool inspectorVisible: true
    property string materialSearchText: ""
    property bool musicalRuler: true
    property bool gridVisible: true
    function selectedClipData() {
        if (editor.tracks.length <= editor.selectedTrack)
            return ({})
        const lane = editor.tracks[editor.selectedTrack]
        const clips = lane.clips || []
        for (let index = 0; index < clips.length; ++index) {
            if (clips[index].clipId === editor.selectedClipId)
                return clips[index]
        }
        return lane
    }

    function clipDataById(clipId) {
        const values = editor.tracks
        for (let laneIndex = 0; laneIndex < values.length; ++laneIndex) {
            const clips = values[laneIndex].clips || []
            for (let clipIndex = 0; clipIndex < clips.length; ++clipIndex) {
                if (clips[clipIndex].clipId === clipId)
                    return clips[clipIndex]
            }
        }
        return ({})
    }

    readonly property var selectedTrackData: selectedClipData()
    readonly property bool selectedHasFile: !!selectedTrackData.hasFile
    readonly property bool previewIsCurrent:
        AudioPreviewController.isTimelinePreview()

    function timelinePreviewClips() {
        const result = []
        const values = editor.tracks
        for (let laneIndex = 0; laneIndex < values.length; ++laneIndex) {
            const clips = values[laneIndex].clips || []
            for (let clipIndex = 0; clipIndex < clips.length; ++clipIndex)
                result.push(clips[clipIndex])
        }
        return result
    }

    Timer {
        id: previewDspTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (page.previewAutoResume && !editor.busy) {
                page.previewAutoResume = false
                page.playPreviewMix()
            }
        }
    }
    Component.onCompleted: {
        editor.keepPitch = SettingsController.keepPitchWhileSpeedChange
        forceActiveFocus()
    }

    Connections {
        target: SettingsController
        function onKeepPitchWhileSpeedChangeChanged() {
            editor.keepPitch = SettingsController.keepPitchWhileSpeedChange
        }
    }

    readonly property var trackColors: [
        Theme.waveformRed,
        Theme.waveformBlue,
        Theme.waveformGreen,
        Theme.waveformCyan,
        Theme.waveformViolet,
        Theme.waveformMagenta
    ]

    function formatTime(ms) {
        const total = Math.max(0, Math.floor(ms / 1000))
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
               + (seconds < 10 ? "0" : "") + seconds
    }

    function projectDurationMs() {
        let duration = 0
        const values = editor.tracks
        for (let i = 0; i < values.length; ++i) {
            const track = values[i]
            const clips = track.clips || []
            for (let clipIndex = 0; clipIndex < clips.length; ++clipIndex) {
                const clip = clips[clipIndex]
                duration = Math.max(duration, clip.timelineStartMs
                                    + Math.max(0, clip.timelineDurationMs
                                                   || (clip.outMs - clip.inMs)))
            }
        }
        return duration
    }

    function rulerIntervalMs() {
        const visibleDuration = fittedTimelineDurationMs / zoomScale
        if (visibleDuration <= 15000)
            return 1000
        if (visibleDuration <= 60000)
            return 5000
        if (visibleDuration <= 180000)
            return 15000
        return 30000
    }

    function beatDurationMs() {
        return 60000 / Math.max(40, Math.min(300, editor.targetBpm))
    }

    function beatsPerBar() {
        const parts = String(editor.timeSignature || "4/4").split("/")
        const numerator = Math.max(1, Number(parts[0]) || 4)
        const denominator = Math.max(1, Number(parts[1]) || 4)
        return numerator * 4 / denominator
    }

    function barDurationMs() {
        return beatDurationMs() * beatsPerBar()
    }

    function gridStepMs() {
        return Math.max(1, Math.round(240000 / (
            Math.max(40, Math.min(300, editor.targetBpm))
            * Math.max(1, editor.snapDivision))))
    }

    function formatRulerPosition(ms) {
        if (!musicalRuler)
            return formatTime(ms)
        const beat = beatDurationMs()
        const bar = barDurationMs()
        const barIndex = Math.floor(Math.max(0, ms) / bar)
        const beatIndex = Math.floor((Math.max(0, ms) - barIndex * bar) / beat)
        return (barIndex + 1) + "." + (beatIndex + 1)
    }

    function applyWheelZoom(delta, anchorX) {
        const oldScale = zoomScale
        const anchorTime = viewportOffsetMs
                         + anchorX / (pixelsPerMs * oldScale)
        const factor = delta > 0 ? 1.15 : 1 / 1.15
        zoomScale = Math.max(0.35, Math.min(12.0, oldScale * factor))
        viewportOffsetMs = Math.max(0, Math.round(anchorTime
                           - anchorX / (pixelsPerMs * zoomScale)))
    }

    function panTimeline(deltaPixels) {
        viewportOffsetMs = Math.max(0, Math.round(viewportOffsetMs
                           + deltaPixels / (pixelsPerMs * zoomScale)))
    }

    function handleTimelineWheel(delta, anchorX, modifiers) {
        if ((modifiers & Qt.ShiftModifier) !== 0) {
            panTimeline(-delta)
        } else if ((modifiers & Qt.ControlModifier) !== 0) {
            const maximum = Math.max(0, tracksViewport.contentHeight
                                     - tracksViewport.height)
            tracksViewport.contentY = Math.max(
                0, Math.min(maximum, tracksViewport.contentY - delta))
        } else {
            applyWheelZoom(delta, anchorX)
        }
    }

    function togglePreview() {
        if (editor.clipCount <= 0)
            return
        if (previewIsCurrent) {
            if (AudioPreviewController.playing)
                AudioPreviewController.pause()
            else
                AudioPreviewController.resume()
            return
        }
        playPreviewMix()
    }

    function playPreviewMix() {
        const absoluteStart = editor.loopEnabled
                              && editor.loopEndMs > editor.loopStartMs
                              && (playheadMs < editor.loopStartMs
                                  || playheadMs >= editor.loopEndMs)
                            ? editor.loopStartMs : playheadMs
        AudioPreviewController.playTimeline(
                    timelinePreviewClips(), absoluteStart,
                    editor.loopEnabled ? editor.loopStartMs : -1,
                    editor.loopEnabled ? editor.loopEndMs : -1)
    }

    function seekPreviewAbsolute(positionMs) {
        playheadMs = Math.max(0, Math.min(projectDurationMs(), positionMs))
        if (!previewIsCurrent)
            return
        AudioPreviewController.seek(playheadMs)
    }

    function isTrackSelected(index) {
        return selectedTrackIndexes.indexOf(index) >= 0
    }

    function selectTrack(index, modifiers) {
        let next = selectedTrackIndexes.slice()
        const control = (modifiers & Qt.ControlModifier) !== 0
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        if (shift && selectionAnchor >= 0) {
            next = []
            const first = Math.min(selectionAnchor, index)
            const last = Math.max(selectionAnchor, index)
            for (let value = first; value <= last; ++value)
                next.push(value)
        } else if (control) {
            const position = next.indexOf(index)
            if (position >= 0 && next.length > 1)
                next.splice(position, 1)
            else if (position < 0)
                next.push(index)
            selectionAnchor = index
        } else {
            next = [index]
            selectionAnchor = index
        }
        selectedTrackIndexes = next
        editor.selectedTrack = index
    }

    function selectClip(clipId, modifiers) {
        const additive = (modifiers & Qt.ControlModifier) !== 0
        const rangeSelection = (modifiers & Qt.ShiftModifier) !== 0
        if (!editor.selectClip(clipId, additive, rangeSelection))
            return
        selectedTrackIndexes = [editor.selectedTrack]
        selectionAnchor = editor.selectedTrack
    }

    function selectAllLoadedTracks() {
        let next = []
        const values = editor.tracks
        for (let index = 0; index < values.length; ++index) {
            if (values[index].hasFile)
                next.push(index)
        }
        if (next.length === 0)
            return
        selectedTrackIndexes = next
        selectionAnchor = next[0]
        editor.selectedTrack = next[0]
    }

    function deleteSelection() {
        if (editor.selectedClipIds.length <= 0 && !selectedHasFile)
            return
        editor.deleteSelectedClip()
        selectedTrackIndexes = [editor.selectedTrack]
        selectionAnchor = editor.selectedTrack
    }

    function handleEditorShortcut(key, modifiers) {
        const control = (modifiers & Qt.ControlModifier) !== 0
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        if (control && key === Qt.Key_A) {
            editor.selectAllClips()
        } else if (key === Qt.Key_Space) {
            togglePreview()
        } else if (key === Qt.Key_S && !control) {
            editor.splitSelectedClip(playheadMs)
        } else if (key === Qt.Key_Delete) {
            deleteSelection()
        } else if (key === Qt.Key_F8) {
            editor.snapEnabled = !editor.snapEnabled
        } else if (control && key === Qt.Key_X) {
            editor.cutSelectedClip()
        } else if (control && key === Qt.Key_C) {
            editor.copySelectedClip()
        } else if (control && key === Qt.Key_V) {
            editor.pasteClip()
        } else if (control && key === Qt.Key_D) {
            editor.duplicateSelectedClip(playheadMs)
        } else if (control && shift && key === Qt.Key_Z) {
            editor.redo()
        } else if (control && key === Qt.Key_Z) {
            editor.undo()
        } else if (control && key === Qt.Key_Y) {
            editor.redo()
        } else if (key === Qt.Key_L && !control) {
            editor.loopEnabled = !editor.loopEnabled
        } else if (key === Qt.Key_Plus || key === Qt.Key_Equal) {
            applyWheelZoom(120, timelineDrawingWidth / 2)
        } else if (key === Qt.Key_Minus) {
            applyWheelZoom(-120, timelineDrawingWidth / 2)
        } else if (key === Qt.Key_Home) {
            playheadMs = 0
        } else if (key === Qt.Key_End) {
            playheadMs = projectDurationMs()
        } else {
            return false
        }
        return true
    }

    Keys.onPressed: function(event) {
        event.accepted = handleEditorShortcut(event.key, event.modifiers)
    }

    Component {
        id: fileDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("音频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: editor.queueFiles([selectedFile])
        }
    }

    Component {
        id: relinkFileDialogComponent
        FileDialog {
            property string targetClipId: ""
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("音频文件 (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)")]
            onAccepted: editor.relinkClipById(targetClipId, selectedFile, true)
        }
    }

    Component {
        id: outputDirDialogComponent
        FolderDialog {
            onAccepted: outputDirectory.text = selectedFolder.toString().replace(/^file:\/+/, "")
        }
    }

    Component {
        id: saveProjectDialogComponent
        FileDialog {
            fileMode: FileDialog.SaveFile
            defaultSuffix: "agproj"
            nameFilters: [qsTr("AgPlayer 工程 (*.agproj)")]
            onAccepted: editor.saveProject(selectedFile)
        }
    }

    Component {
        id: openProjectDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("AgPlayer 工程 (*.agproj *.agproject)")]
            onAccepted: editor.loadProject(selectedFile)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 2

                Button {
                    objectName: "addFileButton"
                    Layout.preferredWidth: 82
                    display: AbstractButton.TextBesideIcon
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    text: qsTr("添加文件")
                    icon.source: Theme.icon("add-line")
                    onClicked: {
                        const dialog = fileDialogComponent.createObject(page)
                        dialog.open()
                    }
                }

                ToolButton {
                    id: openProjectButton
                    objectName: "openProjectButton"
                    display: AbstractButton.IconOnly
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    text: qsTr("打开工程")
                    icon.source: Theme.icon("folder-open-line")
                    icon.color: Theme.iconPrimary
                    onClicked: {
                        const dialog = openProjectDialogComponent.createObject(page)
                        dialog.open()
                    }
                }

                ToolButton {
                    id: saveProjectButton
                    objectName: "saveProjectButton"
                    display: AbstractButton.TextBesideIcon
                    implicitWidth: 86
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    text: qsTr("保存工程")
                    icon.source: Theme.icon("download-line")
                    icon.color: Theme.iconPrimary
                    onClicked: {
                        const dialog = saveProjectDialogComponent.createObject(page)
                        dialog.open()
                    }
                }

                ToolButton {
                    objectName: "undoProjectButton"
                    display: AbstractButton.IconOnly
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    text: qsTr("撤销")
                    icon.source: Theme.icon("arrow-go-back-line")
                    icon.color: Theme.iconPrimary
                    enabled: editor.canUndo
                    onClicked: editor.undo()
                }

                ToolButton {
                    objectName: "redoProjectButton"
                    display: AbstractButton.IconOnly
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    text: qsTr("重做")
                    icon.source: Theme.icon("arrow-go-forward-line")
                    icon.color: Theme.iconPrimary
                    enabled: editor.canRedo
                    onClicked: editor.redo()
                }

                ToolButton {
                    id: loopToggleButton
                    objectName: "loopToggleButton"
                    display: AbstractButton.IconOnly
                    implicitWidth: 30
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    text: qsTr("循环")
                    checkable: true
                    checked: page.loopEnabled
                    icon.source: Theme.icon("repeat-list-line")
                    icon.color: checked ? Theme.cyan : Theme.iconPrimary
                    onToggled: editor.loopEnabled = checked
                }

                ToolButton {
                    id: deleteClipButton
                    objectName: "deleteClipButton"
                    implicitWidth: 30
                    implicitHeight: 30
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("delete-bin-line")
                    icon.color: enabled ? Theme.iconPrimary : Theme.iconSecondary
                    enabled: page.selectedHasFile && !editor.busy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("删除选中片段 (Delete)")
                    onClicked: page.deleteSelection()
                }

                ToolButton {
                    id: splitClipButton
                    objectName: "splitClipButton"
                    implicitWidth: 30
                    implicitHeight: 30
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("split-cells-horizontal")
                    icon.color: enabled ? Theme.iconPrimary : Theme.iconSecondary
                    enabled: page.selectedHasFile && !editor.busy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("在播放头处分割 (S)")
                    onClicked: editor.splitSelectedClip(page.playheadMs)
                }

                Repeater {
                    model: [
                        { name: "rippleEditButton", text: qsTr("波纹"), icon: "split-cells-horizontal", enabled: true, shown: false,
                          action: function() { editor.rippleEditing = !editor.rippleEditing } },
                        { name: "cutClipButton", text: qsTr("剪切"), icon: "scissors-cut-line", enabled: page.selectedHasFile, shown: false,
                          action: function() { editor.cutSelectedClip() } },
                        { name: "copyClipButton", text: qsTr("复制"), icon: "file-copy-line", enabled: page.selectedHasFile, shown: false,
                          action: function() { editor.copySelectedClip() } },
                        { name: "duplicateClipButton", text: qsTr("复制片段"), icon: "file-copy-line", enabled: page.selectedHasFile, shown: false,
                          action: function() { editor.duplicateSelectedClip(page.playheadMs) } },
                        { name: "pasteClipButton", text: qsTr("粘贴"), icon: "file-copy-line", enabled: editor.hasClipboard, shown: false,
                          action: function() { editor.pasteClip() } },
                        { name: "deleteClipOverflowButton", text: qsTr("删除"), icon: "delete-bin-line", enabled: page.selectedHasFile, shown: false,
                          action: function() { page.deleteSelection() } },
                        { name: "splitClipOverflowButton", text: qsTr("分割"), icon: "split-cells-horizontal", enabled: page.selectedHasFile, shown: false,
                          action: function() { editor.splitSelectedClip(page.playheadMs) } },
                        { name: "mergeClipButton", text: qsTr("合并"), icon: "merge-cells-horizontal", enabled: page.selectedHasFile, shown: false,
                          action: function() { editor.mergeSelectedClip() } },
                        { name: "muteClipButton", text: qsTr("静音"), icon: "volume-mute-line", enabled: page.selectedHasFile, shown: false,
                          action: function() {
                              const track = editor.tracks[editor.selectedTrack]
                              editor.setTrackMuted(editor.selectedTrack, !track.muted)
                          } },
                        { name: "cropClipButton", text: qsTr("裁剪"), icon: "crop-line", enabled: page.selectedHasFile, shown: false,
                          action: function() { editor.cropSelectedClip(page.playheadMs) } }
                    ]

                    ToolButton {
                        objectName: modelData.name
                        visible: modelData.shown
                        enabled: modelData.enabled && !editor.busy
                        implicitWidth: 28
                        implicitHeight: 30
                        display: AbstractButton.IconOnly
                        checkable: modelData.name === "rippleEditButton"
                        checked: checkable && editor.rippleEditing
                        icon.source: Theme.icon(modelData.icon)
                        icon.color: checked ? Theme.cyan
                                            : (enabled ? Theme.iconPrimary
                                                       : Theme.iconSecondary)
                        icon.width: 17
                        icon.height: 17
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.text
                        onClicked: modelData.action()
                    }
                }

                ToolButton {
                    id: editorMoreMenuButton
                    objectName: "editorMoreMenuButton"
                    implicitWidth: 30
                    implicitHeight: 30
                    display: AbstractButton.IconOnly
                    icon.source: Theme.icon("list-unordered")
                    icon.color: Theme.iconPrimary
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("更多编辑命令")
                    onClicked: editorMoreMenu.popup()

                    Menu {
                        id: editorMoreMenu
                        objectName: "editorMoreMenu"

                        MenuItem {
                            objectName: "rippleEditMenuItem"
                            text: editor.rippleEditing
                                  ? qsTr("关闭波纹编辑") : qsTr("开启波纹编辑")
                            onTriggered: editor.rippleEditing = !editor.rippleEditing
                        }
                        MenuItem {
                            objectName: "autoCrossfadeMenuItem"
                            text: editor.autoCrossfade
                                  ? qsTr("关闭自动交叉淡化")
                                  : qsTr("开启自动交叉淡化")
                            onTriggered: editor.autoCrossfade = !editor.autoCrossfade
                        }
                        MenuSeparator { }
                        MenuItem {
                            objectName: "cutClipMenuItem"
                            text: qsTr("剪切")
                            enabled: page.selectedHasFile && !editor.busy
                            onTriggered: editor.cutSelectedClip()
                        }
                        MenuItem {
                            objectName: "copyClipMenuItem"
                            text: qsTr("复制")
                            enabled: page.selectedHasFile && !editor.busy
                            onTriggered: editor.copySelectedClip()
                        }
                        MenuItem {
                            objectName: "duplicateClipMenuItem"
                            text: qsTr("复制片段")
                            enabled: page.selectedHasFile && !editor.busy
                            onTriggered: editor.duplicateSelectedClip(page.playheadMs)
                        }
                        MenuItem {
                            objectName: "pasteClipMenuItem"
                            text: qsTr("粘贴")
                            enabled: editor.hasClipboard && !editor.busy
                            onTriggered: editor.pasteClip()
                        }
                        MenuItem {
                            objectName: "mergeClipMenuItem"
                            text: qsTr("合并")
                            enabled: page.selectedHasFile && !editor.busy
                            onTriggered: editor.mergeSelectedClip()
                        }
                        MenuSeparator { }
                        MenuItem {
                            objectName: "muteClipMenuItem"
                            text: page.selectedTrackData.muted
                                  ? qsTr("取消片段静音") : qsTr("片段静音")
                            enabled: page.selectedHasFile && !editor.busy
                            onTriggered: editor.setClipMutedById(
                                editor.selectedClipId,
                                !page.selectedTrackData.muted)
                        }
                        MenuItem {
                            objectName: "cropClipMenuItem"
                            text: qsTr("裁剪到播放头")
                            enabled: page.selectedHasFile && !editor.busy
                            onTriggered: editor.cropSelectedClip(page.playheadMs)
                        }
                    }
                }

                ToolButton {
                    objectName: "snapToggleButton"
                    implicitWidth: 30
                    implicitHeight: 30
                    checkable: true
                    checked: editor.snapEnabled
                    icon.source: Theme.icon("pushpin-fill")
                    icon.color: checked ? Theme.accent : Theme.iconPrimary
                    ToolTip.visible: hovered
                    ToolTip.text: checked ? qsTr("关闭吸附") : qsTr("开启吸附")
                    onToggled: editor.snapEnabled = checked
                }

                ToolButton {
                    id: gridToggleButton
                    objectName: "gridToggleButton"
                    implicitWidth: 30
                    implicitHeight: 30
                    checkable: true
                    checked: page.gridVisible
                    icon.source: Theme.icon("list-unordered")
                    icon.color: checked ? Theme.accent : Theme.iconPrimary
                    ToolTip.visible: hovered
                    ToolTip.text: checked ? qsTr("隐藏网格") : qsTr("显示网格")
                    onToggled: page.gridVisible = checked
                }

                ComboBox {
                    visible: page.width >= 1700
                    Layout.preferredWidth: 104
                    model: Array.from({length: editor.trackCount},
                                      function(_, index) { return qsTr("主轨道：轨道 %1").arg(index + 1) })
                    currentIndex: editor.selectedTrack
                    onActivated: editor.selectedTrack = currentIndex
                }

                ComboBox {
                    id: timeDisplayBox
                    objectName: "timeDisplayBox"
                    Layout.preferredWidth: 78
                    model: [qsTr("小节/拍"), qsTr("时间")]
                    currentIndex: 0
                    onActivated: page.musicalRuler = currentIndex === 0
                }

                TextField {
                    id: materialSearchField
                    objectName: "materialSearchField"
                    visible: page.width >= 1200
                    Layout.preferredWidth: 118
                    placeholderText: qsTr("搜索素材")
                    selectByMouse: true
                    text: page.materialSearchText
                    onTextChanged: page.materialSearchText = text
                }

                ToolButton {
                    objectName: "inspectorToggleButton"
                    implicitWidth: 30
                    checkable: true
                    checked: page.inspectorVisible
                    icon.source: Theme.icon("list-unordered")
                    icon.color: Theme.iconPrimary
                    ToolTip.visible: hovered
                    ToolTip.text: checked ? qsTr("收起属性检查器")
                                              : qsTr("展开属性检查器")
                    onToggled: page.inspectorVisible = checked
                }

                Text {
                    text: qsTr("工程 BPM")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }

                ToolButton {
                    visible: page.width >= 1700
                    implicitWidth: 26
                    icon.source: Theme.icon("subtract-line")
                    icon.color: Theme.iconPrimary
                    onClicked: editor.targetBpm = Math.max(40, editor.targetBpm - 0.01)
                }

                TextField {
                    id: targetBpmField
                    objectName: "targetBpmField"
                    Layout.preferredWidth: 62
                    horizontalAlignment: Text.AlignHCenter
                    text: Number(editor.targetBpm).toFixed(2)
                    validator: DoubleValidator {
                        bottom: 40
                        top: 300
                        decimals: 2
                        notation: DoubleValidator.StandardNotation
                    }
                    onEditingFinished: editor.targetBpm = Number(text)
                }

                ToolButton {
                    visible: page.width >= 1700
                    implicitWidth: 26
                    icon.source: Theme.icon("add-line")
                    icon.color: Theme.iconPrimary
                    onClicked: editor.targetBpm = Math.min(300, editor.targetBpm + 0.01)
                }

                Text {
                    visible: page.width >= 1700
                    text: qsTr("保持音高")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }

                ThemedSwitch {
                    id: keepPitchSwitch
                    objectName: "keepPitchSwitch"
                    visible: page.width >= 1700
                    checked: page.keepPitch
                    onToggled: editor.keepPitch = checked
                }

                Text {
                    visible: false
                    text: qsTr("吸附网格")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }

                ComboBox {
                    id: snapGridBox
                    objectName: "snapGridBox"
                    Layout.preferredWidth: 76
                    model: [qsTr("1 小节"), "1/2", "1/4", "1/8",
                            "1/16", "1/32", "1/8T", "1/16T"]
                    currentIndex: Math.max(0,
                        page.snapDivisions.indexOf(editor.snapDivision))
                    onActivated: {
                        editor.snapDivision = page.snapDivisions[currentIndex]
                        editor.snapEnabled = true
                    }
                }

                ComboBox {
                    id: timeSignatureBox
                    objectName: "timeSignatureBox"
                    Layout.preferredWidth: 72
                    model: ["2/4", "3/4", "4/4", "6/8"]
                    currentIndex: Math.max(0, model.indexOf(editor.timeSignature))
                    onActivated: editor.timeSignature = currentText
                }

                ComboBox {
                    id: projectKeyBox
                    objectName: "projectKeyBox"
                    Layout.preferredWidth: 68
                    model: ["C", "Cm", "D", "Dm", "E", "Em", "F", "Fm",
                            "G", "Gm", "A", "Am", "B", "Bm"]
                    currentIndex: Math.max(0, model.indexOf(editor.projectKey))
                    onActivated: editor.projectKey = currentText
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: unifyBpmButton
                    objectName: "unifyBpmButton"
                    Layout.preferredWidth: 54
                    text: qsTr("统一")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("统一项目 BPM")
                    enabled: !editor.busy
                    onClicked: editor.unifyBpm(false)
                }

                Button {
                    id: alignBpmButton
                    objectName: "alignBpmButton"
                    Layout.preferredWidth: 72
                    text: qsTr("节拍对齐")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("统一 BPM 并对齐节拍")
                    highlighted: true
                    enabled: !editor.busy
                    onClicked: editor.unifyBpm(true)
                }

                Button {
                    objectName: "exportAudioButton"
                    Layout.preferredWidth: 52
                    text: page.exportPanelOpen ? qsTr("收起导出") : qsTr("导出")
                    icon.source: Theme.icon("download-line")
                    enabled: !editor.busy
                    onClicked: page.exportPanelOpen = !page.exportPanelOpen
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Theme.panel
            border.color: Theme.border
            border.width: 1

            Rectangle {
                id: trackRulerHeader
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: page.trackControlWidth
                color: Theme.elevated
                border.color: Theme.border
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 6

                    Rectangle {
                        Layout.preferredWidth: 4
                        Layout.preferredHeight: 18
                        radius: 2
                        color: Theme.accent
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: qsTr("轨道 / 事件")
                            color: Theme.primaryText
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: qsTr("6 轨 · M / S / 锁定")
                            color: Theme.secondaryText
                            font.pixelSize: 9
                        }
                    }
                    Text {
                        text: page.musicalRuler ? qsTr("小节") : qsTr("时间")
                        color: Theme.accent
                        font.pixelSize: 10
                    }
                }
            }

            Item {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.leftMargin: page.trackControlWidth
                width: page.timelineDrawingWidth
                clip: true

                Canvas {
                    id: ruler
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.fillStyle = Theme.primaryText
                        ctx.strokeStyle = Theme.border
                        ctx.font = "11px '" + Theme.fontFallback + "'"
                        // Visual bars stay readable; fine snapping remains in the edit engine.
                        const interval = Math.max(page.gridStepMs(), page.beatDurationMs())
                        const beat = page.beatDurationMs()
                        const bar = page.barDurationMs()
                        const end = page.viewportOffsetMs
                                  + width / (page.pixelsPerMs * page.zoomScale)
                        let start = Math.floor(page.viewportOffsetMs / interval) * interval
                        let lastLabelX = -Infinity
                        for (let time = start; time <= end; time += interval) {
                            const x = (time - page.viewportOffsetMs)
                                    * page.pixelsPerMs * page.zoomScale
                            const barIndex = Math.round(time / bar)
                            const beatIndex = Math.round(time / beat)
                            const isBar = Math.abs(time - barIndex * bar) < interval / 2
                            const isBeat = Math.abs(time - beatIndex * beat) < interval / 2
                            ctx.strokeStyle = isBar ? Theme.secondaryText
                                            : (isBeat ? Theme.border : Theme.border)
                            ctx.beginPath()
                            ctx.moveTo(x, isBar ? 4 : (isBeat ? 14 : 23))
                            ctx.lineTo(x, height)
                            ctx.stroke()
                            if (isBar && x - lastLabelX >= 72) {
                                ctx.fillText(page.formatRulerPosition(time), x + 4, 14)
                                lastLabelX = x
                            }
                        }
                    }
                    Connections {
                        target: page
                        function onZoomScaleChanged() { ruler.requestPaint() }
                        function onViewportOffsetMsChanged() { ruler.requestPaint() }
                        function onMusicalRulerChanged() { ruler.requestPaint() }
                    }
                    Connections {
                        target: editor
                        function onTargetBpmChanged() { ruler.requestPaint() }
                        function onSnapDivisionChanged() { ruler.requestPaint() }
                        function onTimeSignatureChanged() { ruler.requestPaint() }
                    }
                }

                Rectangle {
                    id: loopOverlay
                    objectName: "timelineLoopOverlay"
                    z: 2
                    visible: editor.loopEnabled
                             && editor.loopEndMs > editor.loopStartMs
                    x: (editor.loopStartMs - page.viewportOffsetMs)
                       * page.pixelsPerMs * page.zoomScale
                    width: Math.max(1, (editor.loopEndMs - editor.loopStartMs)
                                    * page.pixelsPerMs * page.zoomScale)
                    height: 6
                    color: Theme.accent
                    opacity: 0.82

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.bottom
                        width: 2
                        height: 12
                        color: parent.color
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.bottom
                        width: 2
                        height: 12
                        color: parent.color
                    }
                }

                Rectangle {
                    z: 3
                    x: (page.playheadMs - page.viewportOffsetMs)
                       * page.pixelsPerMs * page.zoomScale
                    y: 0
                    width: 2
                    height: parent.height
                    color: Theme.cyan
                }

                MouseArea {
                    id: rulerInteraction
                    objectName: "timelineRulerInteraction"
                    z: 4
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    cursorShape: pressed ? Qt.SizeHorCursor : Qt.PointingHandCursor
                    property int anchorMs: 0
                    property real anchorX: 0
                    property bool selectingLoop: false

                    function timeAt(localX) {
                        return Math.max(0, Math.round(
                            page.viewportOffsetMs
                            + localX / (page.pixelsPerMs * page.zoomScale)))
                    }

                    onPressed: function(mouse) {
                        anchorMs = timeAt(mouse.x)
                        anchorX = mouse.x
                        selectingLoop = false
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed || Math.abs(mouse.x - anchorX) < 4)
                            return
                        selectingLoop = true
                        const currentMs = timeAt(mouse.x)
                        editor.loopStartMs = Math.min(anchorMs, currentMs)
                        editor.loopEndMs = Math.max(anchorMs, currentMs)
                        editor.loopEnabled = editor.loopEndMs > editor.loopStartMs
                    }
                    onReleased: function(mouse) {
                        if (!selectingLoop)
                            page.seekPreviewAbsolute(timeAt(mouse.x))
                        selectingLoop = false
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 330
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 6

            Flickable {
                id: tracksViewport
                objectName: "timelineViewport"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: tracksColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: tracksColumn
                    width: tracksViewport.width
                    height: childrenRect.height
                    spacing: page.trackLaneSpacing

                    Repeater {
                        model: editor.tracks

                        MultiTrackWaveform {
                            width: tracksColumn.width
                            trackIndex: index
                            track: modelData
                            trackColor: track.color || page.trackColors[index % page.trackColors.length]
                            pixelsPerMs: page.pixelsPerMs
                            zoomScale: page.zoomScale
                            viewportOffsetMs: page.viewportOffsetMs
                            playheadMs: page.playheadMs
                            selected: page.isTrackSelected(index)
                            controlWidth: page.trackControlWidth
                            laneSpacing: page.trackLaneSpacing
                            filterText: page.materialSearchText
                            gridStepMs: page.gridStepMs()
                            beatDurationMs: page.beatDurationMs()
                            barDurationMs: page.barDurationMs()
                            gridVisible: page.gridVisible
                            idealLaneHeight: page.workbenchTrackHeight

                            onTrackClicked: function(modifiers) {
                                page.selectTrack(index, modifiers)
                            }
                            onClipClicked: function(clipId, modifiers) {
                                page.selectClip(clipId, modifiers)
                            }
                            onClipMoveRequested: function(clipId, targetTrackIndex,
                                                          timelineStartMs,
                                                          modifiers) {
                                const bypassSnap =
                                    (modifiers & Qt.ShiftModifier) !== 0
                                const restoreSnap = bypassSnap
                                                    && editor.snapEnabled
                                if (restoreSnap)
                                    editor.snapEnabled = false
                                const clip = page.clipDataById(clipId)
                                const duplicateDrag =
                                    (modifiers & Qt.AltModifier) !== 0
                                const isGroupMove = editor.selectedClipIds.length > 1
                                    && editor.selectedClipIds.indexOf(clipId) >= 0
                                if (duplicateDrag) {
                                    editor.duplicateClipToTrackById(
                                        clipId, targetTrackIndex, timelineStartMs)
                                } else if (isGroupMove) {
                                    editor.moveSelectedClips(
                                        timelineStartMs - (clip.timelineStartMs || 0),
                                        targetTrackIndex - (clip.trackIndex || 0))
                                } else {
                                    editor.moveClipToTrackById(
                                        clipId, targetTrackIndex, timelineStartMs)
                                }
                                if (restoreSnap)
                                    editor.snapEnabled = true
                            }
                            onClipTrimRequested: function(clipId, inMs, outMs, trimLeft, modifiers) {
                                editor.trimClipEdgeById(
                                    clipId, inMs, outMs, trimLeft,
                                    (modifiers & Qt.ShiftModifier) !== 0)
                            }
                            onSeekRequested: function(positionMs) {
                                page.seekPreviewAbsolute(positionMs)
                            }

                            FileDropArea {
                                anchors.fill: parent
                                onUrlsDropped: function(urls) {
                                    editor.selectedTrack = index
                                    editor.queueFiles(urls)
                                }
                            }
                        }
                    }
                }

                Item {
                    id: marqueeLayer
                    anchors.fill: parent
                    z: 20

                    function laneAt(localY) {
                        const contentY = localY + tracksViewport.contentY
                        let top = 0
                        const lanes = editor.tracks || []
                        for (let lane = 0; lane < lanes.length; ++lane) {
                            if (!lanes[lane].hasFile
                                    && lane >= page.visibleEmptyTrackCount)
                                continue
                            const height = lanes[lane].collapsed && lanes[lane].hasFile
                                ? 34 : page.workbenchTrackHeight
                            if (contentY >= top && contentY < top + height)
                                return lane
                            top += height + page.trackLaneSpacing
                        }
                        return -1
                    }

                    function timeAt(localX) {
                        return Math.max(0, Math.round(
                            page.viewportOffsetMs
                            + (localX - page.trackControlWidth)
                              / (page.pixelsPerMs * page.zoomScale)))
                    }

                    function hasClipAt(laneIndex, timelineMs) {
                        if (laneIndex < 0 || laneIndex >= editor.tracks.length)
                            return false
                        const clips = editor.tracks[laneIndex].clips || []
                        for (let index = 0; index < clips.length; ++index) {
                            const clip = clips[index]
                            const start = clip.timelineStartMs || 0
                            const duration = Math.max(0,
                                clip.timelineDurationMs
                                || ((clip.outMs || clip.durationMs || 0)
                                    - (clip.inMs || 0)))
                            if (timelineMs >= start && timelineMs <= start + duration)
                                return true
                        }
                        return false
                    }

                    Rectangle {
                        id: marqueeRect
                        objectName: "timelineMarqueeRect"
                        visible: marqueeMouse.dragging
                        x: Math.min(marqueeMouse.anchorX, marqueeMouse.currentX)
                        y: Math.min(marqueeMouse.anchorY, marqueeMouse.currentY)
                        width: Math.abs(marqueeMouse.currentX - marqueeMouse.anchorX)
                        height: Math.abs(marqueeMouse.currentY - marqueeMouse.anchorY)
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g,
                                       Theme.accent.b, 0.12)
                        border.width: 1
                        border.color: Theme.accent
                    }

                    MouseArea {
                        id: marqueeMouse
                        objectName: "timelineBoxSelectionArea"
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        propagateComposedEvents: true
                        property bool dragging: false
                        property real anchorX: 0
                        property real anchorY: 0
                        property real currentX: 0
                        property real currentY: 0
                        property int anchorTrack: -1
                        property int anchorTimeMs: 0

                        onPressed: function(mouse) {
                            const lane = marqueeLayer.laneAt(mouse.y)
                            const time = marqueeLayer.timeAt(mouse.x)
                            if (mouse.x < page.trackControlWidth || lane < 0
                                    || marqueeLayer.hasClipAt(lane, time)) {
                                mouse.accepted = false
                                return
                            }
                            dragging = true
                            anchorX = currentX = mouse.x
                            anchorY = currentY = mouse.y
                            anchorTrack = lane
                            anchorTimeMs = time
                        }
                        onPositionChanged: function(mouse) {
                            if (!dragging)
                                return
                            currentX = Math.max(page.trackControlWidth,
                                                Math.min(width, mouse.x))
                            currentY = Math.max(0, Math.min(height, mouse.y))
                        }
                        onReleased: function(mouse) {
                            if (!dragging)
                                return
                            currentX = Math.max(page.trackControlWidth,
                                                Math.min(width, mouse.x))
                            currentY = Math.max(0, Math.min(height, mouse.y))
                            const endTrack = marqueeLayer.laneAt(currentY)
                            const endTime = marqueeLayer.timeAt(currentX)
                            editor.selectClipsInRange(
                                Math.min(anchorTimeMs, endTime),
                                Math.max(anchorTimeMs, endTime),
                                Math.min(anchorTrack, endTrack),
                                Math.max(anchorTrack, endTrack),
                                (mouse.modifiers & Qt.ControlModifier) !== 0)
                            dragging = false
                        }
                        onCanceled: dragging = false
                    }
                }

                WheelHandler {
                    target: null
                    onWheel: function(event) {
                        page.handleTimelineWheel(event.angleDelta.y,
                                                 event.x - page.trackControlWidth,
                                                 event.modifiers)
                        event.accepted = true
                    }
                }

                DragHandler {
                    id: timelinePanHandler
                    target: null
                    acceptedButtons: Qt.MiddleButton
                    property point previousTranslation: Qt.point(0, 0)
                    onActiveChanged: previousTranslation = translation
                    onTranslationChanged: {
                        if (!active)
                            return
                        const dx = translation.x - previousTranslation.x
                        const dy = translation.y - previousTranslation.y
                        page.panTimeline(-dx)
                        const maximum = Math.max(
                            0, tracksViewport.contentHeight - tracksViewport.height)
                        tracksViewport.contentY = Math.max(
                            0, Math.min(maximum, tracksViewport.contentY - dy))
                        previousTranslation = translation
                    }
                }
            }

            Rectangle {
                id: inspectorPanel
                objectName: "editorInspectorPanel"
                visible: page.inspectorVisible
                Layout.preferredWidth: visible ? Math.max(300, page.width * 0.22) : 0
                Layout.fillHeight: true
                color: Theme.elevated
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                Text {
                    id: inspectorTitle
                    objectName: "editorInspectorTitle"
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.top: parent.top
                    anchors.topMargin: 8
                    text: qsTr("属性检查器")
                    color: Theme.primaryText
                    font.weight: Font.DemiBold
                    z: 2
                }
                ToolButton {
                    objectName: "closeInspectorButton"
                    anchors.right: parent.right
                    anchors.rightMargin: 5
                    anchors.top: parent.top
                    anchors.topMargin: 2
                    implicitWidth: 28
                    implicitHeight: 28
                    icon.source: Theme.icon("close-fill")
                    icon.color: Theme.iconSecondary
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("收起属性检查器")
                    onClicked: page.inspectorVisible = false
                    z: 2
                }

                Flickable {
                    id: inspectorScroll
                    objectName: "editorInspectorScroll"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 38
                    anchors.bottomMargin: 8
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: width
                    contentHeight: inspectorColumn.implicitHeight

                    ScrollBar.vertical: ScrollBar {
                        id: inspectorScrollBar
                        policy: ScrollBar.AsNeeded
                    }

                    ColumnLayout {
                        id: inspectorColumn
                        width: inspectorScroll.width
                               - (inspectorScrollBar.visible ? 10 : 0)
                        spacing: 6

                        Rectangle {
                            objectName: "clipFileInfoPanel"
                            Layout.fillWidth: true
                            implicitHeight: fileInfoColumn.implicitHeight + 16
                            color: Theme.panel
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            ColumnLayout {
                                id: fileInfoColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 5
                                Text {
                                    text: qsTr("文件信息")
                                    color: Theme.primaryText
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: page.selectedHasFile
                                          ? (page.selectedTrackData.name || "--")
                                          : qsTr("请选择音频片段")
                                    color: Theme.primaryText
                                    elide: Text.ElideMiddle
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("格式"); color: Theme.secondaryText }
                                    Item { Layout.fillWidth: true }
                                    Text { text: page.selectedTrackData.format || "--"; color: Theme.primaryText }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("采样率 / 声道"); color: Theme.secondaryText }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: page.selectedHasFile
                                              ? ((page.selectedTrackData.sampleRate || 0)
                                                 + " Hz · "
                                                 + (page.selectedTrackData.channels || 0))
                                              : "--"
                                        color: Theme.primaryText
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("位置 / 时长"); color: Theme.secondaryText }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: page.selectedHasFile
                                              ? page.formatTime(page.selectedTrackData.timelineStartMs)
                                                + " / "
                                                + page.formatTime(page.selectedTrackData.outMs
                                                                  - page.selectedTrackData.inMs)
                                              : "--"
                                        color: Theme.primaryText
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: page.selectedTrackData.path || ""
                                    color: page.selectedTrackData.available === false
                                           ? Theme.waveformRed : Theme.secondaryText
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                }
                                Button {
                                    id: relinkClipButton
                                    objectName: "relinkClipButton"
                                    Layout.fillWidth: true
                                    visible: page.selectedHasFile
                                             && page.selectedTrackData.available === false
                                    text: qsTr("重新定位缺失素材")
                                    icon.source: Theme.icon("folder-open-line")
                                    onClicked: {
                                        const dialog = relinkFileDialogComponent.createObject(
                                            page, {"targetClipId": editor.selectedClipId})
                                        dialog.open()
                                    }
                                }
                            }
                        }

                        Rectangle {
                            objectName: "clipTimePitchPanel"
                            Layout.fillWidth: true
                            implicitHeight: clipTimePitchColumn.implicitHeight + 16
                            color: Theme.panel
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            ColumnLayout {
                                id: clipTimePitchColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6
                                Text {
                                    text: qsTr("Time / Pitch")
                                    color: Theme.primaryText
                                    font.weight: Font.DemiBold
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("半音"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    SpinBox {
                                        id: clipPitchSemitoneBox
                                        objectName: "clipPitchSemitoneBox"
                                        from: -12
                                        to: 12
                                        editable: true
                                        enabled: page.selectedHasFile
                                        value: Math.round(page.selectedTrackData.pitchSemitones || 0)
                                        onValueModified: editor.setClipPitchById(
                                            page.selectedTrackData.clipId, value,
                                            clipFineCentsBox.value)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("微调 (Cent)"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    SpinBox {
                                        id: clipFineCentsBox
                                        objectName: "clipFineCentsBox"
                                        from: -100
                                        to: 100
                                        editable: true
                                        enabled: page.selectedHasFile
                                        value: Math.round(page.selectedTrackData.finePitchCents || 0)
                                        onValueModified: editor.setClipPitchById(
                                            page.selectedTrackData.clipId,
                                            clipPitchSemitoneBox.value, value)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("人声保护"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    ComboBox {
                                        id: clipFormantModeBox
                                        objectName: "clipFormantModeBox"
                                        Layout.preferredWidth: 118
                                        enabled: false
                                        model: [qsTr("关闭"), qsTr("自然"), qsTr("增强")]
                                        currentIndex: 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("当前构建未接入共振峰保持；该项不可用")
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("自然/增强需要共振峰保持后端，当前构建未支持")
                                    color: Theme.secondaryText
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("瞬态保护"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    SpinBox {
                                        id: clipTransientProtectionBox
                                        objectName: "clipTransientProtectionBox"
                                        from: 0
                                        to: 100
                                        stepSize: 5
                                        editable: true
                                        enabled: page.selectedHasFile
                                        value: Math.round(
                                            (page.selectedTrackData.transientProtection || 0) * 100)
                                        textFromValue: function(value) { return value + "%" }
                                        valueFromText: function(text) {
                                            return Number(text.replace("%", ""))
                                        }
                                        onValueModified: editor.setClipTransientProtectionById(
                                            page.selectedTrackData.clipId, value / 100)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("高质量处理"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    ThemedSwitch {
                                        id: clipHighQualitySwitch
                                        objectName: "clipHighQualitySwitch"
                                        enabled: page.selectedHasFile
                                        checked: page.selectedHasFile
                                                 && page.selectedTrackData.highQuality
                                        onToggled: editor.setClipHighQualityById(
                                            page.selectedTrackData.clipId, checked)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        objectName: "stereoPhaseProtectionUnavailable"
                                        text: qsTr("立体声相位保护")
                                        color: Theme.secondaryText
                                        opacity: 0.55
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: qsTr("当前引擎不支持")
                                        color: Theme.secondaryText
                                        opacity: 0.55
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        Rectangle {
                            objectName: "clipLoopPanel"
                            Layout.fillWidth: true
                            implicitHeight: loopInspectorColumn.implicitHeight + 16
                            color: Theme.panel
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            ColumnLayout {
                                id: loopInspectorColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6
                                Text {
                                    text: qsTr("Loop / 时间线")
                                    color: Theme.primaryText
                                    font.weight: Font.DemiBold
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("片段模式")
                                        color: Theme.secondaryText
                                        Layout.fillWidth: true
                                    }
                                    ComboBox {
                                        id: clipLoopModeBox
                                        objectName: "clipLoopModeBox"
                                        Layout.preferredWidth: 132
                                        enabled: page.selectedHasFile
                                        model: [qsTr("单次"), qsTr("循环"),
                                                qsTr("跟随项目 BPM")]
                                        currentIndex: page.selectedTrackData.loopMode === "Loop"
                                                      ? 1
                                                      : (page.selectedTrackData.loopMode
                                                         === "FollowProjectBPM" ? 2 : 0)
                                        onActivated: {
                                            const modes = ["OneShot", "Loop",
                                                           "FollowProjectBPM"]
                                            editor.setClipLoopById(
                                                page.selectedTrackData.clipId,
                                                modes[currentIndex],
                                                clipTimelineDurationBox.value)
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("时间线时长")
                                        color: Theme.secondaryText
                                        Layout.fillWidth: true
                                    }
                                    SpinBox {
                                        id: clipTimelineDurationBox
                                        objectName: "clipTimelineDurationBox"
                                        from: 200
                                        to: 86400000
                                        stepSize: 100
                                        editable: true
                                        enabled: page.selectedHasFile
                                                 && page.selectedTrackData.loopMode !== "OneShot"
                                        value: Math.max(200,
                                            page.selectedTrackData.timelineDurationMs || 200)
                                        onValueModified: editor.setClipLoopById(
                                            page.selectedTrackData.clipId,
                                            page.selectedTrackData.loopMode || "OneShot",
                                            value)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("项目循环起点")
                                        color: Theme.secondaryText
                                        Layout.fillWidth: true
                                    }
                                    SpinBox {
                                        objectName: "projectLoopStartBox"
                                        from: 0
                                        to: 86400000
                                        stepSize: 100
                                        editable: true
                                        value: editor.loopStartMs
                                        onValueModified: editor.loopStartMs = value
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("项目循环终点")
                                        color: Theme.secondaryText
                                        Layout.fillWidth: true
                                    }
                                    SpinBox {
                                        objectName: "projectLoopEndBox"
                                        from: 0
                                        to: 86400000
                                        stepSize: 100
                                        editable: true
                                        value: editor.loopEndMs
                                        onValueModified: editor.loopEndMs = value
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: bpmInspectorColumn.implicitHeight + 16
                            color: Theme.panel
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            ColumnLayout {
                                id: bpmInspectorColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6
                                Text {
                                    text: qsTr("BPM / 速度")
                                    color: Theme.primaryText
                                    font.weight: Font.DemiBold
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("检测结果"); color: Theme.secondaryText }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: page.selectedTrackData.originalBpm > 0
                                              ? Number(page.selectedTrackData.originalBpm).toFixed(2)
                                                + " · "
                                                + Math.round(page.selectedTrackData.bpmConfidence || 0)
                                                + "%"
                                              : "--"
                                        color: Theme.primaryText
                                    }
                                }
                                Button {
                                    id: analyzeTrackButton
                                    objectName: "analyzeTrackButton"
                                    Layout.fillWidth: true
                                    text: qsTr("重新分析 BPM")
                                    enabled: page.selectedHasFile && !editor.busy
                                    onClicked: editor.analyzeClipBpmById(
                                        page.selectedTrackData.clipId)
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Button {
                                        Layout.fillWidth: true
                                        text: qsTr("半拍")
                                        enabled: page.selectedHasFile
                                                 && page.selectedTrackData.originalBpm > 0
                                        onClicked: editor.setClipTargetBpmById(
                                            page.selectedTrackData.clipId,
                                            Math.max(40, page.selectedTrackData.originalBpm / 2))
                                    }
                                    Button {
                                        Layout.fillWidth: true
                                        text: qsTr("双拍")
                                        enabled: page.selectedHasFile
                                                 && page.selectedTrackData.originalBpm > 0
                                        onClicked: editor.setClipTargetBpmById(
                                            page.selectedTrackData.clipId,
                                            Math.min(300, page.selectedTrackData.originalBpm * 2))
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("目标 BPM"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    TextField {
                                        id: selectedTrackBpmField
                                        objectName: "selectedTrackBpmField"
                                        Layout.preferredWidth: 108
                                        horizontalAlignment: Text.AlignHCenter
                                        enabled: page.selectedHasFile
                                                 && page.selectedTrackData.originalBpm > 0
                                        text: page.selectedTrackData.targetBpm > 0
                                              ? Number(page.selectedTrackData.targetBpm).toFixed(2)
                                              : ""
                                        validator: DoubleValidator {
                                            bottom: 40
                                            top: 300
                                            decimals: 2
                                        }
                                        onEditingFinished: {
                                            if (editor.setClipTargetBpmById(
                                                    page.selectedTrackData.clipId,
                                                    Number(text)))
                                                previewDspTimer.restart()
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("速度"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    TextField {
                                        id: selectedTrackSpeedField
                                        objectName: "selectedTrackSpeedField"
                                        Layout.preferredWidth: 108
                                        readOnly: true
                                        horizontalAlignment: Text.AlignHCenter
                                        text: page.selectedHasFile
                                              ? (Number(page.selectedTrackData.speedRatio) * 100)
                                                    .toFixed(2) + "%"
                                              : "--"
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("保持音高"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    ThemedSwitch {
                                        id: selectedTrackKeepPitchSwitch
                                        objectName: "selectedTrackKeepPitchSwitch"
                                        enabled: page.selectedHasFile
                                        checked: page.selectedHasFile
                                                 && page.selectedTrackData.keepPitch
                                        onToggled: editor.setClipKeepPitchById(
                                            page.selectedTrackData.clipId, checked)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("节拍对齐"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    ThemedSwitch {
                                        id: alignTrackSwitch
                                        objectName: "alignTrackSwitch"
                                        enabled: page.selectedHasFile
                                        checked: page.selectedHasFile
                                                 && page.selectedTrackData.aligned
                                        onToggled: editor.setClipBeatAlignedById(
                                            page.selectedTrackData.clipId, checked)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: editInspectorColumn.implicitHeight + 16
                            color: Theme.panel
                            radius: Theme.radiusSm
                            border.color: Theme.border
                            ColumnLayout {
                                id: editInspectorColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6
                                Text {
                                    text: qsTr("片段属性")
                                    color: Theme.primaryText
                                    font.weight: Font.DemiBold
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("增益"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    SpinBox {
                                        id: clipGainBox
                                        objectName: "clipGainBox"
                                        from: 0
                                        to: 400
                                        stepSize: 5
                                        editable: true
                                        enabled: page.selectedHasFile
                                        value: Math.round((page.selectedTrackData.gain || 1) * 100)
                                        textFromValue: function(value) { return value + "%" }
                                        valueFromText: function(text) {
                                            return Number(text.replace("%", ""))
                                        }
                                        onValueModified: editor.setClipGainById(
                                            page.selectedTrackData.clipId, value / 100)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: qsTr("淡入 / 淡出"); color: Theme.secondaryText; Layout.fillWidth: true }
                                    SpinBox {
                                        id: clipFadeInBox
                                        objectName: "clipFadeInBox"
                                        from: 0
                                        to: Math.max(0, Math.floor(
                                            (page.selectedTrackData.outMs
                                             - page.selectedTrackData.inMs) / 2))
                                        stepSize: 10
                                        editable: true
                                        enabled: page.selectedHasFile
                                        value: page.selectedTrackData.fadeInMs || 0
                                        onValueModified: editor.setClipFadesById(
                                            page.selectedTrackData.clipId, value,
                                            clipFadeOutBox.value)
                                    }
                                    SpinBox {
                                        id: clipFadeOutBox
                                        objectName: "clipFadeOutBox"
                                        from: 0
                                        to: clipFadeInBox.to
                                        stepSize: 10
                                        editable: true
                                        enabled: page.selectedHasFile
                                        value: page.selectedTrackData.fadeOutMs || 0
                                        onValueModified: editor.setClipFadesById(
                                            page.selectedTrackData.clipId,
                                            clipFadeInBox.value, value)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("淡入曲线")
                                        color: Theme.secondaryText
                                        Layout.fillWidth: true
                                    }
                                    ComboBox {
                                        id: clipFadeInCurveBox
                                        objectName: "clipFadeInCurveBox"
                                        Layout.preferredWidth: 138
                                        enabled: page.selectedHasFile
                                        textRole: "label"
                                        valueRole: "value"
                                        model: [
                                            { label: qsTr("线性"), value: "Linear" },
                                            { label: qsTr("等功率"), value: "EqualPower" },
                                            { label: qsTr("平滑"), value: "Smooth" }
                                        ]
                                        currentIndex: Math.max(0,
                                            ["Linear", "EqualPower", "Smooth"].indexOf(
                                                page.selectedTrackData.fadeInCurve
                                                || "EqualPower"))
                                        onActivated: editor.setClipFadeCurvesById(
                                            page.selectedTrackData.clipId,
                                            currentValue,
                                            clipFadeOutCurveBox.currentValue)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: qsTr("淡出曲线")
                                        color: Theme.secondaryText
                                        Layout.fillWidth: true
                                    }
                                    ComboBox {
                                        id: clipFadeOutCurveBox
                                        objectName: "clipFadeOutCurveBox"
                                        Layout.preferredWidth: 138
                                        enabled: page.selectedHasFile
                                        textRole: "label"
                                        valueRole: "value"
                                        model: [
                                            { label: qsTr("线性"), value: "Linear" },
                                            { label: qsTr("等功率"), value: "EqualPower" },
                                            { label: qsTr("平滑"), value: "Smooth" }
                                        ]
                                        currentIndex: Math.max(0,
                                            ["Linear", "EqualPower", "Smooth"].indexOf(
                                                page.selectedTrackData.fadeOutCurve
                                                || "EqualPower"))
                                        onActivated: editor.setClipFadeCurvesById(
                                            page.selectedTrackData.clipId,
                                            clipFadeInCurveBox.currentValue,
                                            currentValue)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 4

                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: Theme.waveformGreen
                }
                Text {
                    text: qsTr("实时预览")
                    color: Theme.primaryText
                    font.pixelSize: 11
                }
                Text {
                    text: editor.rippleEditing ? qsTr("吸附：开 · 波纹：开")
                                               : qsTr("吸附：开")
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                ToolButton {
                    objectName: "lightPreviewHomeButton"
                    implicitWidth: 32
                    icon.source: Theme.icon("skip-back-fill")
                    icon.color: Theme.iconPrimary
                    enabled: editor.clipCount > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("回到开头")
                    onClicked: page.playheadMs = 0
                }


                ToolButton {
                    objectName: "lightPreviewRewindButton"
                    implicitWidth: 32
                    icon.source: Theme.icon("arrow-go-back-line")
                    icon.color: Theme.iconPrimary
                    enabled: editor.clipCount > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("后退 5 秒")
                    onClicked: {
                        page.seekPreviewAbsolute(page.playheadMs - 5000)
                    }
                }

                ToolButton {
                    objectName: "lightPreviewButton"
                    implicitWidth: 38
                    enabled: editor.clipCount > 0 && !editor.busy
                    icon.source: Theme.icon(page.previewIsCurrent
                                            && AudioPreviewController.playing
                                            ? "pause-fill" : "play-fill")
                    icon.color: Theme.iconPrimary
                    onClicked: {
                        page.togglePreview()
                    }
                }
                ToolButton {
                    objectName: "lightPreviewStopButton"
                    implicitWidth: 32
                    icon.source: Theme.icon("checkbox-blank-line")
                    icon.color: Theme.iconPrimary
                    enabled: editor.clipCount > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("停止")
                    onClicked: {
                        AudioPreviewController.stop()
                        page.playheadMs = 0
                    }
                }
                ToolButton {
                    objectName: "lightPreviewLoopButton"
                    implicitWidth: 32
                    checkable: true
                    checked: editor.loopEnabled
                    icon.source: Theme.icon("repeat-fill")
                    icon.color: checked ? Theme.accent
                                        : Theme.iconPrimary
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("循环播放")
                    onToggled: editor.loopEnabled = checked
                }
                Text {
                    text: page.formatTime(page.playheadMs)
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    text: editor.timeSignature + "  ·  "
                          + Math.round(editor.targetBpm) + " BPM  ·  "
                          + editor.projectKey
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 11
                }
                Text {
                    text: qsTr("缩放")
                    color: Theme.secondaryText
                    font.pixelSize: 10
                }
                Slider {
                    objectName: "timelineZoomSlider"
                    Layout.preferredWidth: 82
                    from: 0.5
                    to: 32
                    value: page.zoomScale
                    onMoved: page.zoomScale = value
                }
                Text {
                    objectName: "cpuLoadLabel"
                    text: editor.busy ? qsTr("处理中") : qsTr("预听就绪")
                    color: editor.busy ? Theme.accent : Theme.secondaryText
                    font.pixelSize: 10
                }
                Slider {
                    objectName: "lightPreviewVolume"
                    Layout.preferredWidth: 110
                    from: 0
                    to: 1
                    value: AudioPreviewController.volume
                    enabled: page.selectedHasFile
                    onMoved: AudioPreviewController.volume = value
                }
                Text {
                    text: page.formatTime(page.projectDurationMs())
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 11
                }
                Row {
                    objectName: "masterOutputMeter"
                    Layout.preferredWidth: 58
                    Layout.preferredHeight: 24
                    spacing: 2
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: AudioPreviewController.playing
                              ? qsTr("输出监测待接入")
                              : qsTr("无信号")
                        color: Theme.secondaryText
                        font.pixelSize: 9
                    }
                }
            }
        }

        RowLayout {
            objectName: "editorExportSettings"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 126 : 0
            spacing: 6
            visible: page.exportPanelOpen
            enabled: visible

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    columns: 6
                    rowSpacing: 5
                    columnSpacing: 8

                    Text { text: qsTr("导出设置"); color: Theme.primaryText; font.pixelSize: 14; Layout.columnSpan: 6 }
                    Text { text: qsTr("导出范围"); color: Theme.secondaryText }
                    ComboBox {
                        id: exportScopeBox
                        objectName: "exportScopeBox"
                        model: [qsTr("整个工程"), qsTr("循环区域"),
                                qsTr("选中片段")]
                        Layout.columnSpan: 5
                        Layout.fillWidth: true
                    }
                    Text { text: qsTr("输出格式"); color: Theme.secondaryText }
                    ComboBox {
                        id: outputFormatBox
                        objectName: "outputFormatBox"
                        model: ["MP3", "WAV", "FLAC", "AAC", "M4A", "OGG"]
                        currentIndex: 0
                        Layout.preferredWidth: 150
                    }
                    Text { text: qsTr("采样率"); color: Theme.secondaryText }
                    ComboBox {
                        id: outputSampleRateBox
                        objectName: "outputSampleRateBox"
                        model: ["44,100 Hz", "48,000 Hz", "88,200 Hz",
                                "96,000 Hz", "176,400 Hz", "192,000 Hz"]
                        Layout.preferredWidth: 160
                    }
                    Text { text: qsTr("声道"); color: Theme.secondaryText }
                    ComboBox {
                        id: outputChannelBox
                        objectName: "outputChannelBox"
                        model: [qsTr("立体声"), qsTr("单声道")]
                        Layout.preferredWidth: 130
                    }
                    Text { text: qsTr("输出目录"); color: Theme.secondaryText }
                    TextField {
                        id: outputDirectory
                        text: SettingsController.defaultOutputDirectory
                        Layout.columnSpan: 4
                        Layout.fillWidth: true
                    }
                    Button {
                        text: qsTr("浏览")
                        onClicked: {
                            const dialog = outputDirDialogComponent.createObject(page)
                            dialog.open()
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("项目信息"); color: Theme.primaryText; font.pixelSize: 14 }
                        Text {
                            text: qsTr("轨道数量：%1 / 6").arg(editor.tracks.filter(
                                function(track) { return track.hasFile }).length)
                            color: Theme.secondaryText
                        }
                        Text {
                            text: qsTr("项目时长：%1").arg(page.formatTime(page.projectDurationMs()))
                            color: Theme.secondaryText
                        }
                        Text {
                            text: qsTr("目标 BPM：%1").arg(Math.round(editor.targetBpm))
                            color: Theme.secondaryText
                        }
                    }

                    Button {
                        objectName: "confirmExportButton"
                        text: editor.busy ? qsTr("处理中…") : qsTr("导出音频")
                        icon.source: Theme.icon("download-line")
                        highlighted: true
                        enabled: !editor.busy && editor.tracks.some(
                            function(track) { return track.hasFile })
                        onClicked: {
                            const rates = [44100, 48000, 88200, 96000,
                                           176400, 192000]
                            const scopes = ["project", "loop", "selected"]
                            editor.exportProjectScope(
                                scopes[exportScopeBox.currentIndex],
                                editor.loopStartMs, editor.loopEndMs,
                                outputDirectory.text,
                                outputFormatBox.currentText.toLowerCase(),
                                rates[outputSampleRateBox.currentIndex],
                                outputChannelBox.currentIndex === 0 ? 2 : 1)
                        }
                    }
                }
            }
        }
    }

    FileDropArea {
        id: editorDropArea
        objectName: "editorDropArea"
        anchors.fill: parent
        z: 100
        onUrlsDropped: function(urls) {
            editor.queueFiles(urls)
        }
    }

    Connections {
        target: editor
        function onTargetBpmChanged() {
            previewDspTimer.restart()
        }
        function onKeepPitchChanged() {
            previewDspTimer.restart()
        }
        function onSelectedTrackChanged() {
            if (!page.isTrackSelected(editor.selectedTrack)) {
                page.selectedTrackIndexes = [editor.selectedTrack]
                page.selectionAnchor = editor.selectedTrack
            }
            previewDspTimer.restart()
        }
        function onTracksChanged() {
            const resume = page.previewIsCurrent
                           && AudioPreviewController.playing
            if (page.previewIsCurrent)
                AudioPreviewController.stop()
            if (resume) {
                page.previewAutoResume = true
                previewDspTimer.restart()
            }
        }
    }

    Connections {
        target: AudioPreviewController

        function onStateChanged() {
            if (!page.previewIsCurrent)
                return
            page.playheadMs = AudioPreviewController.positionMs
        }
    }
}
