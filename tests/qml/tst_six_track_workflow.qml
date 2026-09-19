import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "SixTrackEditorWorkflow"
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

    Component {
        id: editorPlaybackHistoryComponent
        Connections {
            property var samples: []
            target: AudioEditorController
            function onPlaybackChanged() {
                samples.push({
                    playing: AudioEditorController.playing,
                    error: AudioEditorController.errorMessage
                })
            }
        }
    }

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

    function verifyAscendingX(parent, names) {
        let previousX = -1
        for (let index = 0; index < names.length; ++index) {
            const item = findChild(parent, names[index])
            verify(item, "missing " + names[index])
            const x = item.mapToItem(parent, 0, 0).x
            verify(x > previousX, names[index] + " must follow its predecessor")
            previousX = x
        }
    }

    function test_sharedTopNavigationHasFixedLeftInsetAndOrder() {
        var window = createTemporaryObject(shellComponent, testCase)
        verify(window)
        wait(0)
        var nav = findChild(window, "audioToolsTopNav")
        var first = findChild(nav, "audioToolNav_0")
        compare(first.mapToItem(nav, 0, 0).x, 12)
        var names = ["audioToolNav_0", "audioToolNav_4", "audioToolNav_1",
                     "audioToolNav_2", "audioToolNav_3"]
        verifyAscendingX(nav, names)
        compare(nav.height, Theme.settingsRowHeight)
        for (var index = 0; index < names.length; ++index)
            compare(findChild(nav, names[index]).height, first.height)
        compare(first.height, nav.height)
    }

    function test_sharedTopNavigationKeepsOneHostXAcrossAllTools() {
        var window = createTemporaryObject(shellComponent, testCase)
        verify(window)
        wait(0)
        var nav = findVisibleItem(window.contentItem, "audioToolsTopNav", window)
        verify(nav)
        var expectedHostX = -1
        var toolIds = [0, 4, 1, 2, 3]
        for (var index = 0; index < toolIds.length; ++index) {
            AudioToolsController.selectTool(toolIds[index])
            wait(0)
            var hostX = nav.mapToItem(window.contentItem, 0, 0).x
            if (expectedHostX < 0)
                expectedHostX = hostX
            else
                compare(Math.round(hostX), Math.round(expectedHostX),
                        "tool " + toolIds[index]
                        + " must keep the shared navigation left edge")
        }
        AudioToolsController.selectTool(0)
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

    function test_composedToolsShellUsesLightPalette() {
        const previousMode = SettingsController.themeMode
        var shell = null

        try {
            SettingsController.themeMode = 1
            wait(0)
            shell = createTemporaryObject(shellComponent, testCase)
            verify(shell)
            tryVerify(function() { return shell.visible })
            const titleBar = findChild(shell, "audioToolsTitleBar")
            const titleText = findChild(shell, "audioToolsWindowTitle")
            const contentStack = findChild(shell, "audioToolsContentStack")
            const minimize = findChild(shell, "audioToolsMinimizeButton")
            const maximize = findChild(shell, "audioToolsMaximizeButton")
            const close = findChild(shell, "audioToolsCloseButton")
            const moveArea = findChild(shell, "audioToolsMoveArea")
            const topNav = findChild(shell, "audioToolsTopNav")
            verify(titleBar && titleText && contentStack
                   && minimize && maximize && close && moveArea)
            verify(topNav)
            compare(titleBar.color.toString(), Theme.titleBarSurface.toString())
            compare(titleText.color.toString(), Theme.primaryText.toString())
            compare(contentStack.color.toString(), Theme.contentSurface.toString())
            compare(minimize.contentItem.tint.toString(),
                    Theme.iconPrimary.toString())
            compare(maximize.contentItem.tint.toString(),
                    Theme.iconPrimary.toString())
            compare(close.contentItem.tint.toString(),
                    Theme.iconPrimary.toString())
            const moveRight = moveArea.mapToItem(titleBar,
                                                  moveArea.width, 0).x
            const minimizeLeft = minimize.mapToItem(titleBar, 0, 0).x
            verify(moveRight <= minimizeLeft,
                   "window move area must not cover minimize hit target")
            compare(topNav.activeLabelColor.toString(),
                    Theme.primaryText.toString())
            compare(findChild(shell, "skinBackdrop"), null)
        } finally {
            if (shell)
                shell.destroy()
            SettingsController.themeMode = previousMode
            wait(0)
        }
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

        const primaryPlay = findChild(shellPage, "editorPlaybackToggleButton")
        verify(primaryPlay)
        compare(primaryPlay.focusPolicy, Qt.TabFocus)

        if (AudioEditorController.hasDocument)
            verify(AudioEditorController.clearDocument())
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        verify(AudioEditorController.setActiveTool("select"))
        primaryPlay.forceActiveFocus()
        tryVerify(function() { return primaryPlay.activeFocus })
        tryVerify(function() {
            return AudioEditorController.editorPlaybackOwnsPlayer
        })
        verify(spaceShortcut.enabled)
        const activated = createTemporaryObject(signalSpyComponent, testCase,
            { target: spaceShortcut, signalName: "activated" })
        const editorPlayback = createTemporaryObject(
            editorPlaybackHistoryComponent, testCase)
        const mainPlayback = createTemporaryObject(signalSpyComponent, testCase,
            { target: PlaybackController, signalName: "stateChanged" })
        verify(activated.valid)
        verify(editorPlayback)
        verify(mainPlayback.valid)
        keyClick(Qt.Key_Space)
        tryCompare(activated, "count", 1)
        tryVerify(function() {
            return editorPlayback.samples.some(function(sample) {
                return sample.playing
            }) || AudioEditorController.errorMessage.length > 0
        })
        compare(mainPlayback.count, 0)
        if (!AudioEditorController.playing)
            verify(AudioEditorController.errorMessage.length > 0)
        compare(activated.count, 1)
        compare(AudioEditorController.activeTool, "select")
        editorPlayback.enabled = false
        AudioEditorController.deactivate()
        AudioEditorController.clearDocument()
        tryCompare(AudioEditorController, "hasDocument", false)
        AudioEditorController.activate()
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

    function test_editorSlidersCenterTracksAndHandlesInTheirHitArea() {
        for (const sliderName of ["inspectorSpeedSlider",
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
        for (const sliderName of ["inspectorSpeedSlider",
                                  "inspectorPitchSlider"]) {
            const slider = findChild(page, sliderName)
            const groove = findChild(slider, "editorSliderGroove")
            const active = findChild(slider, "editorSliderActiveSegment")
            verify(slider && groove && active)
            verify(groove.clip)
            const activePosition = active.mapToItem(slider, 0, 0)
            const groovePosition = groove.mapToItem(slider, 0, 0)
            if (slider.orientation === Qt.Horizontal) {
                compare(active.height, Theme.sliderTrackHeight)
                verify(activePosition.y >= groovePosition.y)
                verify(activePosition.y + active.height
                       <= groovePosition.y + groove.height + 0.5)
            } else {
                compare(active.width, Theme.sliderTrackHeight)
                verify(activePosition.x >= groovePosition.x)
                verify(activePosition.x + active.width
                       <= groovePosition.x + groove.width + 0.5)
            }
        }
    }

    function test_exportButtonExposesGreenProgressAndCompletionPresentation() {
        const button = findChild(page, "editorExportButton")
        const progressFill = findChild(page, "editorExportProgressFill")
        verify(button && progressFill)
        verify(button.font.bold)
        compare(button.font.pixelSize, Theme.fontSizeBody)
        compare(button.text, "导出音频")
        compare(progressFill.width, 0)
        compare(progressFill.color.toString(), Theme.success.toString())
    }

    function test_configuredExportOffersOnlyValidSelectionRange() {
        const selectionOnly = findChild(page, "editorExportSelectionOnly")
        verify(selectionOnly)
        compare(selectionOnly.enabled, false)
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        compare(selectionOnly.enabled, false)
        verify(AudioEditorController.setSelection(12000, 36000))
        tryCompare(selectionOnly, "enabled", true)
        selectionOnly.checked = true
        verify(AudioEditorController.clearSelection())
        tryCompare(selectionOnly, "checked", false)
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

    function test_realWheelEventUsesContractZoomFactors() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        const interaction = findChild(page, "editorWaveformInteraction")
        verify(interaction)
        AudioEditorController.viewport.setViewportWidth(interaction.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 48000))
        const before = AudioEditorController.viewport.visibleFrameCount

        mouseWheel(interaction, interaction.width / 2, interaction.height / 2,
                   0, 120, Qt.NoButton, Qt.NoModifier)
        compare(AudioEditorController.viewport.visibleFrameCount,
                Math.round(before / 1.25))
        mouseWheel(interaction, interaction.width / 2, interaction.height / 2,
                   0, -120, Qt.NoButton, Qt.NoModifier)
        verify(Math.abs(AudioEditorController.viewport.visibleFrameCount - before) <= 1)
        const start = AudioEditorController.viewport.visibleStartFrame
        mouseWheel(interaction, interaction.width / 2, interaction.height / 2,
                   0, -120, Qt.NoButton, Qt.ControlModifier)
        compare(AudioEditorController.viewport.visibleFrameCount, before)
        verify(AudioEditorController.viewport.visibleStartFrame > start)
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
