import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer
import AgPlayer.Test

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

    function cleanup() {
        if (AudioEditorController.recording)
            AudioEditorController.cancelRecording()
        host = null
        page = null
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

    function test_referenceGeometryAt1672x942ShellContent() {
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
        verify(findChild(shell, "editorSpaceShortcut"))
        compare(findChild(shell, "audioToolsSpaceShortcut"), null)
        const shortcutText = findChild(shellPage, "editorShortcutText").text
        verify(shortcutText.indexOf("空格 = 播放 / 暂停") >= 0)
        compare(shortcutText.indexOf("Phase"), -1)
    }

    function test_composedToolsShellUsesReadableLightThemeColors() {
        const previousMode = SettingsController.themeMode
        SettingsController.themeMode = 1
        const shell = createTemporaryObject(shellComponent, testCase)
        verify(shell)
        tryVerify(function() { return shell.visible })
        const titleBar = findChild(shell, "audioToolsTitleBar")
        const titleText = findChild(shell, "audioToolsWindowTitle")
        const contentStack = findChild(shell, "audioToolsContentStack")
        const minimize = findChild(shell, "audioToolsMinimizeButton")
        const maximize = findChild(shell, "audioToolsMaximizeButton")
        const close = findChild(shell, "audioToolsCloseButton")
        const topNav = findChild(shell, "audioToolsTopNav")
        verify(titleBar && titleText && contentStack
               && minimize && maximize && close,
               "the themed tools shell objects must exist")
        verify(topNav, "the top navigation surface must exist")
        compare(titleBar.color.toString(), Theme.panel.toString())
        compare(titleText.color.toString(), Theme.primaryText.toString())
        compare(contentStack.color.toString(), Theme.background.toString())
        compare(minimize.icon.color.toString(), Theme.iconPrimary.toString())
        compare(maximize.icon.color.toString(), Theme.iconPrimary.toString())
        compare(close.icon.color.toString(), Theme.iconPrimary.toString())
        verify(titleText.color.toString() !== titleBar.color.toString())
        verify(minimize.icon.color.toString() !== titleBar.color.toString())
        verify(close.icon.color.toString() !== titleBar.color.toString())
        compare(topNav.activeLabelColor.toString(),
                Theme.primaryText.toString())
        verify(topNav.activeLabelColor.toString()
               !== titleBar.color.toString())
        SettingsController.themeMode = previousMode
        shell.destroy()
        wait(0)
    }

    function test_editor_media_colors_stay_exact_and_ignore_theme_seeds() {
        const previousMode = SettingsController.themeMode
        const previousAccentMode = SettingsController.accentMode
        const previousAccentPreset = SettingsController.accentPreset
        const previousFollow = SettingsController.highlightFollowAccent
        const previousHighlightMode = SettingsController.highlightMode
        const previousHighlightPreset = SettingsController.highlightPreset

        SettingsController.themeMode = 1
        SettingsController.accentMode = 1
        SettingsController.accentPreset = "systemBlue"
        SettingsController.highlightFollowAccent = false
        SettingsController.highlightMode = 1
        SettingsController.highlightPreset = "purple"
        wait(0)
        compare(Theme.editorWaveform.toString(), "#169b97")
        compare(Theme.editorOverviewWaveform.toString(), "#2b9692")
        compare(Theme.editorSelection.toString(), "#26169b97")
        compare(Theme.editorOverviewSelection.toString(), "#122b9692")
        compare(Theme.listWaveformMono.toString(), "#6b5a70")

        SettingsController.accentPreset = "red"
        SettingsController.highlightPreset = "green"
        wait(0)
        compare(Theme.editorWaveform.toString(), "#169b97")
        compare(Theme.editorSelection.toString(), "#26169b97")

        SettingsController.themeMode = 0
        wait(0)
        compare(Theme.editorWaveform.toString(), "#39c7c0")
        compare(Theme.editorOverviewWaveform.toString(), "#297e7b")
        compare(Theme.editorSelection.toString(), "#2639c7c0")
        compare(Theme.editorOverviewSelection.toString(), "#12297e7b")
        compare(Theme.listWaveformMono.toString(), "#c7b8cb")

        SettingsController.themeMode = previousMode
        SettingsController.accentMode = previousAccentMode
        SettingsController.accentPreset = previousAccentPreset
        SettingsController.highlightFollowAccent = previousFollow
        SettingsController.highlightMode = previousHighlightMode
        SettingsController.highlightPreset = previousHighlightPreset
        wait(0)
    }

    function test_editorWaveformUsesPlayerAppearanceSettings() {
        const originalMode = SettingsController.waveformMode
        const originalColor = SettingsController.waveformSolidBaseColor
        const originalDensity = SettingsController.waveformDensity
        const originalThickness = SettingsController.waveformThickness
        SettingsController.waveformMode = 0
        SettingsController.waveformSolidBaseColor = "#123456"
        SettingsController.waveformDensity = 3.5
        SettingsController.waveformThickness = 2.5
        const waveform = findChild(page, "editorWaveformGeometry")
        verify(waveform)
        tryCompare(waveform, "waveformColor", "#123456")
        tryCompare(waveform, "density", 3.5)
        tryCompare(waveform, "lineWidth", 2.5)
        compare(waveform.sampleMode,
                AudioEditorController.viewport.visibleFrameCount
                    <= Math.max(2, Math.floor(waveform.width) * 2))
        SettingsController.waveformMode = originalMode
        SettingsController.waveformSolidBaseColor = originalColor
        SettingsController.waveformDensity = originalDensity
        SettingsController.waveformThickness = originalThickness
    }

    function test_spaceShortcutYieldsToTextInputAndModalDialog() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const shortcut = findChild(page, "editorSpaceShortcut")
        const bpmInput = findChild(page, "inspectorBpmInput")
        verify(shortcut && bpmInput)
        verify(shortcut.enabled)

        bpmInput.forceActiveFocus()
        tryVerify(function() { return bpmInput.activeFocus })
        compare(shortcut.enabled, false)
        page.forceActiveFocus()
        tryVerify(function() { return !bpmInput.activeFocus })

        verify(AudioEditorController.setSelection(100, 200))
        verify(!AudioEditorController.clearDocument())
        const discard = findChild(page, "editorDiscardDialog")
        verify(discard)
        tryVerify(function() { return discard.visible })
        compare(shortcut.enabled, false)
        discard.reject()
    }

    function test_toolbarExactOrderAndClearRemovesDocument() {
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
            verify(button.shortcutText !== undefined
                && button.shortcutText.length > 0,
                "missing shortcut hint for " + names[index])
            verify(button.x > previousX, names[index] + " is out of order")
            previousX = button.x
        }

        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(100, 200))
        verify(AudioEditorController.setActiveTool("scissors"))
        const clearButton = findChild(page, "editorCommand_clear")
        mouseClick(clearButton)
        compare(AudioEditorController.hasDocument, true)
        verify(AudioEditorController.confirmDiscardAndOpen(),
               "clear must route through the existing unsaved-change confirmation")
        compare(AudioEditorController.hasDocument, false)
        compare(AudioEditorController.selectionStart, -1)
        compare(AudioEditorController.activeTool, "select")
    }

    function test_inspectorAThroughEMatchesReferenceControls() {
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
        for (const name of [
                 "inspectorRecordingDevice", "inspectorInputMeter",
                 "inspectorMonitorSwitch", "inspectorRecordingFormat",
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
        const names = ["inspectorRecordingGroup", "inspectorTempoGroup",
                       "inspectorPitchGroup", "inspectorPreservePitchGroup",
                       "inspectorExportGroup"]
        const arrows = ["inspectorRecordingCollapse", "inspectorTempoCollapse",
                        "inspectorPitchCollapse", "inspectorPreservePitchCollapse",
                        "inspectorExportCollapse"]
        const heights = [186, 129, 107, 104, 276]
        const positions = [0, 192, 327, 440, 550]
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

        const tempo = findChild(page, names[1])
        const pitch = findChild(page, names[2])
        mouseClick(findChild(page, arrows[1]))
        compare(tempo.height, 38)
        tryCompare(pitch, "y", tempo.y + 44)
        mouseClick(findChild(page, arrows[1]))
        compare(tempo.height, heights[1])
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
        const microphone = findChild(page, "recordingMicrophoneButton")
        const recording = findChild(page, "recordingToggleButton")
        const play = findChild(page, "editorPrimaryPlayButton")
        const narrowPlay = findChild(page, "editorNarrowPlaybackAccess")
        for (const control of [microphone, recording, play, narrowPlay]) {
            verify(control)
            verify(control.opacity >= 0.9,
                   control.objectName + " lost the reference appearance")
            verify(control.Accessible.name.length > 0)
            compare(control.Accessible.role, Accessible.Button)
        }
    }

    function test_referenceTransportShortcutAndStatusCopy() {
        const recordingState = findChild(page, "recordingStateText")
        const recordingTime = findChild(page, "recordingTimeText")
        const playBackground = findChild(page, "editorPrimaryPlayBackground")
        const shortcutFirst = findChild(page, "editorShortcutText")
        const shortcutSecond = findChild(page, "editorShortcutTextSecondRow")
        verify(recordingState && recordingTime && playBackground
               && shortcutFirst && shortcutSecond)
        compare(recordingState.text, "准备录音")
        compare(recordingTime.text, "00:00:00")
        verify(shortcutFirst.text.indexOf("空格 = 播放 / 暂停") >= 0)
        verify(shortcutSecond.text.indexOf("拖拽右上角 = 调整淡出") >= 0)
        verify(shortcutSecond.text.indexOf("双击音量线 = 添加控制点") >= 0)
        compare(shortcutFirst.text.indexOf("Phase"), -1)
        compare(shortcutSecond.text.indexOf("Phase"), -1)
        compare(findChild(page, "editorStatusBar").visible, false)
    }

    function test_recordingButtonsMatchReferenceRolesFromIdle() {
        const microphone = findChild(page, "recordingMicrophoneButton")
        const record = findChild(page, "recordingToggleButton")
        const device = findChild(page, "inspectorRecordingDevice")
        verify(microphone && record && device)
        compare(microphone.Accessible.name, "选择录音设备")
        compare(record.Accessible.name, "开始录音")
        compare(record.enabled,
                AudioEditorController.recordingSupported
                && !AudioEditorController.busy)
        mouseClick(microphone)
        verify(device.activeFocus,
               "microphone button must prepare the recording device selector")
    }

    function test_fourthPlaybackButtonJumpsToDocumentEnd() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 480000))
        verify(AudioEditorController.seekMs(100))
        const toEnd = findChild(page, "editorPlaybackForwardButton")
        verify(toEnd)
        compare(toEnd.Accessible.name, "跳到末尾")
        mouseClick(toEnd)
        compare(AudioEditorController.positionMs,
                AudioEditorController.durationMs)
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
        const selectionY = Math.round(body.height * 0.75)
        const fromPoint = body.mapToItem(canvas, fromX, selectionY)
        const toPoint = body.mapToItem(canvas, toX, selectionY)
        const expectedStart = canvas.frameAtCanvasPixel(fromPoint.x)
        const expectedEnd = canvas.frameAtCanvasPixel(toPoint.x)
        const originalStart = AudioEditorController.timelineEventViews[0].timelineStart

        mouseDrag(body, fromX, selectionY,
                  toX - fromX, 0, Qt.LeftButton, Qt.NoModifier, 30)

        compare(AudioEditorController.selectionStart, expectedStart)
        compare(AudioEditorController.selectionEnd, expectedEnd)
        compare(AudioEditorController.timelineEventViews[0].timelineStart,
                originalStart)
    }

    function test_transportTooltipsExposeKeyboardShortcutsAndOperationFeedback() {
        const names = [
            "editorPlaybackToStartButton",
            "editorPlaybackRewindButton",
            "editorPrimaryPlayButton",
            "editorPlaybackForwardButton",
            "editorPlaybackStopButton",
            "recordingMicrophoneButton",
            "recordingToggleButton",
            "recordingStopButton",
            "editorNarrowPlaybackAccess"
        ]
        for (const name of names) {
            const button = findChild(page, name)
            verify(button, "missing transport control " + name)
            verify(button.shortcutText !== undefined
                && button.shortcutText.length > 0,
                "missing shortcut tooltip contract for " + name)
        }
        const shortcuts = [
            "editorPlaybackToStartShortcut",
            "editorPlaybackRewindShortcut",
            "editorSpaceShortcut",
            "editorPlaybackToEndShortcut",
            "editorPlaybackStopShortcut",
            "editorRecordingDeviceShortcut",
            "editorRecordingToggleShortcut",
            "editorRecordingStopShortcut"
        ]
        for (const name of shortcuts)
            verify(findChild(page, name), "missing live shortcut " + name)
        verify(findChild(page, "editorOperationBanner"))
        verify(findChild(page, "editorOperationProgress"))
        verify(findChild(page, "editorOperationCancelButton"))
    }

    function test_selectionTimelineAffordancesAndRightClickCancelAreInteractive() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(12000, 36000))
        verify(AudioEditorController.seekFrame(24000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 96000))
        wait(0)

        const leftEdge = findChild(canvas, "editorSelectionStartHandle")
        const rightEdge = findChild(canvas, "editorSelectionEndHandle")
        const eventLeft = findChild(canvas, "editorEventLeftTrimHandle")
        const eventRight = findChild(canvas, "editorEventRightTrimHandle")
        for (const handle of [leftEdge, rightEdge, eventLeft, eventRight]) {
            verify(handle, "missing timeline edge handle")
            verify(handle.width >= 24, handle.objectName + " must expose 24px hit width")
        }

        const playheadCapsule = findChild(canvas, "editorPlayheadTimeCapsule")
        const selectionCapsule = findChild(canvas, "editorSelectionTimeCapsule")
        const durationCapsule = findChild(canvas, "editorSelectionDurationCapsule")
        const dragCapsule = findChild(canvas, "editorSelectionDragCapsule")
        const overlay = findChild(canvas, "editorSelectionOverlay")
        const dragInteraction = findChild(
            canvas, "editorSelectionFileDragInteraction")
        for (const capsule of [playheadCapsule, selectionCapsule, dragCapsule]) {
            verify(capsule, "missing timeline capsule")
            verify(capsule.visible, capsule.objectName + " must be visible")
        }
        compare(durationCapsule, null,
                "duplicate duration capsule must not be rendered")
        verify(playheadCapsule.text.length > 0)
        verify(playheadCapsule.color.a < 1.0,
               "playhead time capsule must remain translucent")
        verify(selectionCapsule.text.indexOf("–") >= 0)
        verify(selectionCapsule.x + selectionCapsule.width
            > overlay.x + overlay.width / 2,
            "selection range capsule must anchor to the top-right")
        verify(dragCapsule.x < overlay.x + overlay.width / 2,
            "drag capsule must anchor to the bottom-left")
        verify(dragInteraction, "selection WAV capsule must be interactive")

        verify(overlay)
        mouseClick(overlay, overlay.width / 2, overlay.height / 2,
                   Qt.RightButton)
        compare(AudioEditorController.selectionStart, -1)
        compare(AudioEditorController.loopEnabled, false)
    }

    function test_fadeHandleAndVolumeLinePersistSampleExactEdits() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 480000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 480000))
        wait(0)
        const fadeIn = findChild(canvas, "editorEventFadeInHandle")
        let fade = findChild(canvas, "editorEventFadeOutHandle")
        let volumeLine = findChild(canvas, "editorEventVolumeLine")
        verify(fadeIn && fade && volumeLine)
        tryVerify(function() { return fadeIn.visible && fadeIn.width > 0 })
        tryVerify(function() { return fade.visible && fade.width > 0 })

        const fadeInDelta = Math.round(canvas.width * 0.12)
        const fadeInStartX = Math.round(fadeIn.width * 0.2)
        const fadeInStart = fadeIn.mapToItem(canvas, fadeInStartX,
                                             fadeIn.height / 2)
        const fadeInTarget = Qt.point(fadeInStart.x + fadeInDelta,
                                      fadeInStart.y)
        const eventStart = Number(AudioEditorController.timelineEventViews[0].timelineStart)
        const expectedFadeIn = canvas.frameAtCanvasPixel(Math.round(fadeInTarget.x))
            - eventStart
        mousePress(canvas, fadeInStart.x, fadeInStart.y, Qt.LeftButton)
        mouseMove(canvas, fadeInTarget.x, fadeInTarget.y, 30)
        mouseRelease(canvas, fadeInTarget.x, fadeInTarget.y, Qt.LeftButton)
        const framePerPixel = Math.ceil(
            AudioEditorController.viewport.visibleFrameCount / canvas.width)
        verify(Math.abs(Number(AudioEditorController.timelineEventViews[0].fadeIn)
            - expectedFadeIn) <= framePerPixel)
        wait(0)
        fade = findChild(canvas, "editorEventFadeOutHandle")
        volumeLine = findChild(canvas, "editorEventVolumeLine")
        verify(fade && volumeLine)

        const dragDelta = -Math.round(canvas.width * 0.2)
        const dragStartX = Math.round(fade.width * 0.8)
        const dragStartPoint = fade.mapToItem(canvas,
            dragStartX, fade.height / 2)
        const targetPoint = Qt.point(
            dragStartPoint.x + dragDelta, dragStartPoint.y)
        const eventEnd = Number(AudioEditorController.timelineEventViews[0].timelineEnd)
        const expectedFade = eventEnd
            - canvas.frameAtCanvasPixel(Math.round(targetPoint.x))
        mousePress(canvas, dragStartPoint.x, dragStartPoint.y, Qt.LeftButton)
        mouseMove(canvas, targetPoint.x, targetPoint.y, 30)
        mouseRelease(canvas, targetPoint.x, targetPoint.y, Qt.LeftButton)
        verify(Math.abs(Number(AudioEditorController.timelineEventViews[0].fadeOut)
            - expectedFade) <= framePerPixel)

        wait(0)
        volumeLine = findChild(canvas, "editorEventVolumeLine")
        verify(volumeLine)
        const gainBefore = Number(AudioEditorController.timelineEventViews[0].gain)
        const gainLineYBefore = volumeLine.y
        const gainStart = volumeLine.mapToItem(
            canvas, volumeLine.width / 2, volumeLine.height / 2)
        const gainEnd = Qt.point(
            gainStart.x, gainStart.y - Math.round(volumeLine.height / 4))
        mousePress(canvas, gainStart.x, gainStart.y, Qt.LeftButton)
        mouseMove(canvas, gainEnd.x, gainEnd.y, 30)
        verify(volumeLine.y < gainLineYBefore,
            "gain line must follow the pointer before release")
        mouseRelease(canvas, gainEnd.x, gainEnd.y, Qt.LeftButton)
        verify(Number(AudioEditorController.timelineEventViews[0].gain) > gainBefore)
        verify(AudioEditorController.undo())
        compare(Number(AudioEditorController.timelineEventViews[0].gain), gainBefore)

        wait(0)
        volumeLine = findChild(canvas, "editorEventVolumeLine")
        verify(volumeLine)
        const clickX = Math.round(volumeLine.width * 0.45)
        const clickY = Math.round(volumeLine.height * 0.25)
        const envelopePoint = volumeLine.mapToItem(canvas, clickX, clickY)
        const expectedOffset = canvas.frameAtCanvasPixel(envelopePoint.x)
            - Number(AudioEditorController.timelineEventViews[0].timelineStart)
        mouseDoubleClickSequence(canvas, envelopePoint.x, envelopePoint.y)
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

    function test_activeRecordingShowsTimelineWaveformAndFallingMeter() {
        const waveform = findChild(page, "editorWaveformGeometry")
        const recordingOverlay = findChild(page, "editorRecordingOverlayWaveform")
        const playheadCapsule = findChild(page, "editorPlayheadTimeCapsule")
        const recordingTime = findChild(page, "recordingTimeText")
        const meter = findChild(page, "inspectorInputMeter")
        const activeMeterSegment = findChild(
            page, "inspectorInputMeterSegment1")
        verify(waveform && recordingOverlay && playheadCapsule && recordingTime && meter
               && activeMeterSegment)
        const output = RecordingTestDriver.nextOutputUrl()
        verify(output.toString().length > 0)
        verify(AudioEditorController.startRecording(
            output, "", 16000, 2, false, false))
        tryVerify(function() {
            return AudioEditorController.recording
                && !AudioEditorController.busy
        }, 5000)

        verify(RecordingTestDriver.feedActive(16000))
        tryCompare(AudioEditorController, "playheadFrame", 16000, 1000)
        tryCompare(recordingTime, "text", "00:00:01", 1000)
        tryVerify(function() { return playheadCapsule.visible }, 1000)
        tryVerify(function() {
            return playheadCapsule.text !== "00:00.000"
        }, 1000)
        tryVerify(function() {
            return recordingOverlay.visible
                && recordingOverlay.channelPeaks.length === 1
        }, 2000)
        tryVerify(function() { return meter.level > 0.79 }, 1000)
        tryVerify(function() { return activeMeterSegment.opacity > 0.9 }, 1000)

        verify(RecordingTestDriver.feedQuiet(16000))
        tryCompare(AudioEditorController, "playheadFrame", 32000, 1000)
        tryVerify(function() { return meter.level < 0.01 }, 1000)
        tryVerify(function() { return activeMeterSegment.opacity < 0.2 }, 1000)
        verify(AudioEditorController.stopRecording())
        tryVerify(function() {
            return !AudioEditorController.recording
                && AudioEditorController.hasDocument
        }, 5000)
        compare(AudioEditorController.playheadFrame, 0)
        compare(AudioEditorController.positionMs, 0)
        compare(AudioEditorController.recordingFrames, 0)
        compare(waveform.visible, true)
    }

    function test_selectionResizeEdgesExposeVisibleCues() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(12000, 36000))
        const startCue = findChild(page, "editorSelectionStartResizeCue")
        const endCue = findChild(page, "editorSelectionEndResizeCue")
        verify(startCue && endCue)
        verify(startCue.visible && endCue.visible)
        verify(startCue.width >= 2 && endCue.width >= 2)
        verify(findChild(page, "editorSelectionStartHandle").width >= 24)
        verify(findChild(page, "editorSelectionEndHandle").width >= 24)
    }

    function test_recordingDeviceRefreshIsAvailableFromTheInspector() {
        const refresh = findChild(page, "recordingDeviceRefreshButton")
        verify(refresh && refresh.visible)
        mouseClick(refresh)
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
            { tag: "wide", w: 1672, h: 849, access: "editorInspectorScroller",
              wide: true, medium: false, compact: false },
            { tag: "medium", w: 1280, h: 628, access: "editorInspectorScroller",
              wide: false, medium: true, compact: false },
            { tag: "medium-boundary", w: 1000, h: 628, access: "editorInspectorScroller",
              wide: false, medium: true, compact: false },
            { tag: "compact-boundary", w: 999, h: 468, access: "editorInspectorAccess",
              wide: false, medium: false, compact: true },
            { tag: "minimum", w: 880, h: 468, access: "editorInspectorAccess",
              wide: false, medium: false, compact: true }
        ]
    }

    function test_responsivePlaybackAndInspectorAccess(data) {
        host.width = data.w
        host.height = data.h
        wait(0)
        compare(page.referenceLayout, data.wide)
        compare(page.mediumLayout, data.medium)
        compare(page.compactInspectorLayout, data.compact)
        const playback = findChild(page, "editorPlaybackTransport")
        const main = findChild(page, "editorMainColumn")
        const inspector = findChild(page, "editorInspector")
        verify(main && inspector)
        verify(main.x >= 0 && main.x + main.width <= page.width)
        if (!data.compact) {
            verify(inspector.visible)
            verify(inspector.x >= main.x + main.width)
            verify(inspector.x + inspector.width <= page.width)
            if (data.medium) {
                compare(inspector.width, page.mediumInspectorWidth)
                compare(main.width + inspector.width, page.width)
            }
        }
        const exportGroup = findChild(page, "inspectorExportGroup")
        const access = findChild(page, data.access)
        verify(playback && playback.visible)
        verify(exportGroup)
        verify(access && access.visible)
        verify(findChild(page, "editorCommand_importAudio").visible)
        if (data.compact) {
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
            const tabs = findChild(page, "editorCompactInspectorTabs")
            verify(inspector.visible && scroller && exportButton && tabs)
            verify(tabs.visible)
            tabs.currentIndex = 2
            tryVerify(function() { return exportButton.visible })
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

    function test_runtimeMatrixKeepsPrimaryEditorRegionsReachable_data() {
        return [
            { tag: "minimum", w: 880, h: 560 },
            { tag: "medium-start", w: 1000, h: 720 },
            { tag: "medium", w: 1280, h: 720 },
            { tag: "wide", w: 1672, h: 942 }
        ]
    }

    function test_runtimeMatrixKeepsPrimaryEditorRegionsReachable(data) {
        host.width = data.w
        host.height = data.h
        wait(0)
        const main = findChild(page, "editorMainColumn")
        const command = findChild(page, "editorCommandBar")
        const timeline = findChild(page, "editorTimelineWorkspace")
        const recording = findChild(page, "editorRecordingTransport")
        const playback = findChild(page, "editorPlaybackTransport")
        const access = findChild(page, "editorInspectorAccess")
        verify(main && command && timeline && recording && playback && access)
        verify(main.x >= 0 && main.x + main.width <= page.width)
        for (const item of [command, timeline, recording, playback]) {
            const position = item.mapToItem(main.contentItem, 0, 0)
            verify(position.x >= 0 && position.x + item.width <= main.contentWidth)
            verify(position.y >= 0 && position.y + item.height <= main.contentHeight)
        }
        if (page.compactInspectorLayout) {
            verify(access.visible)
            verify(access.x >= 0 && access.x + access.width <= page.width)
            mouseClick(access)
            const inspector = findChild(page, "editorInspector")
            const tabs = findChild(page, "editorCompactInspectorTabs")
            verify(inspector.visible && tabs.visible)
            verify(inspector.x >= 0 && inspector.x + inspector.width <= page.width)
            mouseClick(access)
        }
    }

    function test_compactRecordingStateKeepsTimelineMeterAndStopReachable() {
        host.width = 880
        host.height = 560
        wait(0)
        const output = RecordingTestDriver.nextOutputUrl()
        verify(AudioEditorController.startRecording(
            output, "", 16000, 2, false, false))
        tryVerify(function() { return AudioEditorController.recording }, 5000)
        const overlay = findChild(page, "editorRecordingOverlayWaveform")
        const stop = findChild(page, "recordingStopButton")
        const access = findChild(page, "editorInspectorAccess")
        verify(overlay && stop && stop.visible && access && access.visible)
        mouseClick(access)
        const inspector = findChild(page, "editorInspector")
        const tabs = findChild(page, "editorCompactInspectorTabs")
        tabs.currentIndex = 0
        const meter = findChild(page, "inspectorInputMeter")
        verify(inspector.visible && meter && meter.visible)
        verify(inspector.x + inspector.width <= page.width)
        verify(AudioEditorController.stopRecording())
        tryVerify(function() { return !AudioEditorController.recording }, 5000)
    }

    function test_exportGroupShowsPersistedReadOnlyFields() {
        for (const name of ["editorExportCodec", "editorExportSampleRate",
                            "editorExportBitDepth", "editorExportChannels",
                            "editorExportBitRate", "editorExportDirectory"]) {
            const field = findChild(page, name)
            verify(field, "missing read-only export field " + name)
            verify(field.text.length > 0)
        }
        compare(findChild(page, "editorExportBitDepth").text, "24-bit")
        compare(findChild(page, "editorExportDirectory").text,
                SettingsController.defaultOutputDirectory || "--")
        compare(findChild(page, "audioEditorExportDialog"), null)
        compare(findChild(page, "editorExportButton").enabled, false)
    }

    function test_exportAndPitchControlsFollowSharedSettings() {
        const originalFormat = SettingsController.transcodeFormat
        const originalRate = SettingsController.transcodeSampleRateHz
        const originalChannels = SettingsController.transcodeChannels
        const originalBitrate = SettingsController.transcodeBitrateKbps
        const originalDirectory = SettingsController.defaultOutputDirectory
        const originalPitch = SettingsController.keepPitchWhileSpeedChange
        const originalVocal = SettingsController.vocalProtection
        SettingsController.transcodeFormat = "OPUS"
        SettingsController.transcodeSampleRateHz = 48000
        SettingsController.transcodeChannels = 1
        SettingsController.transcodeBitrateKbps = 192
        SettingsController.defaultOutputDirectory = "C:/shared-export"
        SettingsController.keepPitchWhileSpeedChange = true
        SettingsController.vocalProtection = true
        tryCompare(findChild(page, "editorExportCodec"), "text", "Opus")
        tryCompare(findChild(page, "editorExportSampleRate"), "text", "48 kHz")
        tryCompare(findChild(page, "editorExportChannels"), "text", "单声道")
        tryCompare(findChild(page, "editorExportBitRate"), "text", "192 kbps")
        tryCompare(findChild(page, "editorExportDirectory"), "text", "C:/shared-export")
        tryCompare(findChild(page, "inspectorPreservePitchSwitch"), "checked", true)
        tryCompare(findChild(page, "inspectorFormantSwitch"), "checked", true)
        SettingsController.transcodeFormat = originalFormat
        SettingsController.transcodeSampleRateHz = originalRate
        SettingsController.transcodeChannels = originalChannels
        SettingsController.transcodeBitrateKbps = originalBitrate
        SettingsController.defaultOutputDirectory = originalDirectory
        SettingsController.keepPitchWhileSpeedChange = originalPitch
        SettingsController.vocalProtection = originalVocal
    }
}
