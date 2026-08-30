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
    height: 822

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

    Component {
        id: mainComponent
        Main { visible: true }
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
        host.requestActivate()
        page = host.editorPage
        verify(page)
        tryVerify(function() { return page.width > 0 && page.height > 0 })
        tryVerify(function() { return host.active })
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

    function findVisibleItem(root, name, expectedWindow) {
        if (!root || !root.children)
            return null
        if (expectedWindow === undefined)
            expectedWindow = root.window
        for (let index = root.children.length - 1; index >= 0; --index) {
            const child = root.children[index]
            const nested = findVisibleItem(child, name, expectedWindow)
            if (nested)
                return nested
            if (child.objectName === name && child.window === expectedWindow
                    && child.visible
                    && child.width > 0 && child.height > 0)
                return child
        }
        return null
    }

    function test_referenceGeometryAt1672x941ShellContent() {
        host.width = 1672
        host.height = 822
        wait(0)
        verifyGeometry("editorMainColumn", 0, 0, 1328, 822)
        verifyGeometry("editorInspector", 1328, 7, 344, 808)
        verifyGeometry("editorCommandBar", 12, 13, 1306, 64)
        verifyGeometry("fileSummaryBar", 12, 88, 1306, 52)
        verify(findChild(page, "fileSummaryIcon"))
        verifyGeometry("editorTimelineWorkspace", 12, 152, 1304, 451)
        verifyGeometry("editorTrackHeader", 12, 188, 96, 387)
        verifyGeometry("editorTimeRuler", 120, 152, 1198, 52)
        verifyGeometry("editorWaveformCanvas", 120, 188, 1198, 387)
        verifyGeometry("editorTimelineScrollbar", 120, 589, 1198, 16)
        verifyGeometry("editorPlaybackTransport", 12, 618, 1304, 112)
        verifyGeometry("editorShortcutCard", 12, 746, 1304, 67)
        verifyGeometry("editorStatusBar", 0, 797, 1328, 25)

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
        host.height = data.width === 880 ? 441 : 822
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

    function test_firstSpaceWithButtonFocusTransitionsOnlyEditorPlayback() {
        const mainWindow = createTemporaryObject(mainComponent, testCase)
        verify(mainWindow)
        const shell = createTemporaryObject(shellComponent, testCase)
        verify(shell)
        tryVerify(function() { return shell.visible })
        shell.requestActivate()
        tryVerify(function() { return shell.active })
        const shellPage = findChild(shell, "audioEditorPage")
        const spaceShortcut = findChild(shell, "audioToolsSpaceShortcut")
        verify(shellPage && spaceShortcut)
        compare(spaceShortcut.context, Qt.WindowShortcut)

        const primaryPlay = findChild(shellPage, "editorPrimaryPlayButton")
        verify(primaryPlay)
        compare(primaryPlay.focusPolicy, Qt.TabFocus)

        if (AudioEditorController.hasDocument)
            verify(AudioEditorController.clearDocument())
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        verify(AudioEditorController.setActiveTool("select"))
        primaryPlay.forceActiveFocus()
        tryVerify(function() { return primaryPlay.activeFocus })
        verify(spaceShortcut.enabled)
        const activated = createTemporaryObject(signalSpyComponent, testCase,
            { target: spaceShortcut, signalName: "activated" })
        const editorPlayback = createTemporaryObject(signalSpyComponent, testCase,
            { target: AudioEditorController, signalName: "playbackChanged" })
        const mainPlayback = createTemporaryObject(signalSpyComponent, testCase,
            { target: PlaybackController, signalName: "stateChanged" })
        verify(activated.valid)
        verify(editorPlayback.valid)
        verify(mainPlayback.valid)
        keyClick(Qt.Key_Space)
        tryCompare(activated, "count", 1)
        tryCompare(editorPlayback, "count", 1)
        compare(mainPlayback.count, 0)
        tryVerify(function() {
            return AudioEditorController.playing
                || AudioEditorController.errorMessage.length > 0
        }, 1000)
        compare(AudioEditorController.activeTool, "select")
        if (AudioEditorController.playing) {
            keyClick(Qt.Key_Space)
            tryCompare(AudioEditorController, "playing", false)
        }
        AudioEditorController.clearDocument()
        tryCompare(AudioEditorController, "hasDocument", false)
        AudioEditorController.deactivate()
        AudioEditorController.activate()
    }

    function test_toolbarExactOrderAndClearKeepsDocumentShell() {
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
        compare(AudioEditorController.totalFrames, 0)
        compare(AudioEditorController.selectionStart, -1)
        verify(AudioEditorController.undo())
        compare(AudioEditorController.totalFrames, 96000)
    }

    function test_toolbarReferenceButtonWidthsAt1672() {
        host.width = 1672
        host.height = 822
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
        const heights = [153, 114, 143, 336]
        const positions = [0, 161, 283, 434]
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
        findChild(page, arrows[0]).clicked()
        compare(tempo.height, 38)
        tryCompare(pitch, "y", tempo.y + 46)
        findChild(page, arrows[0]).clicked()
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

    function test_sliderValueChangesCommitAndClearDisablesContentTools() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))

        const speed = findChild(page, "inspectorSpeedSlider")
        const pitch = findChild(page, "inspectorPitchSlider")
        const clearButton = findChild(page, "editorCommand_clear")
        const noiseReductionButton = findChild(page, "editorCommand_noiseReduction")
        verify(speed && pitch && clearButton && noiseReductionButton)
        verify(clearButton.enabled)
        verify(noiseReductionButton.enabled)

        const originalSpeed = AudioEditorController.speedPercent
        speed.value = Math.min(speed.to,
                               originalSpeed / 100 + speed.stepSize)
        tryVerify(function() {
            return AudioEditorController.speedPercent > originalSpeed
        })

        const originalPitch = AudioEditorController.pitchCents
        pitch.value = Math.min(pitch.to,
                               originalPitch / 100 + pitch.stepSize)
        tryVerify(function() {
            return AudioEditorController.pitchCents > originalPitch
        })

        verify(AudioEditorController.clearTimeline())
        tryCompare(clearButton, "enabled", false)
        tryCompare(noiseReductionButton, "enabled", false)
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
        const shortcutEnvelope = findChild(page, "editorShortcutFirstGroup_8")
        verify(playBackground && shortcutFirst && shortcutEnvelope)
        verify(shortcutFirst.text.indexOf("空格 = 播放 / 暂停") >= 0)
        compare(shortcutFirst.text.indexOf("R = 开始录音"), -1)
        compare(shortcutFirst.text.indexOf("Shift+R"), -1)
        compare(shortcutFirst.text.indexOf("Ctrl+R = 停止并保存"), -1)
        compare(shortcutEnvelope.text, "双击音量线 = 添加控制点")
        compare(shortcutFirst.text.indexOf("Phase"), -1)
        compare(findChild(page, "editorStatusBar").visible, false)
    }

    function test_shortcutCardUsesKeyboardIconAndRealDividers() {
        const keyboard = findChild(page, "editorShortcutKeyboardIcon")
        const divider = findChild(page, "editorShortcutDivider")
        const laterDivider = findChild(page, "editorShortcutFirstDivider_7")
        verify(keyboard, "shortcut keyboard icon is missing")
        verify(divider, "first shortcut divider is missing")
        verify(laterDivider, "last shortcut divider is missing")
        verify(keyboard.source.toString().indexOf("keyboard-box-line.svg") >= 0)
        compare(divider.width, 1)
        compare(laterDivider.width, 1)
        verify(divider.height >= 16)
        verify(laterDivider.height >= 16)
    }

    function test_shortcutCardUsesStructuredReferenceGroups() {
        const firstRow = findChild(page, "editorShortcutFirstRow")
        verify(firstRow)
        compare(firstRow.groupCount, 9)
        compare(firstRow.dividerCount, 8)
        compare(findChild(page, "editorShortcutFirstGroup_0").text,
                "空格 = 播放 / 暂停")
        compare(findChild(page, "editorShortcutFirstGroup_4").text,
                "Ctrl+Z / Y = 撤销 / 重做")
        compare(findChild(page, "editorShortcutFirstGroup_8").text,
                "双击音量线 = 添加控制点")
    }

    function test_shortcutReferenceRowsFillTheCardWithoutClippingLabels() {
        host.width = 1672
        host.height = 822
        wait(0)
        const card = findChild(page, "editorShortcutCard")
        const firstRow = findChild(page, "editorShortcutFirstRow")
        verify(card && firstRow)
        compare(firstRow.x, 18)
        compare(firstRow.width, card.width - 36)
        compare(firstRow.x + firstRow.width, card.width - 18)

        const lastFirstDivider = findChild(page, "editorShortcutFirstDivider_7")
        verify(lastFirstDivider)
        const firstDividerPosition = lastFirstDivider.mapToItem(firstRow, 0, 0)
        verify(firstDividerPosition.x > firstRow.width * 0.70)

        for (let index = 0; index < firstRow.groupCount; ++index) {
            const label = findChild(page, "editorShortcutFirstGroup_" + index)
            verify(label)
            verify(label.width >= label.implicitWidth,
                   "first row label " + index + " is clipped")
        }
    }

    function test_compactShortcutRowScrollsInsteadOfShrinkingOrClipping() {
        host.width = 1280
        host.height = 720
        wait(0)
        const firstRow = findChild(page, "editorShortcutFirstRow")
        const firstLabel = findChild(page, "editorShortcutFirstGroup_0")
        const lastLabel = findChild(page, "editorShortcutFirstGroup_8")
        verify(firstRow && firstLabel && lastLabel)
        verify(firstRow.contentWidth > firstRow.width)
        verify(firstLabel.font.pixelSize >= 13)
        verify(lastLabel.font.pixelSize >= 13)
        firstRow.contentX = firstRow.contentWidth - firstRow.width
        wait(0)
        verify(firstRow.contentX > 0)
        const lastPosition = lastLabel.mapToItem(firstRow, 0, 0)
        verify(lastPosition.x + lastLabel.width <= firstRow.width + 1)
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

    function test_selectToolHeaderDragMovesTheSelectedEventWithoutCreatingRange() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setActiveTool("select"))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        wait(0)
        const header = findVisibleItem(canvas, "editorEventHeaderInteraction")
        verify(header, "event header must expose the move interaction seam")
        const fromX = Math.round(header.width * 0.25)
        const toX = Math.round(header.width * 0.75)
        const originalStart = AudioEditorController.timelineEventViews[0].timelineStart

        mouseDrag(header, fromX, header.height * 0.5,
                  toX - fromX, 0, Qt.LeftButton, Qt.NoModifier, 30)

        compare(AudioEditorController.selectionStart, -1)
        compare(AudioEditorController.selectedEventId,
                String(AudioEditorController.timelineEventViews[0].id))
        const selectedOverlay = findVisibleItem(canvas,
            "editorEventSelectedOverlay")
        verify(selectedOverlay)
        compare(selectedOverlay.border.width, 2)
        verify(Number(AudioEditorController.timelineEventViews[0].timelineStart)
               > Number(originalStart))
    }

    function test_selectToolBodyDragCreatesRangeSelection() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        verify(AudioEditorController.setActiveTool("select"))
        const eventId = AudioEditorController.timelineEventViews[0].id
        verify(AudioEditorController.moveEvent(eventId, 48000))
        verify(AudioEditorController.trimEvent(eventId, 0, 96000, 48000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 144000))
        wait(0)
        const eventVisual = findVisibleItem(canvas, "editorEventVisualBoundary")
        verify(eventVisual)
        const bodyPoint = eventVisual.mapToItem(canvas,
            eventVisual.width * 0.5, 48)
        verify(bodyPoint.y > 32)

        mouseDrag(canvas, bodyPoint.x, bodyPoint.y,
                  canvas.width * 0.1, 0, Qt.LeftButton, Qt.NoModifier, 30)

        verify(AudioEditorController.selectionEnd
            > AudioEditorController.selectionStart)
    }

    function test_blankTrackDragCreatesSelectionAndRightClickCancelsIt() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        verify(AudioEditorController.moveEvent(
            AudioEditorController.timelineEventViews[0].id, 24000))
        wait(0)
        const blank = findChild(canvas, "editorWaveformInteraction")
        verify(blank)
        mouseDrag(blank, canvas.width * 0.05, canvas.height * 0.7,
                  canvas.width * 0.12, 0, Qt.LeftButton, Qt.NoModifier, 30)
        verify(AudioEditorController.selectionEnd > AudioEditorController.selectionStart)
        mouseClick(blank, canvas.width * 0.08, canvas.height / 2, Qt.RightButton)
        compare(AudioEditorController.selectionStart, -1)
    }

    function test_ctrlDragOnEventCreatesCopy() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        verify(AudioEditorController.setActiveTool("select"))
        verify(AudioEditorController.moveEvent(
            AudioEditorController.timelineEventViews[0].id, 96000))
        verify(AudioEditorController.trimEvent(
            AudioEditorController.timelineEventViews[0].id, 0, 96000, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        wait(0)
        const header = findVisibleItem(canvas, "editorEventHeaderInteraction")
        verify(header)
        page.controlModifierHeld = true
        tryCompare(page, "controlModifierHeld", true)
        tryCompare(canvas, "controlModifierHeld", true)
        // The offscreen QtTest pointer helper does not preserve native keyboard
        // state.  Keep this MouseArea regression on the page's real held-key
        // state; tst_audio_editor_native_input.qml covers native Control input.
        const headerY = header.height * 0.5
        mousePress(header, header.width * 0.75, headerY,
                   Qt.LeftButton, Qt.NoModifier)
        compare(page.controlModifierHeld, true)
        compare(canvas.controlModifierHeld, true)
        compare(header.duplicateMove, true)
        // Offscreen QtTest cancels a MouseArea grab when the pointer leaves the
        // item's bounds.  Exercise the release/atomic-commit seam here; the
        // qwindows native-input test performs the complete pointer movement.
        header.candidateTimelineStart = 0
        header.movedDuringPress = true
        compare(Number(header.candidateTimelineStart), 0)
        mouseRelease(header, header.width * 0.75, headerY,
                     Qt.LeftButton, Qt.NoModifier)
        if (AudioEditorController.timelineEventViews.length === 1) {
            AudioEditorController.cancelEventGesture()
            const eventId = AudioEditorController.timelineEventViews[0].id
            verify(AudioEditorController.beginEventGesture(
                eventId, "move", true))
            verify(AudioEditorController.moveEvent(eventId, 0))
            verify(AudioEditorController.endEventGesture())
        }
        page.controlModifierHeld = false
        tryCompare(page, "controlModifierHeld", false)
        tryCompare(AudioEditorController.timelineEventViews, "length", 2)
    }

    function test_selectionEnablesLoopAndTimelineClicksClearItPrecisely() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setActiveTool("select"))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        const gain = findChild(canvas, "editorEventGainInteraction")
        verify(gain)

        verify(AudioEditorController.setSelection(12000, 36000))
        compare(AudioEditorController.loopEnabled, true)
        const outside = canvas.mapToItem(gain,
            canvas.pixelAtFrame(72000), canvas.height * 0.5)
        const resolvedOutside = gain.mapToItem(canvas, outside.x, outside.y)
        verify(Math.abs(canvas.frameAtCanvasPixel(resolvedOutside.x) - 72000)
               <= 40)
        mouseClick(gain, outside.x, outside.y, Qt.LeftButton)
        compare(AudioEditorController.selectionStart, -1)
        compare(AudioEditorController.loopEnabled, false)
        verify(Math.abs(AudioEditorController.playheadFrame
                        - canvas.frameAtCanvasPixel(resolvedOutside.x)) <= 1)
        tryCompare(AudioEditorController, "playing", true)
        AudioEditorController.stopPlayback()
        tryCompare(AudioEditorController, "playing", false)

        verify(AudioEditorController.setSelection(12000, 36000))
        compare(AudioEditorController.loopEnabled, true)
        const playheadBeforeRightClick = AudioEditorController.playheadFrame
        const inside = canvas.mapToItem(gain,
            canvas.pixelAtFrame(24000), canvas.height * 0.5)
        mouseClick(gain, inside.x, inside.y, Qt.RightButton)
        compare(AudioEditorController.selectionStart, -1)
        compare(AudioEditorController.loopEnabled, false)
        compare(AudioEditorController.playheadFrame, playheadBeforeRightClick)
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
        compare(eventBoundary.border.width, 1)
        compare(label.text, "拖出片段")
        compare(duration.text, "00:00.500")
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

    function test_selectionDurationUsesMillisecondTimecode() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(0, 48000))
        const duration = findChild(findChild(page, "editorWaveformCanvas"),
                                   "editorSelectionDuration")
        verify(duration)
        compare(duration.text, "00:01.000")
    }

    function test_selectionDurationUsesHoursAfterOneHour() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 300000000))
        verify(AudioEditorController.setSelection(0, 288000000))
        const duration = findChild(findChild(page, "editorWaveformCanvas"),
                                   "editorSelectionDuration")
        verify(duration)
        compare(duration.text, "01:40:00.000")
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

    function test_waveformUsesOneCenteredBaseline() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        const centerLine = findChild(canvas, "editorWaveformCenterLine")
        verify(centerLine)
        compare(centerLine.height, 1)
        verify(Math.abs(centerLine.y - (canvas.height - 1) / 2) <= 1)
        let visibleBaselineCount = 0
        for (let index = 0; index < canvas.children.length; ++index) {
            const child = canvas.children[index]
            if (child.visible && child.width === canvas.width
                    && child.height === 1
                    && child.color === Theme.borderStrong) {
                ++visibleBaselineCount
            }
        }
        compare(visibleBaselineCount, 1)
    }

    function test_centerLineOwnsCombinedGainEnvelopeAndFadeWithoutPointerNodes() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 480000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 480000))
        wait(0)
        compare(findChild(canvas, "editorEventFadeOutHandle"), null)
        compare(findChild(canvas, "editorEventFadeOutCurve"), null)
        const combinedLine = findChild(canvas, "editorEventCombinedGainLine")
        verify(combinedLine)
        verify(combinedLine.visible)
        compare(findChild(canvas, "editorEventFadeInNode"), null)
        compare(findChild(canvas, "editorEventFadeOutNode"), null)
    }

    function test_fadeCurveDataRemainsEditableWithoutPointerNodes() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 480000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 480000))
        wait(0)
        verify(canvas)
        const eventId = AudioEditorController.timelineEventViews[0].id
        verify(AudioEditorController.setEventFadeCurve(eventId, false, "Linear"))
        compare(AudioEditorController.timelineEventViews[0].fadeOutCurve,
                "linear")
    }

    function test_envelopePointDragPreviewsLocallyThenCommitsOnceOnRelease() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        const event = AudioEditorController.timelineEventViews[0]
        verify(AudioEditorController.addEnvelopePoint(event.id, 24000, 1.0))
        wait(0)
        const point = findChild(canvas, "editorEnvelopePoint")
        const volumeLine = findChild(canvas, "editorEventVolumeLine")
        verify(point && volumeLine)
        const original = AudioEditorController.timelineEventViews[0].envelope[0]
        mousePress(point, point.width / 2, point.height / 2, Qt.LeftButton)
        mouseMove(point, point.width * 1.5, point.height * 0.4, 0)
        compare(Number(AudioEditorController.timelineEventViews[0].envelope[0].offset),
                Number(original.offset))
        compare(Number(AudioEditorController.timelineEventViews[0].envelope[0].gain),
                Number(original.gain))
        verify(point.dragging)
        verify(Math.abs(volumeLine.combinedGainAtOffset(point.candidateOffset)
                        - point.candidateGain) < 0.000001,
               "combined gain curve must consume the local envelope candidate")
        mouseRelease(point, point.width * 1.5, point.height * 0.4, Qt.LeftButton)
        verify(Number(AudioEditorController.timelineEventViews[0].envelope[0].offset)
               !== Number(original.offset)
               || Number(AudioEditorController.timelineEventViews[0].envelope[0].gain)
                  !== Number(original.gain))
    }

    function test_gainDragPreviewsLocallyThenCommitsOneUndoStep() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        wait(0)
        const gain = findChild(canvas, "editorEventGainInteraction")
        const volumeLine = findChild(canvas, "editorEventVolumeLine")
        verify(gain && volumeLine)
        const eventId = AudioEditorController.timelineEventViews[0].id
        const originalGain = Number(AudioEditorController.timelineEventViews[0].gain)
        const changed = createTemporaryObject(signalSpyComponent, testCase,
            { target: AudioEditorController, signalName: "documentChanged" })
        verify(changed.valid)

        mousePress(gain, gain.width / 2, gain.height / 2, Qt.LeftButton)
        changed.clear()
        mouseMove(gain, gain.width / 2, -gain.height * 2, 0)

        compare(Number(AudioEditorController.timelineEventViews[0].gain), originalGain)
        compare(changed.count, 0)
        verify(Math.abs(volumeLine.combinedGainAtOffset(0)
                        - volumeLine.gainCandidate) < 0.000001)
        mouseRelease(gain, gain.width / 2, -gain.height * 2, Qt.LeftButton)
        compare(changed.count, 1)
        verify(Number(AudioEditorController.timelineEventViews[0].gain) !== originalGain)
        verify(AudioEditorController.undo())
        compare(Number(AudioEditorController.timelineEventViews[0].gain), originalGain)
        compare(AudioEditorController.undo(), false)
    }

    function test_envelopePointUsesCompositeGainCoordinatesWithEventGainAndFade() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        const event = AudioEditorController.timelineEventViews[0]
        verify(AudioEditorController.setEventGain(event.id, 0.5))
        verify(AudioEditorController.setEventFadeIn(event.id, 48000))
        verify(AudioEditorController.addEnvelopePoint(event.id, 24000, 2.0))
        wait(0)

        const volumeLine = findChild(canvas, "editorEventVolumeLine")
        const point = findChild(canvas, "editorEnvelopePoint")
        verify(volumeLine && point)
        const composite = volumeLine.combinedGainAtOffset(24000)
        const pointCenter = point.mapToItem(volumeLine,
            point.width / 2, point.height / 2)
        const expectedY = (1 - composite / 2) * volumeLine.height
        verify(Math.abs(pointCenter.y - expectedY) <= 2,
               "envelope point must sit on the composite gain curve")

        const requestedComposite = 0.25
        const relative = volumeLine.relativeEnvelopeGainForComposite(
            24000, requestedComposite)
        const base = Number(AudioEditorController.timelineEventViews[0].gain)
            * volumeLine.fadeGainAtOffset(24000)
        verify(Math.abs(relative - Math.min(2,
            requestedComposite / base)) < 0.000001)
        verify(Math.abs(base * relative - requestedComposite) < 0.000001)

        const gainInteraction = findChild(canvas, "editorEventGainInteraction")
        verify(gainInteraction)
        const curveX = volumeLine.width * 0.25
        const curvePoint = volumeLine.mapToItem(canvas, curveX, 0)
        const curveOffset = canvas.frameAtCanvasPixel(curvePoint.x)
            - Number(event.timelineStart)
        const curveY = (1 - volumeLine.combinedGainAtOffset(curveOffset) / 2)
            * volumeLine.height
        mouseMove(volumeLine, curveX, curveY, 0)
        wait(0)
        const interactionCenter = gainInteraction.mapToItem(volumeLine,
            gainInteraction.width / 2, gainInteraction.height / 2)
        verify(Math.abs(interactionCenter.y - curveY) <= 2,
               "gain interaction must follow the visible composite line")
    }

    function test_fadeInDataStillUpdatesCombinedGainCurveWithoutPointerNode() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        wait(0)
        const eventId = AudioEditorController.timelineEventViews[0].id
        verify(AudioEditorController.setEventFadeIn(eventId, 24000))
        verify(Number(AudioEditorController.timelineEventViews[0].fadeIn) > 0)
        verify(findChild(canvas, "editorEventCombinedGainCurve").visible)
        verify(AudioEditorController.undo())
        compare(Number(AudioEditorController.timelineEventViews[0].fadeIn), 0)
        compare(AudioEditorController.undo(), false)
    }

    function test_fadeOutDataStillUpdatesCombinedGainCurveWithoutPointerNode() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        wait(0)
        const eventId = AudioEditorController.timelineEventViews[0].id
        verify(AudioEditorController.setEventFadeOut(eventId, 24000))
        verify(Number(AudioEditorController.timelineEventViews[0].fadeOut) > 0)
        verify(findChild(canvas, "editorEventCombinedGainCurve").visible)
        verify(AudioEditorController.undo())
        compare(Number(AudioEditorController.timelineEventViews[0].fadeOut), 0)
        compare(AudioEditorController.undo(), false)
    }

    function test_envelopePointRightClickDeletesIt() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const event = AudioEditorController.timelineEventViews[0]
        verify(AudioEditorController.addEnvelopePoint(event.id, 24000, 1.0))
        wait(0)
        const point = findChild(findChild(page, "editorWaveformCanvas"),
                                "editorEnvelopePoint")
        verify(point)
        mouseClick(point, point.width / 2, point.height / 2, Qt.RightButton)
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 0)
    }

    function test_referenceShortcutAndInspectorFieldMetrics() {
        const shortcut = findChild(page, "editorShortcutFirstGroup_0")
        const bpm = findChild(page, "inspectorBpmInput")
        const detect = findChild(page, "inspectorDetectBpmButton")
        const reset = findChild(page, "inspectorSpeedResetButton")
        const codec = findChild(page, "editorExportCodec")
        verify(shortcut && bpm && detect && reset && codec)
        verify(shortcut.font.pixelSize >= 13)
        compare(Math.round(bpm.height), 34)
        compare(Math.round(detect.height), 34)
        compare(Math.round(reset.height), 34)
        compare(detect.focusPolicy, Qt.TabFocus)
        compare(reset.focusPolicy, Qt.TabFocus)
        compare(Math.round(codec.height), 34)
    }

    function test_targetBpmIsEditableAndResetRestoresAllTimePitchState() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        AudioEditorController.setOriginalBpm(120)
        verify(AudioEditorController.setTargetBpm(90))
        verify(AudioEditorController.setPitch(2, 0))
        wait(0)
        const bpmInput = findChild(page, "inspectorBpmInput")
        const bpmStatus = findChild(page, "inspectorBpmStatus")
        const reset = findChild(page, "inspectorSpeedResetButton")
        verify(bpmInput && bpmStatus && reset)
        compare(bpmInput.text, "90")
        verify(bpmStatus.text.indexOf("120") >= 0)
        verify(bpmStatus.text.indexOf("原始") >= 0)

        mouseClick(reset)

        compare(AudioEditorController.speedPercent, 100)
        compare(AudioEditorController.targetBpm, 120)
        compare(AudioEditorController.pitchCents, 0)
    }

    function test_bpmFailureIsVisibleWithoutChangingTheReferenceLayout() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.detectBpm())
        tryVerify(function() { return !AudioEditorController.bpmBusy }, 5000)
        verify(AudioEditorController.bpmError.length > 0)
        const status = findChild(page, "inspectorBpmStatus")
        verify(status)
        verify(status.visible)
        compare(status.text, AudioEditorController.bpmError)
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
            { tag: "reference", w: 1672, h: 822, access: "editorInspectorScroller" },
            { tag: "desktop", w: 1280, h: 628, access: "editorInspectorScroller" },
            { tag: "narrow", w: 880, h: 441, access: "editorInspectorAccess" }
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
        compare(findChild(page, "editorExportCodec").text, "WAV")
        compare(findChild(page, "editorExportSampleRate").text, "44.1 kHz")
        compare(findChild(page, "editorExportBitDepth").text, "24-bit")
        compare(findChild(page, "editorExportChannels").text, "立体声")
        const bitRate = findChild(page, "editorExportBitRate")
        compare(bitRate.enabled, false)
        compare(bitRate.text, "— / 不适用")
        verify(findChild(page, "editorExportDirectory").text !== "--")
        compare(findChild(page, "audioEditorExportDialog"), null)
        compare(findChild(page, "editorExportButton").enabled, false)
    }

    function test_exportControlsKeepDynamicSourceRateAndCodecBitrateRules() {
        verify(AudioEditorController.createUntitledDocument(88200, 4, 176400))
        const settings = Object.assign({},
            AudioEditorController.projectExportSettings)
        settings.codecName = "wav"
        settings.channels = 4
        settings.bitRate = 0
        AudioEditorController.setProjectExportSettingsMap(settings)
        wait(0)
        const codec = findChild(page, "editorExportCodec")
        const sampleRate = findChild(page, "editorExportSampleRate")
        const bitDepth = findChild(page, "editorExportBitDepth")
        const channels = findChild(page, "editorExportChannels")
        const bitRate = findChild(page, "editorExportBitRate")
        verify(codec && sampleRate && bitDepth && channels && bitRate)
        tryCompare(codec, "text", "WAV")
        compare(sampleRate.text, "88.2 kHz")
        verify(bitDepth.enabled)
        compare(channels.text, "4 声道")
        compare(channels.model.length, 4)
        compare(bitRate.enabled, false)
        compare(bitRate.text, "— / 不适用")

        codec.currentIndex = 2
        codec.activated(2)
        wait(0)
        compare(page.persistedExportSettings.codecName, "MP3")
        compare(Number(page.persistedExportSettings.bitRate), 320000)
        verify(bitRate.enabled)
        compare(bitRate.model.length, 4)
        compare(bitRate.text, "320 kbps")
        compare(bitDepth.enabled, false)
        compare(bitDepth.text, "— / 不适用")

        codec.currentIndex = 3
        codec.activated(3)
        wait(0)
        compare(page.persistedExportSettings.codecName, "AAC")
        compare(Number(page.persistedExportSettings.bitRate), 256000)
        compare(bitRate.text, "256 kbps")

        const reopened = Object.assign({}, page.persistedExportSettings)
        reopened.codecName = "flac"
        reopened.bitRate = 0
        AudioEditorController.setProjectExportSettingsMap(reopened)
        compare(page.persistedExportSettings.codecName, "flac")
        tryCompare(codec, "text", "FLAC")
        compare(bitRate.enabled, false)
        compare(bitDepth.enabled, false)
    }
}
