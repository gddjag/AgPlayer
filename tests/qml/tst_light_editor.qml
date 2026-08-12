import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "LightEditor"
    when: windowShown
    visible: true

    width: 1672
    height: 942

    Component {
        id: editorHostComponent
        Window {
            width: 1672
            height: 942
            visible: true
            property alias editorPage: hostedEditorPage

            LightEditPage {
                id: hostedEditorPage
                anchors.fill: parent
            }
        }
    }

    Component {
        id: toolsWindowComponent
        AudioToolsWindow {
            visible: false
        }
    }

    Component {
        id: formatPageComponent
        FormatConvertPage {
            width: 1672
            height: 942
        }
    }

    Component {
        id: metadataPageComponent
        MetadataEditPage { width: 1672; height: 942 }
    }

    Component {
        id: filenamePageComponent
        FilenameProcessPage { width: 1672; height: 942 }
    }

    property var page
    property var editorHost

    function init() {
        editorHost = createTemporaryObject(editorHostComponent, testCase)
        verify(editorHost)
        page = editorHost.editorPage
        verify(page)
        tryVerify(function() { return editorHost.visible && page.visible }, 1000)
        tryVerify(function() {
            return page.width === editorHost.width
                    && page.height === editorHost.height
        }, 1000, "The editor page must finish its initial window layout before interaction")
    }

    function collectByObjectName(item, name, result) {
        if (!item) return
        if (item.objectName === name) result.push(item)
        const childItems = item.children || []
        for (let i = 0; i < childItems.length; ++i)
            collectByObjectName(childItems[i], name, result)
    }

    function test_newEditorControlsAndSixTrackLanesExist() {
        verify(findChild(page, "targetBpmField"))
        verify(findChild(page, "snapGridBox"))
        verify(findChild(page, "timeSignatureBox"))
        verify(findChild(page, "projectKeyBox"))
        verify(findChild(page, "unifyBpmButton"))
        verify(findChild(page, "alignBpmButton"))
        verify(findChild(page, "keepPitchSwitch"))
        verify(findChild(page, "selectedTrackBpmField"))
        verify(findChild(page, "selectedTrackSpeedField"))
        verify(findChild(page, "clipGainBox"))
        verify(findChild(page, "clipFadeInBox"))
        verify(findChild(page, "clipFadeOutBox"))
        verify(findChild(page, "clipFadeInCurveBox"))
        verify(findChild(page, "clipFadeOutCurveBox"))
        verify(findChild(page, "clipLoopModeBox"))
        verify(findChild(page, "clipTimelineDurationBox"))
        verify(findChild(page, "analyzeTrackButton"))
        verify(findChild(page, "selectedTrackKeepPitchSwitch"))
        verify(findChild(page, "alignTrackSwitch"))
        verify(findChild(page, "clipFileInfoPanel"))
        verify(findChild(page, "relinkClipButton"))
        verify(findChild(page, "clipTimePitchPanel"))
        verify(findChild(page, "clipLoopPanel"))
        verify(findChild(page, "projectLoopStartBox"))
        verify(findChild(page, "projectLoopEndBox"))
        verify(findChild(page, "clipPitchSemitoneBox"))
        verify(findChild(page, "clipFineCentsBox"))
        verify(findChild(page, "clipFormantModeBox"))
        verify(findChild(page, "clipTransientProtectionBox"))
        verify(findChild(page, "clipHighQualitySwitch"))
        verify(findChild(page, "stereoPhaseProtectionUnavailable"))
        verify(findChild(page, "editorDropArea"))
        const outputFormatBox = findChild(page, "outputFormatBox")
        verify(outputFormatBox)
        compare(outputFormatBox.model.length, 6)
        compare(outputFormatBox.model[3], "AAC")
        compare(outputFormatBox.model[4], "M4A")
        compare(outputFormatBox.model[5], "OGG")
        const exportScopeBox = findChild(page, "exportScopeBox")
        verify(exportScopeBox)
        compare(exportScopeBox.model.length, 3)
        verify(findChild(page, "outputSampleRateBox"))
        verify(findChild(page, "outputChannelBox"))
        verify(findChild(page, "exportAudioButton"))
        verify(findChild(page, "cutClipButton"))
        verify(findChild(page, "copyClipButton"))
        verify(findChild(page, "duplicateClipButton"))
        verify(findChild(page, "pasteClipButton"))
        verify(findChild(page, "deleteClipButton"))
        verify(findChild(page, "splitClipButton"))
        verify(findChild(page, "mergeClipButton"))
        verify(findChild(page, "cropClipButton"))
        verify(findChild(page, "lightPreviewButton"))
        verify(findChild(page, "lightPreviewVolume"))
        verify(findChild(page, "rippleEditButton"))
        verify(findChild(page, "timelineViewport"))
        verify(findChild(page, "saveProjectButton"))
        verify(findChild(page, "openProjectButton"))
        verify(findChild(page, "loopToggleButton"))
        verify(findChild(page, "gridToggleButton"))
        const inspectorPanel = findChild(page, "editorInspectorPanel")
        verify(inspectorPanel)
        compare(inspectorPanel.visible, true)

        compare(findChild(page, "cutClipButton").enabled, false)
        compare(findChild(page, "pasteClipButton").enabled, false)
        compare(findChild(page, "clipFormantModeBox").enabled, false)

        const lanes = []
        collectByObjectName(page, "trackLane", lanes)
        compare(lanes.length, 6)
        const clipWaveforms = []
        collectByObjectName(page, "editorClipWaveform", clipWaveforms)
        for (let waveform of clipWaveforms) {
            compare(waveform.visualMode, 0)
            compare(waveform.baseColor.toString(),
                    SettingsController.waveformSolidBaseColor.toString())
            compare(waveform.progressColor.toString(),
                    SettingsController.waveformSolidProgressColor.toString())
        }
    }

    function test_pdfReferenceKeepsSixTrackLanesVisibleAtDefaultHeight() {
        const viewport = findChild(page, "timelineViewport")
        verify(viewport)

        const lanes = []
        collectByObjectName(page, "trackLane", lanes)
        compare(lanes.length, 6)
        verify(lanes[0].height <= 140,
               "Track controls must stay compact enough for the six-track reference layout")

        const sixthLaneBottom = lanes[5].y + lanes[5].height
                                - viewport.contentY
        verify(sixthLaneBottom <= viewport.height,
                "The PDF reference shows at least six complete tracks without scrolling")
    }

    function test_pdfReferenceTrackControlsFitInsideCompactLane() {
        const lanes = []
        const indicators = []
        const mixerRows = []
        collectByObjectName(page, "trackLane", lanes)
        collectByObjectName(page, "trackEnabledIndicator", indicators)
        collectByObjectName(page, "trackMixerRow", mixerRows)
        compare(lanes.length, 6)
        compare(indicators.length, lanes.length)
        compare(mixerRows.length, lanes.length)

        for (let i = 0; i < lanes.length; ++i) {
            const bottom = mixerRows[i].mapToItem(lanes[i], 0,
                                                  mixerRows[i].height).y
            verify(bottom <= lanes[i].height,
                   "Track mixer controls must not be clipped by a compact lane")
        }
    }

    function test_pdfReferenceKeepsExportSettingsCollapsedUntilRequested() {
        const exportSettings = findChild(page, "editorExportSettings")
        const exportButton = findChild(page, "exportAudioButton")
        verify(exportSettings)
        verify(exportButton)
        compare(exportSettings.visible, false)

        mouseClick(exportButton)
        tryCompare(exportSettings, "visible", true)
    }

    function test_pdfReferenceKeepsEveryProjectCommandInsideToolbar() {
        const exportButton = findChild(page, "exportAudioButton")
        const cutButton = findChild(page, "cutClipButton")
        const viewport = findChild(page, "timelineViewport")
        verify(exportButton)
        verify(cutButton)
        verify(viewport)
        tryVerify(function() { return page.width >= 1200 && page.height >= 800 },
                  1000)
        const rightEdge = exportButton.mapToItem(page,
                                                  exportButton.width, 0).x
        verify(rightEdge <= page.width - 8,
                "The export command must remain visible at the reference width: "
                + rightEdge + " > " + (page.width - 8))
        verify(cutButton.mapToItem(page, 0, cutButton.height).y
               < viewport.mapToItem(page, 0, 0).y,
               "Edit commands belong in the top command toolbar, not below the timeline")
    }

    function test_pdfReferenceAdvancedEditCommandsAreReachable() {
        const moreButton = findChild(page, "editorMoreMenuButton")
        const moreMenu = findChild(page, "editorMoreMenu")
        verify(moreButton)
        verify(moreButton.visible)
        verify(moreMenu)

        const commandNames = [
            "rippleEditMenuItem", "autoCrossfadeMenuItem",
            "cutClipMenuItem", "copyClipMenuItem",
            "duplicateClipMenuItem", "pasteClipMenuItem",
            "mergeClipMenuItem", "muteClipMenuItem", "cropClipMenuItem"
        ]
        for (let name of commandNames)
            verify(findChild(page, name), "Missing editor command: " + name)

        mouseClick(moreButton)
        tryCompare(moreMenu, "visible", true)
        moreMenu.close()
    }

    function test_pdfReferenceEditorToolbarAndTransportAreComplete() {
        tryVerify(function() { return page.width >= 1200 && page.height >= 800 },
                  1000)
        const toolbarNames = [
            "addFileButton", "saveProjectButton", "undoProjectButton",
            "redoProjectButton", "splitClipButton", "deleteClipButton",
            "snapToggleButton", "snapGridBox", "loopToggleButton",
            "exportAudioButton", "targetBpmField", "timeSignatureBox",
            "projectKeyBox", "timeDisplayBox", "materialSearchField",
            "inspectorToggleButton"
        ]
        for (let name of toolbarNames) {
            const control = findChild(page, name)
            verify(control, "Missing PDF toolbar control: " + name)
            verify(control.visible, "Hidden PDF toolbar control: " + name)
            const point = control.mapToItem(page, control.width, control.height)
            verify(point.x <= page.width && point.y <= page.height,
                   "Toolbar control is clipped: " + name + " at "
                   + point.x + "," + point.y + " in "
                   + page.width + "x" + page.height)
        }

        const transportNames = [
            "lightPreviewRewindButton", "lightPreviewButton",
            "lightPreviewStopButton", "lightPreviewLoopButton",
            "timelineZoomSlider", "cpuLoadLabel", "masterOutputMeter",
            "lightPreviewVolume"
        ]
        for (let name of transportNames) {
            const control = findChild(page, name)
            verify(control, "Missing PDF transport control: " + name)
            verify(control.visible)
        }

        const outputLabels = []
        collectByObjectName(page, "trackOutputLabel", outputLabels)
        compare(outputLabels.length, 6)
        for (let label of outputLabels)
            compare(label.text, "Master")
    }

    function test_pdfReferenceInspectorKeepsEveryPropertyReachable() {
        const inspector = findChild(page, "editorInspectorScroll")
        const lastControl = findChild(page, "alignTrackSwitch")
        verify(inspector,
               "The right-side property inspector must provide a scroll viewport")
        verify(lastControl)
        compare(inspector.visible, true,
                "The reference layout keeps the property inspector open by default")

        inspector.contentY = Math.max(0, inspector.contentHeight - inspector.height)
        wait(0)
        const center = lastControl.mapToItem(inspector,
                                             lastControl.width / 2,
                                             lastControl.height / 2)
        verify(center.y >= 0 && center.y <= inspector.height,
               "The last track property must remain reachable in the inspector")
    }

    function test_pdfReferenceTimelineAutoFitsLoadedProjectInsteadOfFiveMinutes() {
        LightEditor.clear()
        LightEditor.loadFileToTrack(0, testAudioUrl)
        tryVerify(function() { return LightEditor.clipCount === 1 }, 3000)

        verify(page.projectDurationMs() > 0)
        verify(page.projectDurationMs() < 10000,
               "A short project must not be forced into a five-minute timeline")

        const clipBodies = []
        collectByObjectName(page, "clipBody", clipBodies)
        tryVerify(function() {
            for (let i = 0; i < clipBodies.length; ++i) {
                if (clipBodies[i].visible && clipBodies[i].width >= 160)
                    return true
            }
            return false
        }, 1000)
    }

    function test_wheelZoomEntryPointChangesScale() {
        compare(page.zoomScale, 1.0)
        verify(typeof page.applyWheelZoom === "function")
        page.applyWheelZoom(120, page.width / 2)
        verify(page.zoomScale > 1.0)
    }

    function test_acidStyleViewportWheelModes() {
        const viewport = findChild(page, "timelineViewport")
        verify(viewport)
        compare(page.viewportOffsetMs, 0)
        page.handleTimelineWheel(-120, page.width / 2, Qt.ShiftModifier)
        verify(page.viewportOffsetMs > 0)

        const oldScale = page.zoomScale
        page.handleTimelineWheel(120, page.width / 2, Qt.NoModifier)
        verify(page.zoomScale > oldScale)
    }

    function test_pdfKeyboardShortcutsMatchTheDocument() {
        LightEditor.clear()
        LightEditor.loadFileToTrack(0, testAudioUrl)
        tryCompare(LightEditor, "clipCount", 1, 3000)
        page.forceActiveFocus()
        verify(typeof page.handleEditorShortcut === "function")

        page.handleEditorShortcut(Qt.Key_D, Qt.ControlModifier)
        tryCompare(LightEditor, "clipCount", 2)
        page.handleEditorShortcut(Qt.Key_Z, Qt.ControlModifier)
        tryCompare(LightEditor, "clipCount", 1)
        page.handleEditorShortcut(Qt.Key_Z,
                                  Qt.ControlModifier | Qt.ShiftModifier)
        tryCompare(LightEditor, "clipCount", 2)

        const loopBefore = LightEditor.loopEnabled
        page.handleEditorShortcut(Qt.Key_L, Qt.NoModifier)
        compare(LightEditor.loopEnabled, !loopBefore)

        const zoomBefore = page.zoomScale
        page.handleEditorShortcut(Qt.Key_Plus, Qt.NoModifier)
        verify(page.zoomScale > zoomBefore)
        page.handleEditorShortcut(Qt.Key_Minus, Qt.NoModifier)
        verify(page.zoomScale <= zoomBefore + 0.001)
        LightEditor.clear()
    }

    function test_projectBpmKeepsTwoDecimalPrecisionAndFullGridSet() {
        const bpmField = findChild(page, "targetBpmField")
        const gridBox = findChild(page, "snapGridBox")
        verify(bpmField)
        verify(gridBox)
        bpmField.text = "128.25"
        bpmField.editingFinished()
        compare(LightEditor.targetBpm, 128.25)
        compare(gridBox.count, 8)
        compare(gridBox.textAt(0), "1 小节")
        compare(gridBox.textAt(5), "1/32")
        compare(gridBox.textAt(6), "1/8T")
        compare(gridBox.textAt(7), "1/16T")
    }

    function test_acidStyleRulerCreatesVisibleLoopSelection() {
        const rulerInteraction = findChild(page, "timelineRulerInteraction")
        const loopOverlay = findChild(page, "timelineLoopOverlay")
        verify(rulerInteraction)
        verify(loopOverlay)

        LightEditor.loopEnabled = false
        LightEditor.loopStartMs = 0
        LightEditor.loopEndMs = 0
        mousePress(rulerInteraction, 80, rulerInteraction.height / 2,
                   Qt.LeftButton)
        mouseMove(rulerInteraction, 260, rulerInteraction.height / 2, 20)
        mouseRelease(rulerInteraction, 260, rulerInteraction.height / 2,
                     Qt.LeftButton)
        tryCompare(LightEditor, "loopEnabled", true)
        verify(LightEditor.loopEndMs > LightEditor.loopStartMs)
        verify(loopOverlay.visible)
        verify(loopOverlay.width > 0)
    }

    function test_acidStyleCtrlAndShiftTrackSelection() {
        page.selectTrack(1, Qt.NoModifier)
        compare(page.selectedTrackIndexes.length, 1)
        compare(page.selectedTrackIndexes[0], 1)

        page.selectTrack(3, Qt.ControlModifier)
        compare(page.selectedTrackIndexes.length, 2)
        verify(page.selectedTrackIndexes.indexOf(1) >= 0)
        verify(page.selectedTrackIndexes.indexOf(3) >= 0)

        page.selectTrack(5, Qt.ShiftModifier)
        compare(page.selectedTrackIndexes.length, 3)
        compare(page.selectedTrackIndexes[0], 3)
        compare(page.selectedTrackIndexes[2], 5)
    }

    function test_acidStyleCtrlAndShiftClipSelection() {
        LightEditor.clear()
        LightEditor.loadFileToTrack(0, testAudioUrl)
        LightEditor.loadFileToTrack(1, testAudioUrl)
        LightEditor.loadFileToTrack(2, testAudioUrl)
        tryCompare(LightEditor, "clipCount", 3, 3000)

        const first = LightEditor.tracks[0].clips[0].clipId
        const second = LightEditor.tracks[1].clips[0].clipId
        const third = LightEditor.tracks[2].clips[0].clipId
        verify(typeof page.selectClip === "function")
        page.selectClip(first, Qt.NoModifier)
        compare(LightEditor.selectedClipIds.length, 1)
        page.selectClip(third, Qt.ControlModifier)
        compare(LightEditor.selectedClipIds.length, 2)
        page.selectClip(second, Qt.ShiftModifier)
        compare(LightEditor.selectedClipIds.length, 2)
        compare(LightEditor.selectedClipIds[0], second)
        compare(LightEditor.selectedClipIds[1], third)
        LightEditor.clear()
    }

    function test_acidStyleMarqueeSelectionUsesTimelineRange() {
        LightEditor.clear()
        LightEditor.snapEnabled = false
        LightEditor.loadFileToTrack(0, testAudioUrl)
        LightEditor.loadFileToTrack(1, testAudioUrl)
        LightEditor.loadFileToTrack(2, testAudioUrl)
        tryCompare(LightEditor, "clipCount", 3, 3000)
        verify(LightEditor.moveClip(1, 3000))
        verify(LightEditor.moveClip(2, 6000))
        verify(findChild(page, "timelineBoxSelectionArea") !== null)
        verify(findChild(page, "timelineMarqueeRect") !== null)
        compare(LightEditor.selectClipsInRange(2200, 4500, 1, 1, false), 1)
        compare(LightEditor.selectedClipIds.length, 1)
        compare(LightEditor.selectClipsInRange(5500, 6500, 2, 2, true), 1)
        compare(LightEditor.selectedClipIds.length, 2)
        LightEditor.clear()
    }

    function test_acidStyleClipMovesAndTrimsWithRealMouse() {
        LightEditor.clear()
        LightEditor.loadFiles([testAudioUrl])
        tryVerify(function() { return LightEditor.clipCount > 0 }, 3000)

        const clips = []
        collectByObjectName(page, "clipBody", clips)
        tryVerify(function() { return clips.length > 0 }, 1000)
        const clip = clips[clips.length - 1]
        const moveHandler = findChild(clip, "clipMoveHandler")
        verify(moveHandler && moveHandler.enabled)
        const firstTrack = LightEditor.tracks[0]
        const clipId = firstTrack.clips[0].clipId
        const startBefore = firstTrack.clips[0].timelineStartMs
        verify(nativeDropHelper.dragItem(clip, clip.width / 2,
                                         clip.height / 2, 30, 0))
        tryVerify(function() {
            const track = LightEditor.tracks[0]
            return track.clips[0].clipId === clipId
                    && track.clips[0].timelineStartMs > startBefore
        }, 1000, "The clip must move through the real pointer path; start="
           + startBefore + ", current="
           + LightEditor.tracks[0].clips[0].timelineStartMs)

        const movedClips = []
        collectByObjectName(page, "clipBody", movedClips)
        tryVerify(function() { return movedClips.length > 0 }, 1000)
        const movedClip = movedClips[movedClips.length - 1]
        const lanes = []
        collectByObjectName(page, "trackLane", lanes)
        verify(lanes.length >= 2)
        verify(nativeDropHelper.dragItem(movedClip, movedClip.width / 2,
                                         movedClip.height / 2, 0,
                                         lanes[0].height + 6))
        tryVerify(function() {
            return LightEditor.tracks[0].clips.length === 0
                    && LightEditor.tracks[1].clips.length === 1
                    && LightEditor.tracks[1].clips[0].clipId === clipId
        }, 1000)

        LightEditor.clear()
        LightEditor.loadFiles([testAudioUrl])
        tryVerify(function() { return LightEditor.clipCount > 0 }, 3000)
        page.zoomScale = 16
        wait(30)
        const zoomedClips = []
        collectByObjectName(page, "clipBody", zoomedClips)
        tryVerify(function() { return zoomedClips.length > 0 }, 1000)
        const zoomedClip = zoomedClips[zoomedClips.length - 1]
        const leftTrim = findChild(zoomedClip, "clipTrimLeftHandle")
        verify(leftTrim && leftTrim.visible)
        const inBefore = LightEditor.tracks[0].clips[0].inMs
        verify(nativeDropHelper.dragItem(zoomedClip, 2,
                                         zoomedClip.height / 2, 30, 0))
        tryVerify(function() {
            return LightEditor.tracks[0].clips[0].inMs > inBefore
        }, 1000)
        LightEditor.clear()
    }

    function test_acidStyleAltDragDuplicatesClip() {
        LightEditor.clear()
        LightEditor.snapEnabled = false
        LightEditor.loadFileToTrack(0, testAudioUrl)
        tryCompare(LightEditor, "clipCount", 1, 3000)
        page.zoomScale = 16
        wait(30)

        const clipBodies = []
        collectByObjectName(page, "clipBody", clipBodies)
        tryVerify(function() { return clipBodies.length > 0 })
        const clipBody = clipBodies[clipBodies.length - 1]
        const moveHandler = findChild(clipBody, "clipMoveHandler")
        verify(moveHandler)
        const originalId = LightEditor.tracks[0].clips[0].clipId
        const originalStart = LightEditor.tracks[0].clips[0].timelineStartMs

        verify(nativeDropHelper.dragItemWithModifiers(
                   moveHandler, Math.min(100, clipBody.width / 2),
                   clipBody.height / 2,
                   90, 0, Qt.AltModifier))
        tryCompare(LightEditor, "clipCount", 2)
        const clips = LightEditor.tracks[0].clips
        verify(clips.some(function(clip) {
            return clip.clipId === originalId
                    && clip.timelineStartMs === originalStart
        }))
        verify(clips.some(function(clip) {
            return clip.clipId !== originalId
                    && clip.timelineStartMs > originalStart
        }))
        LightEditor.clear()
    }

    function test_spacePreviewsTheWholeMultitrackProject() {
        LightEditor.clear()
        LightEditor.loadFileToTrack(0, testAudioUrl)
        LightEditor.loadFileToTrack(1, testAudioUrl)
        tryCompare(LightEditor, "clipCount", 2, 3000)
        page.forceActiveFocus()
        page.togglePreview()
        tryVerify(function() {
            return AudioPreviewController.isTimelinePreview()
                    && AudioPreviewController.playing
        }, 3000)
        compare(AudioPreviewController.sourcePath,
                "agplayer://timeline-preview")
        verify(AudioPreviewController.durationMs > 0)
        page.togglePreview()
        LightEditor.clear()
    }

    function test_pdfReferenceTransportExposesRewindStopAndLoop() {
        verify(findChild(page, "lightPreviewRewindButton"))
        verify(findChild(page, "lightPreviewStopButton"))
        const loopButton = findChild(page, "lightPreviewLoopButton")
        verify(loopButton)
        compare(loopButton.checkable, true)
    }

    function test_exportSampleRatesCoverProfessionalOptions() {
        const sampleRates = findChild(page, "outputSampleRateBox")
        verify(sampleRates)
        compare(sampleRates.count, 6)
        compare(sampleRates.textAt(0), "44,100 Hz")
        compare(sampleRates.textAt(5), "192,000 Hz")
    }

    function test_audioToolsWindowUsesReferenceSize() {
        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        compare(toolsWindow.width, 1672)
        compare(toolsWindow.height, 942)
        compare(AudioToolsController.currentTool >= 0
                && AudioToolsController.currentTool < 4, true)
    }

    function test_audioToolsWindowUsesNativeMoveSurface() {
        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        const moveArea = findChild(toolsWindow, "audioToolsMoveArea")
        verify(moveArea)
        compare(moveArea.acceptedButtons, Qt.LeftButton)
    }

    function test_audioToolsWindowMovesWithRealMouseDrag() {
        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        toolsWindow.visible = true
        wait(80)
        const moveArea = findChild(toolsWindow, "audioToolsMoveArea")
        verify(moveArea)
        const before = Qt.point(toolsWindow.x, toolsWindow.y)
        verify(nativeDropHelper.dragItem(moveArea, 160, moveArea.height / 2,
                                         64, 24))
        tryVerify(function() {
            return toolsWindow.x !== before.x || toolsWindow.y !== before.y
        }, 1000, "The tools title bar must move the native window")
        toolsWindow.destroy()
    }

    function test_audioToolsUseHorizontalTopNavigation() {
        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        toolsWindow.visible = true
        wait(50)
        compare(toolsWindow.width, 1672)
        compare(toolsWindow.height, 942)
        verify(Math.abs(toolsWindow.width / toolsWindow.height - 16 / 9) < 0.01,
               "The PDF reference uses a compact 16:9 desktop workspace")
        const titleBar = findChild(toolsWindow, "audioToolsTitleBar")
        verify(titleBar)
        verify(titleBar.height <= 42,
               "The title bar must remain compact like the PDF reference")
        const topNav = findChild(toolsWindow, "audioToolsTopNav")
        verify(topNav)
        verify(topNav.width > toolsWindow.width * 0.6)
        verify(topNav.height <= 40)
        const buttons = []
        collectByObjectName(topNav, "audioToolNavButton", buttons)
        compare(buttons.length, 4)
        let occupiedWidth = 0
        for (let button of buttons) {
            verify(button.width >= 104 && button.width <= 156,
                   "Top tool tabs must be compact, not four full-width cards")
            verify(button.height <= 38)
            occupiedWidth += button.width
        }
        verify(occupiedWidth <= 624,
               "The four module tabs must stay grouped at the leading edge")
        compare(topNav.toolNames.join("|"),
                "轻度剪辑|格式转换|元数据修改|文件名处理")
    }

    function test_formatConverterOptionsUseIndependentDefaults() {
        SettingsController.preserveMetadata = false
        const formatPage = createTemporaryObject(formatPageComponent, testCase)
        verify(formatPage)
        const keepMetadata = findChild(formatPage, "keepMetadataCheck")
        const extractAudio = findChild(formatPage, "extractAudioCheck")
        verify(keepMetadata)
        verify(extractAudio)
        compare(keepMetadata.checked, false)
        compare(extractAudio.checked, false)
        SettingsController.preserveMetadata = true
        tryCompare(keepMetadata, "checked", true)
        compare(extractAudio.checked, false)
    }

    function test_formatConverterCanRunSelectedOrAllTasks() {
        const formatPage = createTemporaryObject(formatPageComponent, testCase)
        verify(formatPage)
        verify(findChild(formatPage, "convertSelectedButton"))
        verify(findChild(formatPage, "convertAllButton"))
        verify(findChild(formatPage, "retryFailedButton"))
        const formatBox = findChild(formatPage, "converterOutputFormatBox")
        verify(formatBox)
        compare(formatBox.count, FormatConverter.supportedOutputFormats.length)
    }

    function test_pdfReferenceConverterUsesTaskListInspectorAndBottomBar() {
        const formatPage = createTemporaryObject(formatPageComponent, testCase)
        verify(formatPage)
        const taskPanel = findChild(formatPage, "formatTaskPanel")
        const settingsPanel = findChild(formatPage, "formatSettingsPanel")
        const bottomBar = findChild(formatPage, "formatBottomBar")
        verify(taskPanel)
        verify(settingsPanel)
        verify(bottomBar)
        const parallelJobs = findChild(formatPage,
                                       "converterParallelJobsBox")
        verify(parallelJobs)
        compare(parallelJobs.count, 3)

        const taskTop = taskPanel.mapToItem(formatPage, 0, 0)
        const settingsTop = settingsPanel.mapToItem(formatPage, 0, 0)
        const bottomTop = bottomBar.mapToItem(formatPage, 0, 0)
        verify(taskTop.x < settingsTop.x)
        compare(Math.round(taskTop.y), Math.round(settingsTop.y))
        verify(bottomTop.y > taskTop.y + taskPanel.height - 2)
        verify(settingsPanel.width >= 280 && settingsPanel.width <= 340)
    }

    function test_formatConverterMatchesReferenceWorkbenchZones() {
        const formatPage = createTemporaryObject(formatPageComponent, testCase)
        verify(formatPage)
        const required = [
            "formatToolbar", "formatSearchField", "formatStatusFilters",
            "formatTaskPanel", "formatSettingsPanel", "formatBottomBar",
            "formatOutputFormatGroup", "formatEncodingSettingsGroup",
            "formatOutputOptionsGroup", "formatTotalProgress"
        ]
        for (let name of required)
            verify(findChild(formatPage, name),
                   "Missing reference conversion control: " + name)
        formatPage.destroy()
    }

    function test_pdfReferenceMetadataUsesFileListInspectorAndBottomBar() {
        const metadataPage = createTemporaryObject(metadataPageComponent, testCase)
        verify(metadataPage)
        const filePanel = findChild(metadataPage, "metadataFilePanel")
        const inspectorPanel = findChild(metadataPage, "metadataInspectorPanel")
        const bottomBar = findChild(metadataPage, "metadataBottomBar")
        verify(filePanel)
        verify(inspectorPanel)
        verify(bottomBar)

        const fileTop = filePanel.mapToItem(metadataPage, 0, 0)
        const inspectorTop = inspectorPanel.mapToItem(metadataPage, 0, 0)
        const bottomTop = bottomBar.mapToItem(metadataPage, 0, 0)
        verify(fileTop.x < inspectorTop.x)
        compare(Math.round(fileTop.y), Math.round(inspectorTop.y))
        verify(bottomTop.y > fileTop.y + filePanel.height - 2)
        verify(inspectorPanel.width >= 360 && inspectorPanel.width <= 430)

        const modeButtons = []
        collectByObjectName(inspectorPanel, "metadataModeButton", modeButtons)
        compare(modeButtons.length, 27)
        verify(findChild(inspectorPanel, "metadataCoverSection"))
        verify(findChild(inspectorPanel, "metadataChangePreview"))
        verify(findChild(inspectorPanel, "metadataScopeBox"))
        verify(findChild(inspectorPanel, "metadataProcessingModeBox"))
        verify(findChild(metadataPage, "metadataSearchField"))
        verify(findChild(metadataPage, "metadataPreflightButton"))
        verify(findChild(metadataPage, "metadataExportResultsButton"))
        verify(findChild(metadataPage, "metadataApplyButton"))
    }

    function test_pdfReferenceFilenameUsesFilesRulesPreviewAndBottomBar() {
        const filenamePage = createTemporaryObject(filenamePageComponent, testCase)
        verify(filenamePage)
        const filePanel = findChild(filenamePage, "filenameFilePanel")
        const rulesPanel = findChild(filenamePage, "filenameRulesPanel")
        const previewPanel = findChild(filenamePage, "filenamePreviewPanel")
        const bottomBar = findChild(filenamePage, "filenameBottomBar")
        verify(filePanel)
        verify(rulesPanel)
        verify(previewPanel)
        verify(bottomBar)

        const fileTop = filePanel.mapToItem(filenamePage, 0, 0)
        const rulesTop = rulesPanel.mapToItem(filenamePage, 0, 0)
        const previewTop = previewPanel.mapToItem(filenamePage, 0, 0)
        const bottomTop = bottomBar.mapToItem(filenamePage, 0, 0)
        verify(fileTop.x < rulesTop.x)
        verify(previewTop.x >= rulesTop.x - 1)
        verify(previewTop.y > rulesTop.y + rulesPanel.height - 2)
        verify(bottomTop.y > fileTop.y + filePanel.height - 2)
    }

    function test_pdfReferenceFilenameRuleInputsRemainReadable() {
        const filenamePage = createTemporaryObject(filenamePageComponent, testCase)
        verify(filenamePage)
        const caseBox = findChild(filenamePage, "filenameCaseBox")
        const conflictBox = findChild(filenamePage, "filenameConflictBox")
        verify(caseBox)
        verify(conflictBox)
        verify(caseBox.width >= 112,
               "The case rule must show the complete selected value")
        verify(conflictBox.width >= 180,
               "The conflict policy must remain readable")
    }

    function test_keepPitchSettingUpdatesOpenToolPages() {
        SettingsController.keepPitchWhileSpeedChange = false
        tryCompare(LightEditor, "keepPitch", false)

        SettingsController.keepPitchWhileSpeedChange = true
        tryCompare(LightEditor, "keepPitch", true)
    }

    function test_allToolPagesExposeKeyboardDeleteSelection() {
        const formatPage = createTemporaryObject(formatPageComponent, testCase)
        const metadataPage = createTemporaryObject(metadataPageComponent, testCase)
        const filenamePage = createTemporaryObject(filenamePageComponent, testCase)
        verify(formatPage)
        verify(metadataPage)
        verify(filenamePage)
        verify(typeof formatPage.deleteSelection === "function")
        verify(typeof metadataPage.deleteSelection === "function")
        verify(typeof filenamePage.deleteSelection === "function")
        const coverMode = findChild(metadataPage, "coverModeBox")
        verify(coverMode)
        compare(coverMode.currentValue, "keep")
        coverMode.currentIndex = 2
        compare(coverMode.currentValue, "clear")
    }

    function test_qmlDropFallbackReachesEveryToolController() {
        LightEditor.clear()
        FormatConverter.clear()
        MetadataEditor.clear()
        FilenameProcessor.clear()

        const formatPage = createTemporaryObject(formatPageComponent, testCase)
        const metadataPage = createTemporaryObject(metadataPageComponent, testCase)
        const filenamePage = createTemporaryObject(filenamePageComponent, testCase)
        verify(formatPage && metadataPage && filenamePage)

        const formatDrop = findChild(formatPage, "formatDropArea")
        const metadataDrop = findChild(metadataPage, "metadataDropArea")
        const filenameDrop = findChild(filenamePage, "filenameDropArea")
        const editorDrop = findChild(page, "editorDropArea")
        verify(formatDrop && metadataDrop && filenameDrop && editorDrop)

        verify(editorDrop.submitUrls([testAudioUrl]))
        tryVerify(function() { return LightEditor.clipCount > 0 }, 3000)
        verify(formatDrop.submitUrls([testAudioUrl]))
        tryCompare(FormatConverter, "fileCount", 1)
        verify(metadataDrop.submitUrls([testAudioUrl]))
        tryCompare(MetadataEditor, "fileCount", 1)
        verify(filenameDrop.submitUrls([testAudioUrl]))
        tryCompare(FilenameProcessor, "fileCount", 1)

        formatPage.destroy()
        metadataPage.destroy()
        filenamePage.destroy()
        LightEditor.clear()
        FormatConverter.clear()
        MetadataEditor.clear()
        FilenameProcessor.clear()
    }

    function test_nativeDropEventsReachEveryAudioTool() {
        LightEditor.clear()
        FormatConverter.clear()
        MetadataEditor.clear()
        FilenameProcessor.clear()

        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        toolsWindow.visible = true

        AudioToolsController.currentTool = 0
        wait(80)
        verify(nativeDropHelper.sendUrls(toolsWindow,
                                         [testAudioUrl]), "editor native drop")
        tryVerify(function() { return LightEditor.clipCount > 0 })

        AudioToolsController.currentTool = 1
        wait(80)
        verify(nativeDropHelper.sendUrls(toolsWindow,
                                         [testAudioUrl]), "format native drop")
        tryCompare(FormatConverter, "fileCount", 1)

        AudioToolsController.currentTool = 2
        wait(80)
        verify(nativeDropHelper.sendUrls(toolsWindow,
                                         [testAudioUrl]), "metadata native drop")
        tryCompare(MetadataEditor, "fileCount", 1)

        AudioToolsController.currentTool = 3
        wait(80)
        verify(nativeDropHelper.sendUrls(toolsWindow,
                                         [testAudioUrl]), "filename native drop")
        tryCompare(FilenameProcessor, "fileCount", 1)
        toolsWindow.destroy()
        LightEditor.clear()
        FormatConverter.clear()
        MetadataEditor.clear()
        FilenameProcessor.clear()
    }

    function test_toolsWindowTitleBarMovesFromRealPointerDrag() {
        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        toolsWindow.visible = true
        tryVerify(function() { return toolsWindow.visible }, 1000)

        const moveArea = findChild(toolsWindow, "audioToolsMoveArea")
        verify(moveArea)
        const startX = toolsWindow.x
        const startY = toolsWindow.y
        mousePress(moveArea, moveArea.width / 2, moveArea.height / 2,
                   Qt.LeftButton)
        mouseMove(moveArea, moveArea.width / 2 + 48,
                  moveArea.height / 2 + 36, 80, Qt.LeftButton)
        mouseRelease(moveArea, moveArea.width / 2 + 48,
                     moveArea.height / 2 + 36, Qt.LeftButton)
        tryVerify(function() {
            return toolsWindow.x !== startX || toolsWindow.y !== startY
        }, 1000, "the frameless tool shell must remain draggable")
        toolsWindow.destroy()
    }
}
