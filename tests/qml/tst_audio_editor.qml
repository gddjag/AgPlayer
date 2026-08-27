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

    Component { id: signalSpyComponent; SignalSpy {} }

    property var host
    property var page

    function init() {
        AudioToolsController.selectTool(0)
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

    function test_recordingSurfaceIsAbsent() {
        compare(findChild(page, "editorRecordingTransport"), null)
        compare(findChild(page, "inspectorRecordingGroup"), null)
        compare(findChild(page, "editorRecordShortcut"), null)
        compare(findChild(page, "editorPauseRecordingShortcut"), null)
        compare(findChild(page, "editorStopRecordingShortcut"), null)
    }

    function test_switchingAwayDeactivatesAndReturningActivatesEditor() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const shell = createTemporaryObject(shellComponent, testCase)
        verify(shell)
        const deactivated = createTemporaryObject(signalSpyComponent, testCase,
            { target: AudioEditorController, signalName: "deactivated" })
        const activated = createTemporaryObject(signalSpyComponent, testCase,
            { target: AudioEditorController, signalName: "activated" })
        verify(deactivated.valid)
        verify(activated.valid)

        AudioToolsController.selectTool(1)
        tryCompare(deactivated, "count", 1)
        compare(AudioEditorController.hasDocument, true)
        AudioToolsController.selectTool(0)
        tryCompare(activated, "count", 1)
        verify(AudioEditorController.setSelection(100, 200))
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
        verifyGeometry("editorMainColumn", 0, 0, 1328, 849)
        verifyGeometry("editorInspector", 1328, 0, 344, 849)
        verifyGeometry("editorCommandBar", 12, 13, 1306, 61)
        verifyGeometry("fileSummaryBar", 12, 88, 1306, 48)
        verify(findChild(page, "fileSummaryIcon"))
        verifyGeometry("editorTimelineWorkspace", 12, 152, 1304, 364)
        verifyGeometry("editorTrackHeader", 12, 202, 96, 284)
        verifyGeometry("editorTimeRuler", 118, 152, 1195, 50)
        verifyGeometry("editorWaveformCanvas", 118, 202, 1195, 284)
        verifyGeometry("editorTimelineScrollbar", 118, 500, 1195, 16)
        verifyGeometry("editorPlaybackTransport", 12, 527, 1304, 130)
        verifyGeometry("editorShortcutCard", 12, 667, 1304, 157)
        verifyGeometry("editorStatusBar", 0, 824, 1328, 25)

        compare(findChild(page, "inspectorTempoTitle").text,
                "A. 速度 / BPM")
        compare(findChild(page, "inspectorPitchTitle").text,
                "B. 升调降调")
        compare(findChild(page, "inspectorPreservePitchTitle").text,
                "C. 保持音调")
        compare(findChild(page, "inspectorExportTitle").text,
                "D. 导出设置")
    }

    function test_continuousMainAndInspectorWidths_data() {
        return [
            { tag: "wide", width: 1800, inspector: 372 },
            { tag: "reference", width: 1672, inspector: 344 },
            { tag: "medium", width: 1500, inspector: 320 },
            { tag: "desktop", width: 1280, inspector: 320 },
            { tag: "compact", width: 880, inspector: 0 }
        ]
    }

    function test_continuousMainAndInspectorWidths(data) {
        host.width = data.width
        host.height = data.width === 880 ? 468 : 849
        wait(0)
        const main = findChild(page, "editorMainColumn")
        const inspector = findChild(page, "editorInspector")
        const transport = findChild(page, "editorPlaybackTransport")
        const exportButton = findChild(page, "editorExportButton")
        const summary = findChild(page, "fileSummaryBar")
        const summaryContent = findChild(page, "fileSummaryContent")
        verify(main && inspector && transport && exportButton
               && summary && summaryContent)
        compare(Math.round(inspector.visible ? inspector.width : 0),
                data.inspector)
        verify(Math.abs(main.width
                        + (inspector.visible ? inspector.width : 0)
                        - page.width) <= 1,
               "main and visible inspector must exactly fill " + data.width)
        verify(transport.visible)
        verify(summaryContent.implicitWidth <= summary.width,
               "file summary must contract without clipping at " + data.width)
        if (data.width === 880) {
            const access = findChild(page, "editorInspectorAccess")
            verify(access && access.visible)
            mouseClick(access)
            verify(inspector.visible)
            const scroller = findChild(page, "editorInspectorScroller")
            scroller.contentY = Math.max(0,
                scroller.contentHeight - scroller.height)
            wait(0)
            const exportPosition = exportButton.mapToItem(page, 0, 0)
            verify(exportPosition.y >= 0)
            verify(exportPosition.y + exportButton.height <= page.height)
            mouseClick(access)
        }
    }

    function test_timeRulerHasReferenceMinorTicks() {
        const tick = findChild(page, "editorRulerMinorTick")
        verify(tick, "timeline ruler must expose dense minor ticks")
        verify(tick.visible)
        verify(tick.height >= 4 && tick.height <= 7)
    }

    function test_composedToolsShellHasOneRealSpaceShortcut() {
        const shell = createTemporaryObject(shellComponent, testCase)
        verify(shell)
        tryVerify(function() { return shell.visible })
        const shellPage = findChild(shell, "audioEditorPage")
        verify(shellPage)
        verify(findChild(shell, "audioToolsSpaceShortcut"))
        compare(findChild(shellPage, "editorSpaceShortcut"), null)
        const shortcutText = findChild(shellPage,
                                       "editorShortcutFirstGroup_0").text
        verify(shortcutText.indexOf("空格 = 播放 / 暂停") >= 0)
        compare(shortcutText.indexOf("Phase"), -1)
    }

    function test_spaceTransportOwnsFocusAcrossNonTextControls() {
        const shell = createTemporaryObject(shellComponent, testCase)
        verify(shell)
        tryVerify(function() { return shell.visible })
        shell.requestActivate()
        tryVerify(function() { return shell.active })
        const shellPage = findChild(shell, "audioEditorPage")
        const spaceShortcut = findChild(shell, "audioToolsSpaceShortcut")
        verify(shellPage && spaceShortcut)
        compare(spaceShortcut.context, Qt.ApplicationShortcut)

        const controls = [
            findChild(shellPage, "editorCommand_split"),
            findChild(shellPage, "editorPrimaryPlayButton"),
            findChild(shellPage, "inspectorSpeedSlider"),
            findChild(shellPage, "editorExportCodec")
        ]
        for (let index = 0; index < controls.length; ++index) {
            const control = controls[index]
            verify(control, "missing Space focus regression control")
            if (index < 2)
                compare(control.focusPolicy, Qt.NoFocus)
        }

        if (AudioEditorController.hasDocument)
            verify(AudioEditorController.clearDocument())
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setActiveTool("select"))
        const previousSliderValue = controls[2].value
        const previousComboIndex = controls[3].currentIndex
        for (const control of controls) {
            control.forceActiveFocus()
            tryVerify(function() { return control.activeFocus })
            verify(spaceShortcut.enabled,
                   "Space shortcut disabled for " + control.objectName)
            compare(AudioEditorController.activeTool, "select")
            compare(controls[2].value, previousSliderValue)
            compare(controls[3].currentIndex, previousComboIndex)
            compare(controls[3].popup.visible, false)
        }
        keyClick(Qt.Key_Space)
        tryVerify(function() { return AudioEditorController.playing },
                  1000,
                  controls[3].objectName + ": "
                      + AudioEditorController.errorMessage)
        compare(AudioEditorController.activeTool, "select")
        compare(controls[2].value, previousSliderValue)
        compare(controls[3].currentIndex, previousComboIndex)
        compare(controls[3].popup.visible, false)
        keyClick(Qt.Key_Space)
        tryCompare(AudioEditorController, "playing", false)
        AudioEditorController.deactivate()
        AudioEditorController.activate()
    }

    function test_toolbarExactOrderAndClearKeepsDocument() {
        const names = [
            "importAudio", "saveProject", "select", "split", "delete",
            "crop", "copy", "paste", "fadeIn", "fadeOut", "mute",
            "noiseReduction", "clear"
        ]
        const labels = [
            "导入音频", "保存工程", "选择", "分割", "删除", "裁剪",
            "复制", "粘贴", "淡入", "淡出", "静音片段", "降噪", "清除"
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

    function test_toolbarReferenceButtonWidthsAt1672() {
        host.width = 1672
        host.height = 849
        wait(0)
        const expectedWidths = [132, 140, 80, 80, 86, 83, 81,
                                91, 77, 75, 95, 73, 82]
        const names = ["importAudio", "saveProject", "select", "split",
                       "delete", "crop", "copy", "paste", "fadeIn",
                       "fadeOut", "mute", "noiseReduction", "clear"]
        for (let index = 0; index < names.length; ++index) {
            const button = findChild(page, "editorCommand_" + names[index])
            verify(button)
            verify(Math.abs(button.width - expectedWidths[index]) <= 2,
                   names[index] + " width=" + button.width)
        }
    }

    function test_toolbarNeverOverflowsItsAvailableWidth() {
        for (const dimensions of [{ width: 1280, height: 720 },
                                  { width: 880, height: 560 }]) {
            host.width = dimensions.width
            host.height = dimensions.height
            wait(0)
            const bar = findChild(page, "editorCommandBar")
            const clear = findChild(page, "editorCommand_clear")
            const first = findChild(page, "editorCommand_importAudio")
            verify(bar && clear && first)
            verify(clear.x + clear.width <= bar.width + 0.5,
                   dimensions.width + " toolbar clips clear x=" + clear.x
                   + " width=" + clear.width + " first=" + first.x
                   + " bar=" + bar.width)
        }
    }

    function test_inspectorNonRecordingControlsMatchReference() {
        const names = [
            "inspectorTempoGroup",
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
        for (const name of [
                 "inspectorBpmInput", "inspectorDetectBpmButton",
                 "inspectorSpeedSlider", "inspectorSpeedValue",
                 "inspectorSpeedResetButton", "inspectorPitchMinus",
                 "inspectorPitchSlider", "inspectorPitchPlus",
                 "inspectorPitchValue", "inspectorPreservePitchSwitch",
                 "inspectorFormantRow", "inspectorFormantSwitch",
                 "editorExportCodec", "editorExportSampleRate",
                 "editorExportBitDepth", "editorExportChannels",
                 "editorExportBitRate", "editorExportDirectory",
                 "editorExportBrowseButton", "editorExportButton"]) {
            const control = findChild(page, name)
            verify(control, "missing reference inspector control " + name)
            verify(control.visible, name + " must be visible")
        }
        compare(findChild(page, "overviewNavigator"), null)
        compare(findChild(page, "recordingInspector"), null)
        compare(findChild(page, "timePitchInspector"), null)
        verify(findChild(page, "editorCommand_noiseReduction"))
        compare(findChild(page, "editorCommand_gain"), null)
        compare(findChild(page, "editorCommand_insertSilence"), null)
        compare(findChild(page, "editorCommand_clearDocument"), null)
        compare(findChild(page, "editorCommand_exportMenu"), null)
    }

    function test_inspectorReferenceGeometryAndCollapsibleHeaders() {
        const names = ["inspectorTempoGroup",
                       "inspectorPitchGroup", "inspectorPreservePitchGroup",
                       "inspectorExportGroup"]
        const arrows = ["inspectorTempoCollapse",
                        "inspectorPitchCollapse", "inspectorPreservePitchCollapse",
                        "inspectorExportCollapse"]
        const heights = [129, 107, 104, 276]
        const positions = [0, 135, 248, 358]
        for (let index = 0; index < names.length; ++index) {
            const group = findChild(page, names[index])
            const arrow = findChild(page, arrows[index])
            verify(group && arrow)
            verify(Math.abs(group.y - positions[index]) <= 2,
                   names[index] + " y=" + group.y)
            verify(Math.abs(group.height - heights[index]) <= 2,
                   names[index] + " height=" + group.height)
            verify(arrow.Accessible.name.length > 0)
        }

        const tempo = findChild(page, names[0])
        const pitch = findChild(page, names[1])
        mouseClick(findChild(page, arrows[0]))
        compare(tempo.height, 38)
        tryCompare(pitch, "y", tempo.y + 44)
        mouseClick(findChild(page, arrows[0]))
        compare(tempo.height, heights[0])
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

    function test_backendActionsTrackCapabilitiesWhileTimelineActionsTrackState() {
        const exportButton = findChild(page, "editorExportButton")
        const deleteButton = findChild(page, "editorCommand_delete")
        verify(exportButton && deleteButton)
        compare(exportButton.enabled,
                AudioEditorController.exportSupported
                && AudioEditorController.actionEnabled("editor.export"))
        compare(deleteButton.enabled, false)

        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        compare(exportButton.enabled,
                AudioEditorController.exportSupported
                && AudioEditorController.actionEnabled("editor.export"))
        verify(AudioEditorController.setSelection(100, 200))
        tryCompare(deleteButton, "enabled", true)
    }

    function test_transportControlsKeepReferenceAppearanceWhenUnavailable() {
        const play = findChild(page, "editorPrimaryPlayButton")
        const narrowPlay = findChild(page, "editorNarrowPlaybackAccess")
        for (const control of [play, narrowPlay]) {
            verify(control)
            verify(control.opacity >= 0.9,
                   control.objectName + " lost the reference appearance")
            verify(control.Accessible.name.length > 0)
            compare(control.Accessible.role, Accessible.Button)
        }
    }

    function test_referenceTransportShortcutAndStatusCopy() {
        const playBackground = findChild(page, "editorPrimaryPlayBackground")
        const shortcutFirst = findChild(page, "editorShortcutFirstGroup_0")
        const shortcutFade = findChild(page, "editorShortcutSecondGroup_4")
        const shortcutEnvelope = findChild(page, "editorShortcutSecondGroup_5")
        verify(playBackground && shortcutFirst && shortcutFade && shortcutEnvelope)
        verify(shortcutFirst.text.indexOf("空格 = 播放 / 暂停") >= 0)
        compare(shortcutFirst.text.indexOf("R = 开始录音"), -1)
        compare(shortcutFirst.text.indexOf("Shift+R"), -1)
        compare(shortcutFirst.text.indexOf("Ctrl+R = 停止并保存"), -1)
        compare(shortcutFade.text, "拖拽右上角 = 调整淡出")
        compare(shortcutEnvelope.text, "双击音量线 = 添加控制点")
        compare(shortcutFirst.text.indexOf("Phase"), -1)
        compare(shortcutFade.text.indexOf("Phase"), -1)
        compare(findChild(page, "editorStatusBar").visible, false)
    }

    function test_shortcutCardUsesKeyboardIconAndRealDividers() {
        const keyboard = findChild(page, "editorShortcutKeyboardIcon")
        const divider = findChild(page, "editorShortcutDivider")
        const secondDivider = findChild(page, "editorShortcutSecondDivider")
        verify(keyboard && divider && secondDivider)
        verify(keyboard.source.toString().indexOf("keyboard-box-line.svg") >= 0)
        compare(divider.width, 1)
        compare(secondDivider.width, 1)
        verify(divider.height >= 18)
        verify(secondDivider.height >= 18)
    }

    function test_shortcutCardUsesStructuredReferenceGroups() {
        const firstRow = findChild(page, "editorShortcutFirstRow")
        const secondRow = findChild(page, "editorShortcutSecondRow")
        verify(firstRow && secondRow)
        compare(firstRow.groupCount, 5)
        compare(firstRow.dividerCount, 4)
        compare(secondRow.groupCount, 6)
        compare(secondRow.dividerCount, 5)
        compare(findChild(page, "editorShortcutFirstGroup_0").text,
                "空格 = 播放 / 暂停")
        compare(findChild(page, "editorShortcutFirstGroup_4").text,
                "Ctrl+Z / Y = 撤销 / 重做")
        compare(findChild(page, "editorShortcutSecondGroup_5").text,
                "双击音量线 = 添加控制点")
    }

    function test_shortcutReferenceRowsFillTheCardWithoutClippingLabels() {
        host.width = 1672
        host.height = 849
        wait(0)
        const card = findChild(page, "editorShortcutCard")
        const firstRow = findChild(page, "editorShortcutFirstRow")
        const secondRow = findChild(page, "editorShortcutSecondRow")
        verify(card && firstRow && secondRow)

        for (const row of [firstRow, secondRow]) {
            compare(row.x, 22)
            compare(row.width, card.width - 44)
            compare(row.x + row.width, card.width - 22)
        }

        const lastFirstDivider = findChild(page, "editorShortcutFirstDivider_3")
        const lastSecondDivider = findChild(page, "editorShortcutSecondRowDivider_4")
        verify(lastFirstDivider && lastSecondDivider)
        const firstDividerPosition = lastFirstDivider.mapToItem(firstRow, 0, 0)
        const secondDividerPosition = lastSecondDivider.mapToItem(secondRow, 0, 0)
        verify(firstDividerPosition.x > firstRow.width * 0.70)
        verify(secondDividerPosition.x > secondRow.width * 0.70)

        for (let index = 0; index < firstRow.groupCount; ++index) {
            const label = findChild(page, "editorShortcutFirstGroup_" + index)
            verify(label)
            verify(label.width >= label.implicitWidth,
                   "first row label " + index + " is clipped")
        }
        for (let index = 0; index < secondRow.groupCount; ++index) {
            const label = findChild(page, "editorShortcutSecondGroup_" + index)
            verify(label)
            verify(label.width >= label.implicitWidth,
                   "second row label " + index + " is clipped")
        }
    }

    function test_referenceTransportUsesFineControlsAndHoverShortcuts() {
        const toStart = findChild(page, "editorPlaybackToStartButton")
        const rewind = findChild(page, "editorPlaybackRewindButton")
        const play = findChild(page, "editorPrimaryPlayButton")
        const forward = findChild(page, "editorPlaybackForwardButton")
        const next = findChild(page, "editorPlaybackNextButton")
        const stop = findChild(page, "editorPlaybackStopButton")
        const startShortcut = findChild(page, "editorToStartShortcut")
        const rewindShortcut = findChild(page, "editorRewindShortcut")
        const forwardShortcut = findChild(page, "editorForwardShortcut")
        const stopShortcut = findChild(page, "editorStopPlaybackShortcut")

        for (const control of [toStart, rewind, play, forward, next, stop]) {
            verify(control, "missing reference transport control")
            compare(control.toolTipDelay, 500)
            verify(control.toolTipText.length > 0)
        }
        compare(toStart.toolTipText, "上一段（Home）")
        compare(rewind.toolTipText, "后退 5 秒（←）")
        compare(play.toolTipText, "播放 / 暂停（Space）")
        compare(forward.toolTipText, "前进 5 秒（→）")
        compare(next.toolTipText, "下一段")
        compare(stop.toolTipText, "停止（Ctrl+Space）")

        compare(startShortcut.sequence.toString(), "Home")
        compare(rewindShortcut.sequence.toString(), "Left")
        compare(forwardShortcut.sequence.toString(), "Right")
        compare(stopShortcut.sequence.toString(), "Ctrl+Space")
        verify(rewind.icon.source.toString().indexOf("rewind-fill.svg") >= 0)
        verify(forward.icon.source.toString().indexOf("speed-fill.svg") >= 0)
        verify(stop.icon.source.toString().indexOf("stop-fill.svg") >= 0)
        compare(toStart.icon.width, 32)
        compare(rewind.icon.width, 32)
        compare(play.icon.width, 42)
        compare(forward.icon.width, 32)
        compare(next.icon.width, 32)
        compare(stop.icon.width, 32)

        for (const sliderName of ["editorTrackGain", "inspectorSpeedSlider",
                                  "inspectorPitchSlider"]) {
            const slider = findChild(page, sliderName)
            verify(slider, "missing editor slider " + sliderName)
            compare(slider.visibleGrooveThickness, 4)
            compare(slider.thumbDiameter, 14)
            verify(slider.pointerHitExtent >= 28)
            const groove = findChild(slider, "editorSliderGroove")
            verify(groove)
            if (slider.orientation === Qt.Horizontal)
                verify(Math.abs(groove.height - 4) <= 1)
            else
                verify(Math.abs(groove.width - 4) <= 1)
        }
        for (const switchName of ["inspectorPreservePitchSwitch",
                                   "inspectorFormantSwitch"]) {
            const control = findChild(page, switchName)
            verify(control)
            compare(control.width, 40)
            compare(control.height, 22)
        }
    }

    function test_editorSlidersCenterTracksAndHandlesInTheirHitArea() {
        for (const sliderName of ["editorTrackGain", "inspectorSpeedSlider",
                                  "inspectorPitchSlider"]) {
            const slider = findChild(page, sliderName)
            verify(slider)
            const groove = findChild(slider, "editorSliderGroove")
            const handle = slider.handle
            verify(groove && handle)
            if (slider.orientation === Qt.Horizontal) {
                verify(Math.abs(groove.y + groove.height / 2
                                - slider.height / 2) <= 0.5,
                       sliderName + " horizontal groove is not centered")
                verify(Math.abs(handle.y + handle.height / 2
                                - slider.height / 2) <= 0.5,
                       sliderName + " horizontal thumb is not centered")
            } else {
                verify(Math.abs(groove.x + groove.width / 2
                                - slider.width / 2) <= 0.5,
                       sliderName + " vertical groove is not centered")
                verify(Math.abs(handle.x + handle.width / 2
                                - slider.width / 2) <= 0.5,
                       sliderName + " vertical thumb is not centered")
            }
            verify(handle.x >= 0 && handle.y >= 0)
            verify(handle.x + handle.width <= slider.width + 0.5)
            verify(handle.y + handle.height <= slider.height + 0.5)
        }
    }

    function test_editorSliderActiveSegmentStaysInsideFineGroove() {
        for (const sliderName of ["editorTrackGain", "inspectorSpeedSlider",
                                  "inspectorPitchSlider"]) {
            const slider = findChild(page, sliderName)
            const groove = findChild(slider, "editorSliderGroove")
            const active = findChild(slider, "editorSliderActiveSegment")
            verify(slider && groove && active)
            verify(groove.clip)
            const activePosition = active.mapToItem(slider, 0, 0)
            const groovePosition = groove.mapToItem(slider, 0, 0)
            if (slider.orientation === Qt.Horizontal) {
                compare(active.height, 4)
                verify(activePosition.y >= groovePosition.y)
                verify(activePosition.y + active.height
                       <= groovePosition.y + groove.height + 0.5)
            } else {
                compare(active.width, 4)
                verify(activePosition.x >= groovePosition.x)
                verify(activePosition.x + active.width
                       <= groovePosition.x + groove.width + 0.5)
            }
        }
    }

    function test_fourthPlaybackButtonAdvancesFiveSeconds() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 480000))
        verify(AudioEditorController.seekMs(100))
        const toEnd = findChild(page, "editorPlaybackForwardButton")
        verify(toEnd)
        compare(toEnd.Accessible.name, "快进 5 秒")
        mouseClick(toEnd)
        compare(AudioEditorController.positionMs, 5100)
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

    function test_selectToolDragOnEventCreatesSampleExactSelection() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        const body = findChild(canvas, "editorEventBodyInteraction")
        verify(body, "event body must expose the selection interaction seam")
        const fromX = Math.round(body.width * 0.25)
        const toX = Math.round(body.width * 0.75)
        const fromPoint = body.mapToItem(canvas, fromX, body.height / 2)
        const toPoint = body.mapToItem(canvas, toX, body.height / 2)
        const expectedStart = canvas.frameAtCanvasPixel(fromPoint.x)
        const expectedEnd = canvas.frameAtCanvasPixel(toPoint.x)
        const originalStart = AudioEditorController.timelineEventViews[0].timelineStart

        mouseDrag(body, fromX, body.height * 0.7,
                  toX - fromX, 0, Qt.LeftButton, Qt.NoModifier, 30)

        compare(AudioEditorController.selectionStart, expectedStart)
        compare(AudioEditorController.selectionEnd, expectedEnd)
        compare(AudioEditorController.timelineEventViews[0].timelineStart,
                originalStart)
    }

    function test_selectionDragIsTransientUntilRelease() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        const body = findChild(canvas, "editorEventBodyInteraction")
        verify(body)
        const fromX = Math.round(body.width * 0.2)
        const toX = Math.round(body.width * 0.7)

        mousePress(body, fromX, body.height * 0.7, Qt.LeftButton)
        mouseMove(body, toX, body.height * 0.7, 0)
        compare(AudioEditorController.selectionStart, -1)
        verify(findChild(canvas, "editorSelectionDashedBorder").visible)
        mouseRelease(body, toX, body.height * 0.7, Qt.LeftButton)
        verify(AudioEditorController.selectionStart >= 0)
        verify(AudioEditorController.selectionEnd
               > AudioEditorController.selectionStart)
    }

    function test_selectionUsesOneDashedBorderAndShowsExactLabels() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(12000, 36000))
        const canvas = findChild(page, "editorWaveformCanvas")
        const border = findChild(canvas, "editorSelectionDashedBorder")
        const eventBoundary = findChild(canvas, "editorEventVisualBoundary")
        const capsule = findChild(canvas, "editorSelectionHandoffCapsule")
        const label = findChild(canvas, "editorSelectionHandoffLabel")
        const duration = findChild(canvas, "editorSelectionDuration")
        const durationCapsule = findChild(canvas,
                                          "editorSelectionDurationCapsule")
        verify(border && eventBoundary && capsule && label && duration
               && durationCapsule)
        compare(eventBoundary.border.width, 0)
        compare(label.text, "拖出片段")
        compare(duration.text, "00:00.50")
        verify(capsule.border.color.toString().indexOf("ff8a00") >= 0)
        verify(label.color.toString().indexOf("ff8a00") >= 0)
        verify(durationCapsule.border.color.toString().indexOf("ff8a00") >= 0)
        verify(duration.color.toString().indexOf("ff8a00") >= 0)
        verify(Math.abs(capsule.x - 6) <= 1)
        verify(Math.abs(capsule.y + capsule.height
                        - capsule.parent.height + 6) <= 1)
        verify(Math.abs(duration.x + duration.width
                        - duration.parent.width + 6) <= 1)
    }

    function test_selectionDurationUsesReferenceCentisecondTimecode() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(0, 48000))
        const duration = findChild(findChild(page, "editorWaveformCanvas"),
                                   "editorSelectionDuration")
        verify(duration)
        compare(duration.text, "00:01.00")
    }

    function test_gainAndEnvelopeControlsUseSampleAndGainMapping() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        const event = AudioEditorController.timelineEventViews[0]
        verify(AudioEditorController.addEnvelopePoint(event.id, 48000, 0.5))
        wait(0)
        const line = findChild(canvas, "editorEnvelopeLine")
        const point = findChild(canvas, "editorEnvelopePoint")
        const gain = findChild(canvas, "editorEventGainInteraction")
        const gainLine = findChild(canvas, "editorEventGainLine")
        verify(line && point && gain && gainLine)
        compare(line.implicitStartGain, 1)
        compare(canvas.envelopeGainAtOffset(
            AudioEditorController.timelineEventViews[0].envelope, 0), 1)
        verify(Math.abs(canvas.envelopeGainAtOffset(
            AudioEditorController.timelineEventViews[0].envelope, 24000)
            - 0.75) < 0.000001)
        compare(canvas.envelopeGainAtOffset(
            AudioEditorController.timelineEventViews[0].envelope, 48000), 0.5)
        const pointOnCanvas = point.mapToItem(canvas,
            point.width / 2, point.height / 2)
        const expectedPoint = gain.parent.mapToItem(canvas, 0,
            gain.parent.height * 0.75)
        verify(Math.abs(pointOnCanvas.x - canvas.pixelAtFrame(48000)) <= 2)
        verify(Math.abs(pointOnCanvas.y - expectedPoint.y) <= 2)

        verify(AudioEditorController.setEventGain(event.id, 1.5))
        wait(0)
        const updatedGain = findChild(canvas, "editorEventGainInteraction")
        const updatedGainLine = findChild(canvas, "editorEventGainLine")
        verify(updatedGain && updatedGainLine)
        verify(Math.abs(updatedGainLine.y
            - updatedGain.parent.height * 0.25) <= 1)
    }

    function test_fadeHandleAndVolumeLinePersistSampleExactEdits() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 480000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 480000))
        wait(0)
        const fade = findChild(canvas, "editorEventFadeOutHandle")
        const volumeLine = findChild(canvas, "editorEventVolumeLine")
        const body = findChild(canvas, "editorEventBodyInteraction")
        verify(fade && volumeLine && body)
        tryVerify(function() { return fade.visible && fade.width > 0 })

        const dragDelta = -Math.round(canvas.width * 0.2)
        const dragStartX = fade.width - 8
        const dragStartPoint = fade.mapToItem(canvas,
            dragStartX, fade.height / 2)
        const targetPoint = Qt.point(
            dragStartPoint.x + dragDelta, dragStartPoint.y)
        const eventEnd = Number(AudioEditorController.timelineEventViews[0].timelineEnd)
        const expectedFade = eventEnd - canvas.frameAtCanvasPixel(targetPoint.x)
        mouseDrag(canvas, dragStartPoint.x, dragStartPoint.y,
                  dragDelta, 0, Qt.LeftButton, Qt.NoModifier, 30)
        compare(Number(AudioEditorController.timelineEventViews[0].fadeOut),
                expectedFade)

        const clickX = Math.round(volumeLine.width * 0.45)
        const clickY = Math.round(volumeLine.height * 0.25)
        const envelopePoint = volumeLine.mapToItem(canvas, clickX, clickY)
        const expectedOffset = canvas.frameAtCanvasPixel(envelopePoint.x)
            - Number(AudioEditorController.timelineEventViews[0].timelineStart)
        canvas.addEnvelopePointForEvent(
            AudioEditorController.timelineEventViews[0].id,
            Number(AudioEditorController.timelineEventViews[0].timelineStart),
            Number(AudioEditorController.timelineEventViews[0].timelineEnd),
            envelopePoint.x, clickY, volumeLine.height)
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 1)
        compare(Number(AudioEditorController.timelineEventViews[0]
            .envelope[0].offset), expectedOffset)
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

    function test_playheadIncludesReferenceTimeCapsuleAndRulerCircle() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.seekFrame(24000))
        const canvas = findChild(page, "editorWaveformCanvas")
        const line = findChild(canvas, "editorPlayheadLine")
        const capsule = findChild(page, "editorPlayheadTimeCapsule")
        const circle = findChild(page, "editorPlayheadRulerCircle")
        verify(canvas && line && capsule && circle)
        verify(line.color.toString().indexOf("ffaf00") >= 0)
        compare(circle.width, 14)
        compare(circle.height, 14)
        verify(capsule.text.indexOf(":") >= 0)
        const circleCenter = circle.mapToItem(page,
            circle.width / 2, circle.height / 2)
        const lineCenter = line.mapToItem(page, line.width / 2, 0)
        verify(Math.abs(circleCenter.x - lineCenter.x) <= 1)
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
            { tag: "reference", w: 1672, h: 849, access: "editorInspectorScroller" },
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
            const summary = findChild(page, "fileSummaryBar")
            verify(access.y >= commandBar.y + commandBar.height,
                   "narrow inspector access overlaps the command toolbar")
            const playAccess = findChild(page, "editorNarrowPlaybackAccess")
            verify(playAccess && playAccess.visible)
            compare(playAccess.enabled, false)
            verify(playAccess.y >= commandBar.y + commandBar.height)
            verify(playAccess.y + playAccess.height <= page.height)
            verify(playAccess.x + playAccess.width + 8 <= access.x,
                   "narrow playback and inspector access overlap")
            const accessPosition = access.mapToItem(page, 0, 0)
            const playPosition = playAccess.mapToItem(page, 0, 0)
            const summaryPosition = summary.mapToItem(page, 0, 0)
            verify(accessPosition.y >= summaryPosition.y + summary.height
                   || summaryPosition.y >= accessPosition.y + access.height,
                   "narrow inspector access overlaps the file summary")
            verify(playPosition.y >= summaryPosition.y + summary.height
                   || summaryPosition.y >= playPosition.y + playAccess.height,
                   "narrow playback access overlaps the file summary")

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
        compare(findChild(page, "editorExportCodec").text, "MP3")
        compare(findChild(page, "editorExportSampleRate").text, "44.1 kHz")
        compare(findChild(page, "editorExportBitDepth").text, "24-bit")
        compare(findChild(page, "editorExportChannels").text, "立体声")
        compare(findChild(page, "editorExportBitRate").text, "320 kbps")
        verify(findChild(page, "editorExportDirectory").text !== "--")
        compare(findChild(page, "audioEditorExportDialog"), null)
        compare(findChild(page, "editorExportButton").enabled, false)
    }
}
