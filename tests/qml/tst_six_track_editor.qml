import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "SixTrackEditor"
    when: windowShown
    width: 1672
    height: 822
    visible: true
    property int originalThemeMode: 0
    function initTestCase() { originalThemeMode = SettingsController.themeMode }
    function cleanupTestCase() { SettingsController.themeMode = originalThemeMode }
    Component {
        id: pageComponent
        Window {
            // Keep fixture geometry independent of the QuickTest host window.
            // Offscreen hosts can initially report a 0/64-pixel content size.
            width: 1672
            height: 822
            visible: true
            AudioEditorPage { anchors.fill: parent }
        }
    }
    Component { id: shellComponent; AudioToolsWindow { visible: true } }
    function init() {
        failOnWarning(/Binding loop detected|Cannot open: qrc|TypeError|ReferenceError/)
        AudioToolsController.selectTool(0)
        if (AudioEditorController.hasDocument && !AudioEditorController.busy) {
            if (!AudioEditorController.clearDocument()) verify(AudioEditorController.confirmDiscardAndOpen())
        }
    }
    function cleanup() {
        AudioEditorController.stopPlayback()
        if (AudioEditorController.hasDocument && !AudioEditorController.busy) {
            if (!AudioEditorController.clearDocument()) verify(AudioEditorController.confirmDiscardAndOpen())
        }
    }
    function inside(item, owner) {
        verify(item && item.visible, "Missing visible control")
        const p = item.mapToItem(owner, 0, 0)
        const end = item.mapToItem(owner, item.width, item.height)
        verify(p.x >= -1 && p.y >= -1 && end.x <= owner.width + 1
            && end.y <= owner.height + 1,
            item.objectName + " outside page: " + p.x + "," + p.y + " " + item.width + "x" + item.height)
    }
    function createEditorHost() {
        const window = createTemporaryObject(pageComponent, testCase)
        verify(window)
        waitForRendering(window.contentItem)
        tryCompare(window.contentItem, "width", 1672)
        return window
    }
    function importPcm(count) {
        if (typeof testAudioUrl === "undefined" || !testAudioUrl.toString().length)
            skip("This runner has no real PCM fixture")
        let sources = []
        for (let i = 0; i < count; ++i) sources.push(testAudioUrl)
        verify(AudioEditorController.addFiles(sources))
        tryVerify(function() { return !AudioEditorController.busy && AudioEditorController.timelineEventViews.length === count }, 15000)
    }
    function test_soloAndScopedTempoPitchControls() {
        importPcm(2)
        const host = createEditorHost()
        const root = host.contentItem
        const solo = visualChild(root, "editorTrackSolo0")
        const mute = visualChild(root, "editorTrackMute0")
        const tempoScope = visualChild(root, "inspectorTempoScope")
        const pitchScope = visualChild(root, "inspectorPitchScope")
        const speed = visualChild(root, "inspectorSpeedSlider")
        const pitchPlus = visualChild(root, "inspectorPitchPlus")
        verify(solo && mute && tempoScope && pitchScope && speed && pitchPlus,
               "missing " + [!!solo, !!mute, !!tempoScope, !!pitchScope, !!speed, !!pitchPlus].join(","))
        verify(solo.x > mute.x && solo.x + solo.width <= mute.parent.width)
        mouseClick(solo)
        compare(AudioEditorController.tracks[0].solo, true)
        compare(AudioEditorController.tracks[1].solo, false)
        tempoScope.currentIndex = 1
        pitchScope.currentIndex = 1
        AudioEditorController.selectedTrack = 0
        verify(AudioEditorController.setTimelineTrackSpeedPercent(0, 125))
        tryCompare(speed, "value", 1.25)
        mouseClick(pitchPlus)
        compare(AudioEditorController.timelineTrackPitchSemitones(0), 1)
        compare(AudioEditorController.timelineTrackPitchSemitones(1), 0)
        compare(AudioEditorController.timelineTrackSpeedPercent(0), 125)
        for (const event of AudioEditorController.timelineEventViews)
            compare(event.speedRatio, event.trackIndex === 0 ? 1.25 : 1)
        tempoScope.currentIndex = 0
        pitchScope.currentIndex = 0
        verify(AudioEditorController.setTimelineAllSpeedPercent(150))
        verify(AudioEditorController.setTimelineAllPitch(2))
        for (const event of AudioEditorController.timelineEventViews) {
            compare(event.speedRatio, 1.5)
            compare(event.pitchSemitone, 2)
        }
        AudioEditorController.selectEvent(String(AudioEditorController.timelineEventViews[0].id))
        waitForRendering(root)
        if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length)
            grabImage(root).save(visualFixtureOutput + "-scoped-selected.png")
    }
    function test_realPcmLayout_data() {
        return [{tag: "reference", w: 1672, h: 941, mode: 0}, {tag: "small", w: 1000, h: 700, mode: 0},
            {tag: "minimum", w: 760, h: 420, mode: 0}, {tag: "light", w: 1672, h: 941, mode: 1}]
    }
    function test_realPcmLayout(data) {
        SettingsController.themeMode = data.mode
        importPcm(6)
        const host = createTemporaryObject(shellComponent, testCase, {width: data.w, height: data.h})
        verify(host)
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null })
        const page = visualChild(host.contentItem, "audioEditorPage")
        verify(page)
        waitForRendering(page)
        wait(0) // Flush Column/Row positioners before measuring their children.
        const inspector = visualChild(page, "editorInspector")
        verify(inspector.visible)
        const layoutOwner = page.macStackedLayout
            ? visualChild(page, "editorMainColumn").parent : page
        compare(inspector.x, page.macStackedLayout ? 0 : page.mainWidth)
        if (page.macStackedLayout)
            verify(inspector.y >= visualChild(page, "editorMainColumn").height)
        verify(inspector.width >= (page.macDesktopLayout ? 220 : 300))
        inside(inspector, layoutOwner)
        const device = visualChild(page, "editorInputDevice")
        const meter = visualChild(page, "editorRecordingLevel")
        const recordingGain = visualChild(page, "editorRecordingGain")
        inside(meter, layoutOwner)
        inside(recordingGain, layoutOwner)
        compare(meter.width, device.width)
        verify(meter.mapToItem(page, 0, meter.height).y <= device.mapToItem(page, 0, 0).y)
        compare(device.palette.text, Theme.primaryText)
        compare(visualChild(page, "editorTrackHeader0").border.width, 0)
        const exportButton = visualChild(page, "editorExportButton")
        if (data.tag === "reference") {
            let measurements = {pageTop: page.mapToItem(host.contentItem, 0, 0).y, pageHeight: page.height}
            for (const name of ["editorCommandBar", "editorTimeRuler", "editorTrackScroller", "editorTrackHeader0",
                    "editorInspector", "inspectorTempoGroup", "inspectorPitchGroup", "inspectorPreservePitchGroup",
                    "inspectorExportGroup", "editorExportButton", "editorRecordingTransport", "editorRecordButton", "editorPlaybackTransport"]) {
                const item = visualChild(page, name)
                const p = item.mapToItem(page, 0, 0)
                measurements[name] = {x: p.x, y: p.y, width: item.width, height: item.height}
            }
            console.log("SIX_TRACK_GEOMETRY " + JSON.stringify(measurements))
        }
        compare(exportButton.background.color, Theme.accent)
        compare(exportButton.contentItem.color, Theme.accentText)
        for (const name of ["editorCommandBar", "editorRecordingTransport", "editorInputDevice", "editorRecordButton", "editorPauseRecordingButton", "editorPlaybackTransport", "editorCurrentTime"])
            inside(visualChild(page, name), layoutOwner)
        const scroller = visualChild(page, "editorTrackScroller")
        verify(scroller.height >= 82)
        const transport = visualChild(page, "editorRecordingTransport")
        verify(transport.y >= scroller.y + scroller.height,
               "Transport must remain below the timeline")
        inside(scroller, layoutOwner)
        const first = AudioEditorController.timelineEventViews[0]
        const waveform = visualChild(page, "editorWaveformGeometry_" + first.id)
        verify(waveform)
        verify(waveform.width > 10, "Decoded clip must occupy the viewport, not a stale one-pixel geometry: " + waveform.width)
        tryVerify(function() { return waveform.channelPeaks.length > 0 })
        waitForRendering(page)
        if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0) {
            wait(350) // Allow the real viewport-detail decoder's 250 ms debounce.
            grabImage(host.contentItem).save(visualFixtureOutput + "-" + data.tag + ".png")
        }
        if (data.tag === "small" || data.tag === "minimum") {
            scroller.contentY = scroller.contentHeight - scroller.height
            waitForRendering(page)
            const last = visualChild(page, "editorTrackHeader5")
            const p = last.mapToItem(scroller, 0, 0)
            verify(p.y >= -1 && p.y + last.height <= scroller.height + 1, "sixth track must remain vertically reachable")
        }
    }
    function test_clipMoveKeepsMouseGrabAndOneUndo() {
        importPcm(1)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null })
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        verify(canvas)
        const original = AudioEditorController.timelineEventViews[0]
        AudioEditorController.viewport.setVisibleRange(0, AudioEditorController.totalFrames * 3)
        waitForRendering(canvas)
        const startX = canvas.pixelAtFrame(Number(original.timelineStart) + Math.floor((Number(original.timelineEnd) - Number(original.timelineStart)) / 4))
        const frameAtPress = canvas.frameAtCanvasPixel(startX)
        mousePress(canvas, startX, 10, Qt.LeftButton)
        mouseMove(canvas, startX + 35, canvas.rowHeight + 10, 20)
        mouseMove(canvas, startX + 70, canvas.rowHeight * 2 + 10, 20)
        mouseRelease(canvas, startX + 70, canvas.rowHeight * 2 + 10, Qt.LeftButton)
        const moved = AudioEditorController.timelineEventViews[0]
        compare(String(moved.id), String(original.id))
        compare(moved.trackIndex, 2)
        verify(Math.abs(Number(moved.timelineStart) - (canvas.frameAtCanvasPixel(startX + 70) - frameAtPress)) <= 1,
               "move must preserve the original grab offset")
        verify(AudioEditorController.triggerAction("editor.undo"))
        compare(AudioEditorController.timelineEventViews[0].trackIndex, 0)
        compare(AudioEditorController.timelineEventViews[0].timelineStart, original.timelineStart)
    }
    function test_rightClickCreatesVolumeLineThenControlPoint_data() {
        return [{tag: "dark", mode: 0}, {tag: "light", mode: 1}]
    }
    function test_rightClickCreatesVolumeLineThenControlPoint(data) {
        SettingsController.themeMode = data.mode
        importPcm(1)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorWaveformCanvas") !== null })
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        const x = Math.round(canvas.width * 0.4)
        mouseClick(canvas, x, canvas.rowHeight - 18, Qt.RightButton)
        const lineAction = findChild(host, "editorAddVolumeLine")
        const menu = findChild(host, "editorClipContextMenu")
        tryCompare(menu, "opened", true)
        waitForRendering(host.contentItem)
        verify(lineAction, "Waveform context menu must offer a real volume line")
        tryVerify(function() { return lineAction.visible && lineAction.enabled })
        mouseClick(lineAction, lineAction.width / 2, lineAction.height / 2)
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 1)
        compare(AudioEditorController.timelineEventViews[0].envelope[0].gain, 1)
        tryCompare(menu, "visible", false)
        mouseClick(canvas, x, canvas.gainY(1, 0), Qt.RightButton)
        tryCompare(menu, "opened", true)
        const pointAction = findChild(host, "editorAddVolumePoint")
        verify(pointAction)
        tryVerify(function() { return pointAction.visible && pointAction.enabled })
        compare(menu.background.color, Theme.surfaceElevated)
        const submenuEntry = menu.itemAt(menu.count - 1)
        verify(submenuEntry && submenuEntry.subMenu)
        compare(submenuEntry.contentItem.color, Theme.primaryText)
        if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0)
            grabImage(host.contentItem).save(visualFixtureOutput + "-menu-" + data.tag + ".png")
        mouseClick(pointAction, pointAction.width / 2, pointAction.height / 2)
        const points = AudioEditorController.timelineEventViews[0].envelope
        compare(points.length, 2)
        verify(Math.abs(points[1].offset - canvas.frameAtCanvasPixel(x)) <= 1)
        compare(points[1].gain, 1)
        verify(AudioEditorController.undo())
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 1)
        verify(AudioEditorController.undo())
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 0)
    }
    function test_clipEdgesTrimAtPlayheadAndExactEndAndCancel() {
        importPcm(2)
        const first = AudioEditorController.timelineEventViews[0]
        verify(AudioEditorController.trimEvent(String(first.id), 0, Math.floor(Number(first.sourceEnd) * 0.75), 0))
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorWaveformCanvas") !== null })
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        const original = canvas.eventById(String(first.id))
        verify(AudioEditorController.viewport.setVisibleRange(0, AudioEditorController.totalFrames))
        waitForRendering(canvas)
        const y = canvas.rowHeight - 18
        mousePress(canvas, 1, y, Qt.LeftButton)
        mouseMove(canvas, 40, y, 20)
        verify(canvas.eventById(String(first.id)).sourceStart > 0, "Left edge at playhead must trim, not scrub")
        mouseRelease(canvas, 40, y, Qt.LeftButton)
        verify(AudioEditorController.undo())
        compare(canvas.eventById(String(first.id)).sourceStart, original.sourceStart)
        const right = canvas.pixelAtFrame(Number(original.timelineEnd))
        mousePress(canvas, right, y, Qt.LeftButton)
        mouseMove(canvas, right - 40, y, 20)
        verify(canvas.eventById(String(first.id)).sourceEnd < original.sourceEnd, "Exact exclusive right boundary must be draggable")
        keyClick(Qt.Key_Escape)
        mouseRelease(canvas, right - 40, y, Qt.LeftButton)
        compare(canvas.eventById(String(first.id)).sourceEnd, original.sourceEnd)
        mousePress(canvas, right, y, Qt.LeftButton)
        mouseMove(canvas, right - 40, y, 20)
        mouseRelease(canvas, right - 40, y, Qt.LeftButton)
        verify(canvas.eventById(String(first.id)).sourceEnd < original.sourceEnd)
        verify(AudioEditorController.undo())
        compare(canvas.eventById(String(first.id)).sourceEnd, original.sourceEnd)
    }
    function test_controlDragPansWithoutMovingClip() {
        importPcm(2)
        const host = createEditorHost()
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        const viewport = AudioEditorController.viewport
        viewport.zoomAt(4, 0)
        const original = AudioEditorController.timelineEventViews[0]
        mousePress(canvas, canvas.width * 0.8, 45, Qt.LeftButton, Qt.ControlModifier)
        mouseMove(canvas, canvas.width * 0.3, 45, 20)
        mouseRelease(canvas, canvas.width * 0.3, 45, Qt.LeftButton, Qt.ControlModifier)
        verify(viewport.visibleStartFrame > 0)
        compare(canvas.eventById(String(original.id)).timelineStart, original.timelineStart)
        const bar = visualChild(host.contentItem, "editorTimelineScrollBar")
        verify(bar && bar.visible)
        mousePress(bar, bar.width * (bar.position + bar.size / 2), bar.height / 2)
        mouseMove(bar, bar.width * 0.75, bar.height / 2, 20)
        mouseRelease(bar, bar.width * 0.75, bar.height / 2)
        verify(viewport.overviewStartRatio > 0.3)
    }
    function test_quietWaveformDisplayDoesNotInventSilentSignal() {
        const host = createEditorHost()
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        const quiet = canvas.readablePeaks([[-0.01, 0.01]])
        fuzzyCompare(quiet[0][1], 0.01, 0.000001)
        const contour = canvas.readablePeaks([[-1, 1, -0.5, 0.5, -0.1, 0.1]])[0]
        compare(contour[1], 1)
        compare(contour[3] / contour[1], 0.5)
        compare(canvas.readablePeaks([[0, 2]])[0][1], 1)
        compare(canvas.readablePeaks([[0, 0]])[0][1], 0)
        compare(canvas.readablePeaks([[0, null]])[0][1], null)
    }
    function test_muteShortcutDoesNotFireInsideTextField() {
        importPcm(1)
        const host = createEditorHost()
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        canvas.forceActiveFocus()
        keyClick(Qt.Key_M)
        tryCompare(AudioEditorController.tracks[0], "muted", true)
        const input = visualChild(host.contentItem, "inspectorBpmInput")
        input.forceActiveFocus()
        keyClick(Qt.Key_M)
        compare(AudioEditorController.tracks[0].muted, true)
        canvas.forceActiveFocus()
        keyClick(Qt.Key_M)
        tryCompare(AudioEditorController.tracks[0], "muted", false)
    }
    function test_trackGainWheelAndDoubleClickReset() {
        importPcm(1)
        const host = createEditorHost()
        const slider = visualChild(host.contentItem, "editorTrackGain0")
        verify(slider)
        verify(AudioEditorController.setTrackGain(0, 1.5))
        mouseWheel(slider, slider.width / 2, slider.height / 2, 0, 120)
        tryVerify(function() { return Math.abs(AudioEditorController.tracks[0].gain - 1.55) < 0.001 })
        mouseDoubleClickSequence(slider, slider.width * 0.8, slider.height / 2)
        tryVerify(function() { return Math.abs(AudioEditorController.tracks[0].gain - 1) < 0.001 })
        tryCompare(slider, "value", 1)
        compare(slider.position, 0.5)
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        mouseMove(canvas, 1, canvas.rowHeight - 16)
        compare(visualChild(canvas, "editorWaveformInteraction").cursorShape, Qt.SizeHorCursor)
        const clip = visualChild(canvas, "editorClip_" + AudioEditorController.timelineEventViews[0].id)
        compare(clip.border.width, 0)
    }
    function test_bodySelectionRetainsDragOutEntry() {
        importPcm(3)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorWaveformCanvas") !== null })
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        const y = canvas.rowHeight - 18
        mousePress(canvas, canvas.width * 0.2, y, Qt.RightButton)
        mouseMove(canvas, canvas.width * 0.6, y, 20)
        mouseRelease(canvas, canvas.width * 0.6, y, Qt.RightButton)
        verify(AudioEditorController.selectionEnd > AudioEditorController.selectionStart)
        compare(visualChild(canvas, "editorSelectionOverlay").height, canvas.rowHeight)
        const start = AudioEditorController.selectionStart, end = AudioEditorController.selectionEnd
        const capsule = visualChild(canvas, "editorSelectionDragCapsule")
        verify(capsule && capsule.visible)
        mousePress(capsule, capsule.width / 2, capsule.height / 2, Qt.LeftButton)
        compare(visualChild(canvas, "editorWaveformInteraction").mode, "handoff")
        keyClick(Qt.Key_Escape)
        mouseRelease(capsule, capsule.width / 2, capsule.height / 2, Qt.LeftButton)
        compare(AudioEditorController.selectionStart, start)
        compare(AudioEditorController.selectionEnd, end)
        compare(AudioEditorController.timelineEventViews.length, 3)
        mouseClick(canvas, canvas.width * 0.4, y, Qt.RightButton)
        verify(AudioEditorController.selectionEnd <= AudioEditorController.selectionStart)
        verify(!findChild(host, "editorClipContextMenu").visible)
    }
    function test_envelopePointWinsHitTestAndUndoIsAtomic() {
        importPcm(1)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null })
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        const original = AudioEditorController.timelineEventViews[0]
        const offset = Math.floor((Number(original.timelineEnd) - Number(original.timelineStart)) / 3)
        verify(AudioEditorController.addEnvelopePoint(String(original.id), offset, 1))
        waitForRendering(canvas)
        const x = canvas.pixelAtFrame(Number(original.timelineStart) + offset)
        const y = canvas.gainY(1, 0)
        mousePress(canvas, x, y, Qt.LeftButton)
        mouseMove(canvas, x + 12, y - 7, 20)
        mouseMove(canvas, x + 24, y - 14, 20)
        mouseRelease(canvas, x + 24, y - 14, Qt.LeftButton)
        const moved = AudioEditorController.timelineEventViews[0]
        compare(moved.timelineStart, original.timelineStart)
        compare(moved.trackIndex, 0)
        compare(moved.gain, 1)
        verify(moved.envelope[0].offset !== offset)
        verify(moved.envelope[0].gain > 1)
        verify(AudioEditorController.triggerAction("editor.undo"))
        const restored = AudioEditorController.timelineEventViews[0]
        compare(restored.envelope.length, 1)
        compare(restored.envelope[0].offset, offset)
        compare(restored.envelope[0].gain, 1)
    }
    function test_sharedSplitBoundaryTrimsOnlySelectedClipLeavingGap() {
        importPcm(1)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null })
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        const first = AudioEditorController.timelineEventViews[0]
        const split = Math.floor(Number(first.timelineEnd) / 2)
        verify(AudioEditorController.splitEvent(String(first.id), split))
        waitForRendering(canvas)
        const boundary = canvas.pixelAtFrame(split)
        mousePress(canvas, boundary, canvas.rowHeight - 12, Qt.LeftButton)
        mouseMove(canvas, boundary + 30, canvas.rowHeight - 12, 30)
        mouseRelease(canvas, boundary + 30, canvas.rowHeight - 12, Qt.LeftButton)
        const clips = AudioEditorController.timelineEventViews
        compare(clips.length, 2)
        compare(clips[0].timelineEnd, split)
        verify(clips[1].timelineStart > split)
        compare(clips[0].sourceEnd, split)
        verify(AudioEditorController.triggerAction("editor.undo"))
        compare(AudioEditorController.timelineEventViews[0].timelineEnd, split)
        compare(AudioEditorController.timelineEventViews[1].timelineStart, split)
        AudioEditorController.selectEvent(String(AudioEditorController.timelineEventViews[0].id))
        mousePress(canvas, boundary, canvas.rowHeight - 12, Qt.LeftButton)
        mouseMove(canvas, boundary - 30, canvas.rowHeight - 12, 30)
        mouseRelease(canvas, boundary - 30, canvas.rowHeight - 12, Qt.LeftButton)
        verify(AudioEditorController.timelineEventViews[0].timelineEnd < split)
        compare(AudioEditorController.timelineEventViews[1].timelineStart, split)
        compare(AudioEditorController.timelineEventViews[1].sourceStart, split)
    }
    function test_leftBodyAtPlayheadMovesClipInsteadOfSelecting() {
        importPcm(1)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null })
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        verify(AudioEditorController.seekFrame(Math.floor(AudioEditorController.totalFrames / 4)))
        waitForRendering(canvas)
        const x = canvas.pixelAtFrame(AudioEditorController.playheadFrame)
        const clip = AudioEditorController.timelineEventViews[0]
        mousePress(canvas, x + 2, canvas.rowHeight - 16, Qt.LeftButton)
        mouseMove(canvas, x + 40, canvas.rowHeight - 16, 30)
        mouseRelease(canvas, x + 40, canvas.rowHeight - 16, Qt.LeftButton)
        verify(AudioEditorController.timelineEventViews[0].timelineStart > clip.timelineStart)
        compare(AudioEditorController.playing, false)
        verify(AudioEditorController.selectionEnd <= AudioEditorController.selectionStart)
        verify(AudioEditorController.undo())
        compare(AudioEditorController.timelineEventViews[0].timelineStart, clip.timelineStart)
    }
    function test_blankTrackClickClearsPreviouslySelectedClip() {
        importPcm(1)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null })
        const canvas = visualChild(host.contentItem, "editorWaveformCanvas")
        AudioEditorController.selectEvent(String(AudioEditorController.timelineEventViews[0].id))
        mouseClick(canvas, canvas.width / 2, canvas.rowHeight * 2 + canvas.rowHeight - 16)
        compare(AudioEditorController.selectedEventId, "")
        verify(!AudioEditorController.triggerAction("editor.deleteSelection"))
        compare(AudioEditorController.timelineEventViews.length, 1)
    }
    function test_rulerEscapeCancelsAndReleaseDoesNotCommit() {
        importPcm(1)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null })
        const ruler = visualChild(host.contentItem, "editorRulerSelectionInteraction")
        const original = Math.floor(AudioEditorController.totalFrames / 5)
        verify(AudioEditorController.seekFrame(original))
        mousePress(ruler, ruler.width / 2, 20, Qt.LeftButton)
        mouseMove(ruler, ruler.width * 0.7, 20, 20)
        keyClick(Qt.Key_Escape)
        compare(AudioEditorController.playheadFrame, original)
        mouseRelease(ruler, ruler.width * 0.7, 20, Qt.LeftButton)
        compare(AudioEditorController.playheadFrame, original)
    }
    function test_busyImportHasWorkingCancelControl() {
        if (typeof testAudioUrl === "undefined" || !testAudioUrl.toString().length) skip("No PCM fixture")
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null })
        const cancel = visualChild(host.contentItem, "editorCancelOperation")
        verify(cancel)
        verify(!cancel.visible)
        verify(AudioEditorController.addFiles([testAudioUrl, testAudioUrl, testAudioUrl, testAudioUrl, testAudioUrl, testAudioUrl]))
        verify(AudioEditorController.busy)
        verify(cancel.visible)
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        tryVerify(function() { return !AudioEditorController.busy }, 10000)
        verify(!cancel.visible)
    }
    function test_offlineSourceHasUsableRelinkEntry() {
        if (typeof testAudioUrl === "undefined" || !testAudioUrl.toString().length) skip("No PCM fixture")
        verify(AudioEditorController.openProject(Qt.resolvedUrl("fixtures/six-track-offline.agproj")))
        tryVerify(function() { return !AudioEditorController.busy && AudioEditorController.projectIssues.length === 1 }, 10000)
        const host = createEditorHost()
        tryVerify(function() { return visualChild(host.contentItem, "editorOfflineSourceBanner") !== null })
        const page = visualChild(host.contentItem, "audioEditorPage")
        const banner = visualChild(page, "editorOfflineSourceBanner")
        const button = visualChild(page, "editorRelinkSourceButton")
        verify(banner.visible && button.enabled)
        inside(banner, page)
        const dialog = findChild(host, "editorRelinkSourceDialog")
        verify(dialog)
        // This case verifies our relink flow, not the OS file picker. Destroying
        // a native Windows picker immediately from a synthetic test races its
        // shell worker; exercise the real QML dialog and keep native acceptance separate.
        dialog.options = dialog.options | FileDialog.DontUseNativeDialog
        mouseClick(button, button.width / 2, button.height / 2)
        compare(page.pendingRelinkSourceId, "1")
        tryCompare(dialog, "visible", true)
        dialog.close()
        tryCompare(dialog, "visible", false)
        verify(AudioEditorController.relinkProjectSource("1", testAudioUrl))
        tryVerify(function() { return !AudioEditorController.busy && AudioEditorController.projectIssues.length === 0 }, 10000)
        verify(!banner.visible)
        compare(AudioEditorController.timelineEventViews.length, 1)
    }
    function visualChild(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const children = item.children || []
        for (let index = 0; index < children.length; ++index) {
            const found = visualChild(children[index], name)
            if (found) return found
        }
        return null
    }
    function test_sixEmptyTracksAndRecordingControls() {
        const host = createEditorHost()
        verify(host)
        const trackList = AudioEditorController.tracks
        verify(trackList !== undefined, "Controller must expose real six-track state")
        compare(trackList.length, 6)
        tryVerify(function() { return visualChild(host.contentItem, "editorTrackHeader0") !== null }, 3000)
        for (let i = 0; i < 6; ++i) {
            const header = visualChild(host.contentItem, "editorTrackHeader" + i)
            verify(header, "Missing track " + (i + 1))
            compare(header.height, Qt.platform.os === "osx" ? 89 : 82)
        }
        verify(findChild(host, "editorRecordingTransport"))
        verify(findChild(host, "editorInputDevice"))
        verify(findChild(host, "editorRecordButton"))
    }
}
