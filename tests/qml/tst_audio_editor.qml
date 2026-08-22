import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "AudioEditorReferenceTimeline"
    when: windowShown
    visible: true
    width: 1672
    height: 849

    Component {
        id: pageComponent
        Window {
            width: testCase.width
            height: testCase.height
            visible: true
            property alias editorPage: page
            AudioEditorPage { id: page; anchors.fill: parent }
        }
    }

    Component {
        id: shellComponent
        AudioToolsWindow { visible: true }
    }

    property var host
    property var page

    function init() {
        if (AudioEditorController.hasDocument && !AudioEditorController.busy) {
            if (!AudioEditorController.clearDocument())
                verify(AudioEditorController.confirmDiscardAndOpen(),
                       "modified document could not be explicitly discarded")
            compare(AudioEditorController.hasDocument, false)
        }
        host = createTemporaryObject(pageComponent, testCase)
        verify(host)
        page = host.editorPage
        verify(page)
        tryVerify(function() { return page.width > 0 && page.height > 0 })
    }

    function verifyGeometry(name, x, y, width, height) {
        const item = findChild(page, name)
        verify(item, "missing " + name)
        verify(Math.abs(item.x - x) <= 2, name + " x=" + item.x)
        verify(Math.abs(item.y - y) <= 2, name + " y=" + item.y)
        verify(Math.abs(item.width - width) <= 2,
               name + " width=" + item.width)
        verify(Math.abs(item.height - height) <= 2,
               name + " height=" + item.height)
        return item
    }

    function test_referenceGeometryAt1672x941ShellContent() {
        host.width = 1672
        host.height = 849
        wait(0)
        verifyGeometry("editorMainColumn", 0, 0, 1300, 849)
        verifyGeometry("editorInspector", 1300, 0, 372, 849)
        verifyGeometry("editorCommandBar", 12, 13, 1278, 61)
        verifyGeometry("fileSummaryBar", 12, 88, 1278, 48)
        verify(findChild(page, "fileSummaryIcon"))
        verifyGeometry("editorTimelineWorkspace", 12, 152, 1276, 364)
        verifyGeometry("editorTrackHeader", 12, 202, 96, 284)
        verifyGeometry("editorTimeRuler", 118, 152, 1167, 50)
        verifyGeometry("editorWaveformCanvas", 118, 202, 1167, 284)
        verifyGeometry("editorTimelineScrollbar", 118, 500, 1167, 16)
        verifyGeometry("editorRecordingTransport", 12, 527, 556, 130)
        verifyGeometry("editorPlaybackTransport", 580, 527, 708, 130)
        verifyGeometry("editorShortcutCard", 12, 667, 1276, 157)
        verifyGeometry("editorStatusBar", 0, 824, 1300, 25)
    }

    function test_composedToolsShellHasOnePageOwnedSpaceShortcut() {
        const shell = createTemporaryObject(shellComponent, testCase)
        verify(shell)
        tryVerify(function() { return shell.visible })
        const shellPage = findChild(shell, "audioEditorPage")
        verify(shellPage)
        compare(shellPage.playbackShortcutEnabled, false)
        compare(findChild(shell, "audioToolsSpaceShortcut"), null)
    }

    function test_toolbarExactOrderAndClearKeepsDocument() {
        const names = [
            "importAudio", "saveProject", "select", "split", "delete",
            "crop", "copy", "paste", "fadeIn", "fadeOut", "mute", "clear"
        ]
        const labels = [
            "导入音频", "保存工程", "选择", "分割", "删除", "裁剪",
            "复制", "粘贴", "淡入", "淡出", "静音片段", "清除"
        ]
        let previousX = -1
        for (let index = 0; index < names.length; ++index) {
            const button = findChild(page, "editorCommand_" + names[index])
            verify(button, "missing toolbar command " + names[index])
            compare(button.label, labels[index])
            verify(button.x > previousX, names[index] + " is out of order")
            previousX = button.x
        }

        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(100, 200))
        verify(AudioEditorController.setActiveTool("scissors"))
        const clearButton = findChild(page, "editorCommand_clear")
        mouseClick(clearButton)
        compare(AudioEditorController.hasDocument, true)
        compare(AudioEditorController.selectionStart, -1)
        compare(AudioEditorController.activeTool, "select")
    }

    function test_inspectorAThroughEAndRemovedContradictions() {
        const names = [
            "inspectorRecordingGroup", "inspectorTempoGroup",
            "inspectorPitchGroup", "inspectorPreservePitchGroup",
            "inspectorExportGroup"
        ]
        let previousY = -1
        for (const name of names) {
            const group = findChild(page, name)
            verify(group, "missing " + name)
            verify(group.y > previousY, name + " is out of order")
            previousY = group.y
        }
        compare(AudioEditorController.formantPreservationSupported, false)
        compare(AudioEditorController.recordingSupported, false)
        compare(AudioEditorController.bpmDetectionSupported, false)
        compare(AudioEditorController.timePitchSupported, false)
        compare(AudioEditorController.playbackSupported, false)
        compare(AudioEditorController.exportSupported, false)
        compare(findChild(page, "inspectorFormantRow"), null)
        compare(findChild(page, "overviewNavigator"), null)
        compare(findChild(page, "recordingInspector"), null)
        compare(findChild(page, "timePitchInspector"), null)
        compare(findChild(page, "editorCommand_noiseReduction"), null)
        compare(findChild(page, "editorCommand_gain"), null)
        compare(findChild(page, "editorCommand_insertSilence"), null)
        compare(findChild(page, "editorCommand_clearDocument"), null)
        compare(findChild(page, "editorCommand_exportMenu"), null)
    }

    function test_keyboardToolSelectionSplitAndEscape() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        page.forceActiveFocus()
        keyClick(Qt.Key_2, Qt.ControlModifier)
        compare(AudioEditorController.activeTool, "scissors")
        keyClick(Qt.Key_1, Qt.ControlModifier)
        compare(AudioEditorController.activeTool, "select")
        verify(AudioEditorController.setSelection(12000, 36000))
        verify(AudioEditorController.setActiveTool("scissors"))
        keyClick(Qt.Key_Escape)
        compare(AudioEditorController.selectionStart, -1)
        compare(AudioEditorController.activeTool, "select")

        verify(AudioEditorController.seekFrame(48000))
        compare(AudioEditorController.timelineEventViews.length, 1)
        keyClick(Qt.Key_S)
        compare(AudioEditorController.timelineEventViews.length, 2)
    }

    function test_futureBackendActionsStayDisabledWhileTimelineActionsTrackState() {
        const exportButton = findChild(page, "editorExportButton")
        const deleteButton = findChild(page, "editorCommand_delete")
        verify(exportButton && deleteButton)
        compare(exportButton.enabled, false)
        compare(deleteButton.enabled, false)

        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        compare(exportButton.enabled, false)
        verify(AudioEditorController.setSelection(100, 200))
        tryCompare(deleteButton, "enabled", true)
    }

    function test_futureTransportControlsLookAndReadDisabled() {
        const microphone = findChild(page, "recordingMicrophoneButton")
        const recording = findChild(page, "recordingToggleButton")
        const play = findChild(page, "editorPrimaryPlayButton")
        const narrowPlay = findChild(page, "editorNarrowPlaybackAccess")
        for (const control of [microphone, recording, play, narrowPlay]) {
            verify(control)
            compare(control.enabled, false)
            verify(control.opacity <= 0.55,
                   control.objectName + " does not look disabled")
            verify(control.Accessible.name.length > 0)
            compare(control.Accessible.role, Accessible.Button)
        }
    }

    function test_splitToolbarOnlySelectsScissorsMode() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.seekFrame(48000))
        const splitButton = findChild(page, "editorCommand_split")
        verify(splitButton)
        compare(AudioEditorController.timelineEventViews.length, 1)

        mouseClick(splitButton)

        compare(AudioEditorController.activeTool, "scissors")
        compare(AudioEditorController.timelineEventViews.length, 1)
    }

    function test_playheadDragCommitsOneSeekOnRelease() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const handle = findChild(page, "editorPlayheadHandle")
        verify(handle)
        verify(AudioEditorController.seekFrame(24000))
        compare(AudioEditorController.playheadFrame, 24000)

        mousePress(handle, handle.width / 2, handle.height / 2,
                   Qt.LeftButton)
        mouseMove(handle, handle.width - 1, handle.height / 2, 0)
        compare(AudioEditorController.playheadFrame, 24000)
        mouseRelease(handle, handle.width - 1, handle.height / 2,
                     Qt.LeftButton)
        verify(AudioEditorController.playheadFrame > 24000)
    }

    function test_realWheelEventUsesContractZoomFactors() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const interaction = findChild(page, "editorWaveformInteraction")
        verify(interaction)
        AudioEditorController.viewport.setViewportWidth(interaction.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 48000))
        const before = AudioEditorController.viewport.visibleFrameCount

        mouseWheel(interaction, interaction.width / 2, interaction.height / 2,
                   0, 120, Qt.NoButton, Qt.ControlModifier)
        compare(AudioEditorController.viewport.visibleFrameCount,
                Math.round(before / 1.25))
        mouseWheel(interaction, interaction.width / 2, interaction.height / 2,
                   0, -120, Qt.NoButton, Qt.ControlModifier)
        verify(Math.abs(AudioEditorController.viewport.visibleFrameCount - before) <= 1)
    }

    function test_mappingAndDirectManipulationShareViewport() {
        verify(AudioEditorController.createUntitledDocument(96000, 2,
                                                             96000 * 60 * 60 * 2))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(123456789,
                                                               123456789 + 960000))
        const frame = AudioEditorController.viewport.frameAtPixel(canvas.width * 0.375)
        const pixel = AudioEditorController.viewport.pixelAtFrame(frame)
        verify(Math.abs(pixel - canvas.width * 0.375) <= 0.01)
        compare(canvas.frameAtCanvasPixel(canvas.width * 0.375), frame)
        verify(findChild(canvas, "editorSelectionStartHandle"))
        verify(findChild(canvas, "editorSelectionEndHandle"))
        verify(findChild(canvas, "editorPlayheadHandle"))
    }

    function test_responsivePlaybackAndInspectorAccess_data() {
        return [
            { tag: "desktop", w: 1280, h: 628, access: "editorInspectorScroller" },
            { tag: "narrow", w: 880, h: 468, access: "editorInspectorAccess" }
        ]
    }

    function test_responsivePlaybackAndInspectorAccess(data) {
        host.width = data.w
        host.height = data.h
        wait(0)
        const playback = findChild(page, "editorPlaybackTransport")
        const exportGroup = findChild(page, "inspectorExportGroup")
        const access = findChild(page, data.access)
        verify(playback && playback.visible)
        verify(exportGroup)
        verify(access && access.visible)
        verify(findChild(page, "editorCommand_importAudio").visible)
        if (data.w === 880) {
            const commandBar = findChild(page, "editorCommandBar")
            verify(access.y >= commandBar.y + commandBar.height,
                   "narrow inspector access overlaps the command toolbar")
            const playAccess = findChild(page, "editorNarrowPlaybackAccess")
            verify(playAccess && playAccess.visible)
            compare(playAccess.enabled, false)
            verify(playAccess.y >= commandBar.y + commandBar.height)
            verify(playAccess.y + playAccess.height <= page.height)
            verify(playAccess.x + playAccess.width + 8 <= access.x,
                   "narrow playback and inspector access overlap")

            mouseClick(access)
            const inspector = findChild(page, "editorInspector")
            const scroller = findChild(page, "editorInspectorScroller")
            const exportButton = findChild(page, "editorExportButton")
            verify(inspector.visible && scroller && exportButton)
            scroller.contentY = Math.max(0,
                scroller.contentHeight - scroller.height)
            wait(0)
            const exportPosition = exportButton.mapToItem(page, 0, 0)
            verify(exportPosition.y >= 0)
            verify(exportPosition.y + exportButton.height <= page.height,
                   "narrow export action cannot be fully scrolled into view")
            mouseClick(access)
        }
    }

    function test_exportGroupShowsPersistedReadOnlyFields() {
        for (const name of ["editorExportCodec", "editorExportSampleRate",
                            "editorExportBitDepth", "editorExportChannels",
                            "editorExportBitRate", "editorExportDirectory"]) {
            const field = findChild(page, name)
            verify(field, "missing read-only export field " + name)
            verify(field.text.length > 0)
        }
        compare(findChild(page, "audioEditorExportDialog"), null)
        compare(findChild(page, "editorExportButton").enabled, false)
    }
}
