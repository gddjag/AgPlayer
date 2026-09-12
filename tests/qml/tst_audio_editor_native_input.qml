import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "AudioEditorNativeInput"
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
        id: mouseModifierObserverComponent
        Item {
            id: observer
            width: 0
            height: 0
            visible: false
            property var target
            property int observedModifiers: Qt.NoModifier

            Connections {
                target: observer.target
                function onPressed(event) {
                    observer.observedModifiers = event.modifiers
                }
            }
        }
    }

    property var host
    property var page

    function destroyCurrentHost() {
        if (!host) return
        const closingHost = host
        host = null
        page = null
        verify(nativeDropHelper.destroyItem(closingHost),
               "native editor shell did not finish deterministic cleanup")
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

    function findTimelineEventItem(root, eventId) {
        if (!root || !root.children)
            return null
        for (let index = root.children.length - 1; index >= 0; --index) {
            const child = root.children[index]
            if (child.objectName === "editorEventVisualBoundary"
                    && child.modelData
                    && String(child.modelData.id) === String(eventId)
                    && child.visible) {
                return child
            }
            const nested = findTimelineEventItem(child, eventId)
            if (nested)
                return nested
        }
        return null
    }

    function eventViewById(eventId, events) {
        const source = events === undefined
            ? AudioEditorController.timelineEventViews : events
        return source.filter(function(event) {
            return String(event.id) === String(eventId)
        })[0]
    }

    function init() {
        AudioToolsController.selectTool(0)
        if (AudioEditorController.hasDocument && !AudioEditorController.busy) {
            if (!AudioEditorController.clearDocument())
                verify(AudioEditorController.confirmDiscardAndOpen())
        }
        host = createTemporaryObject(pageComponent, testCase)
        verify(host)
        page = host.editorPage
        verify(nativeDropHelper.prepareItem(host),
               "native editor shell was not visible, exposed, and active")
        tryVerify(function() { return page.width > 0 && page.height > 0 })
    }

    function cleanup() {
        if (AudioEditorController.busy) {
            AudioEditorController.cancelOperation()
            tryVerify(function() { return !AudioEditorController.busy }, 5000)
        }
        if (AudioEditorController.hasDocument) {
            AudioEditorController.stopPlayback()
            if (!AudioEditorController.clearDocument())
                verify(AudioEditorController.confirmDiscardAndOpen())
        }
        if (host) {
            destroyCurrentHost()
        }
    }

    function test_editorDropAreaImportsExactlyOneAudioAndRejectsMultipleUrls() {
        const dropArea = findChild(page, "editorAudioDropArea")
        verify(dropArea && testAudioUrl && testAudioUrl.toString().length > 0)
        verify(nativeDropHelper.sendUrls(page, [testAudioUrl]))
        tryVerify(function() { return AudioEditorController.hasDocument })
        verify(AudioEditorController.clearDocument())
        const secondAudioUrl = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(secondAudioUrl && secondAudioUrl.toString().length > 0)
        verify(nativeDropHelper.sendUrls(page, [testAudioUrl, secondAudioUrl]))
        tryVerify(function() {
            return AudioEditorController.errorMessage.length > 0
        })
    }

    function test_spaceStartsRealFixturePlaybackFromCodecFocus() {
        destroyCurrentHost()
        host = createTemporaryObject(shellComponent, testCase)
        verify(host)
        verify(nativeDropHelper.prepareItem(host),
               "full shell was not visible, exposed, and active")
        page = findChild(host, "audioEditorPage")
        const shortcut = findChild(host, "audioToolsSpaceShortcut")
        const codec = findChild(page, "editorExportCodec")
        verify(page && shortcut && codec)
        verify(testAudioUrl && testAudioUrl.toString().length > 0)
        verify(AudioEditorController.openFile(testAudioUrl))
        tryVerify(function() {
            return !AudioEditorController.busy
                && AudioEditorController.hasDocument
        }, 5000)

        codec.forceActiveFocus()
        tryVerify(function() { return codec.activeFocus })
        verify(shortcut.enabled)
        verify(nativeDropHelper.keyClickItem(
            codec, Qt.Key_Space, Qt.NoModifier))
        tryVerify(function() { return AudioEditorController.playing }, 5000,
            AudioEditorController.errorMessage)
        verify(nativeDropHelper.keyClickItem(
            codec, Qt.Key_Space, Qt.NoModifier))
        tryCompare(AudioEditorController, "playing", false)

        verify(AudioEditorController.setPitch(2, 0))
        const reset = findChild(page, "inspectorSpeedResetButton")
        verify(reset)
        compare(reset.focusPolicy, Qt.TabFocus)
        reset.forceActiveFocus()
        tryVerify(function() { return reset.activeFocus })
        verify(nativeDropHelper.keyClickItem(
            reset, Qt.Key_Space, Qt.NoModifier))
        tryVerify(function() { return AudioEditorController.playing }, 5000,
            AudioEditorController.errorMessage)
        compare(AudioEditorController.pitchCents, 200)
        verify(nativeDropHelper.keyClickItem(
            reset, Qt.Key_Space, Qt.NoModifier))
        tryCompare(AudioEditorController, "playing", false)
        compare(AudioEditorController.pitchCents, 200)
    }

    function test_narrowShellFixtureKeepsRealPlaybackControlsInsideThePage() {
        destroyCurrentHost()
        host = createTemporaryObject(shellComponent, testCase)
        verify(host)
        host.width = 880
        host.height = 560
        verify(nativeDropHelper.prepareItem(host),
               "880 shell was not visible, exposed, and active")
        page = findChild(host, "audioEditorPage")
        verify(page && testAudioUrl && testAudioUrl.toString().length > 0)
        verify(page.height >= 441 && page.height <= host.height,
               "compact title/navigation must leave the editor its full usable page")

        verify(nativeDropHelper.sendUrls(page, [testAudioUrl]))
        tryVerify(function() {
            return AudioEditorController.hasDocument
                && !AudioEditorController.busy
        }, 5000, "880 fixture did not finish importing: "
                 + AudioEditorController.errorMessage)
        const transport = findChild(page, "editorPlaybackTransport")
        const narrowPlay = findChild(page, "editorNarrowPlaybackAccess")
        const primaryPlay = findChild(page, "editorPrimaryPlayButton")
        const selectButton = findChild(page, "editorCommand_select")
        verify(transport && narrowPlay && primaryPlay && selectButton)
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        verify(canvas.height >= 48,
               "880 shell must retain enough native pointer-edit height")
        const transportPosition = transport.mapToItem(page, 0, 0)
        const primaryPosition = primaryPlay.mapToItem(page, 0, 0)
        const narrowPosition = narrowPlay.mapToItem(page, 0, 0)
        verify(transportPosition.y >= 0
               && transportPosition.y + transport.height <= page.height)
        verify(primaryPosition.y >= 0
               && primaryPosition.y + primaryPlay.height <= page.height)
        verify(narrowPosition.y >= 0
               && narrowPosition.y + narrowPlay.height <= page.height)
        tryVerify(function() {
            return narrowPlay.enabled && primaryPlay.enabled
        }, 5000, "880 playback controls did not become enabled")
        verify(nativeDropHelper.clickItem(selectButton,
            selectButton.width * 0.5, selectButton.height * 0.5,
            Qt.LeftButton))
        tryCompare(AudioEditorController, "activeTool", "select")

        // The narrow shell deliberately leaves a compact timeline, but its
        // real native pointer targets must remain usable.
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(
            0, AudioEditorController.totalFrames))
        // The compact event header and the six-pixel gain hit target occupy
        // the upper/middle bands. Exercise the remaining waveform body band,
        // which is the range-selection surface at 880x560.
        verify(nativeDropHelper.dragItem(canvas, canvas.width * 0.18,
            canvas.height - 3, canvas.width * 0.22, 0),
            "880 native range drag did not reach the canvas")
        tryVerify(function() {
            return AudioEditorController.selectionEnd
                > AudioEditorController.selectionStart
        }, 5000, "880 range drag did not create a selection")
        verify(nativeDropHelper.keyClickItem(canvas, Qt.Key_Escape, Qt.NoModifier),
               "880 native Escape did not reach the canvas")
        tryVerify(function() {
            return AudioEditorController.selectionEnd
                <= AudioEditorController.selectionStart
        }, 5000, "880 Escape did not clear the selection")
        verify(nativeDropHelper.dragItem(narrowPlay, narrowPlay.width * 0.5,
            narrowPlay.height * 0.5, 0, 0))
        tryVerify(function() { return AudioEditorController.playing }, 5000,
            AudioEditorController.errorMessage)
        primaryPlay.forceActiveFocus()
        tryVerify(function() { return primaryPlay.activeFocus })
        verify(nativeDropHelper.keyClickItem(primaryPlay, Qt.Key_Space,
            Qt.NoModifier))
        tryCompare(AudioEditorController, "playing", false)
    }

    function test_realFixtureEditingJourneyUsesNativeInputEndToEnd() {
        destroyCurrentHost()
        host = createTemporaryObject(shellComponent, testCase)
        verify(host)
        verify(nativeDropHelper.prepareItem(host),
               "editing-journey shell was not visible, exposed, and active")
        page = findChild(host, "audioEditorPage")
        verify(page && testAudioUrl && testAudioUrl.toString().length > 0)

        verify(nativeDropHelper.sendUrls(page, [testAudioUrl]))
        tryVerify(function() {
            return AudioEditorController.hasDocument
                && !AudioEditorController.busy
        }, 5000, AudioEditorController.errorMessage)
        const canvas = findChild(page, "editorWaveformCanvas")
        const splitButton = findChild(page, "editorCommand_split")
        const copyButton = findChild(page, "editorCommand_copy")
        const pasteButton = findChild(page, "editorCommand_paste")
        const muteButton = findChild(page, "editorCommand_mute")
        const fadeInButton = findChild(page, "editorCommand_fadeIn")
        const deleteButton = findChild(page, "editorCommand_delete")
        const primaryPlay = findChild(page, "editorPrimaryPlayButton")
        verify(canvas && splitButton && copyButton && pasteButton && muteButton
               && fadeInButton && deleteButton && primaryPlay)

        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(
            0, AudioEditorController.totalFrames))
        const splitPointX = canvas.width * 0.6
        verify(nativeDropHelper.dragItem(canvas, splitPointX,
            canvas.height * 0.75, 0, 0))
        verify(nativeDropHelper.keyClickItem(canvas, Qt.Key_S, Qt.NoModifier))
        tryCompare(AudioEditorController.timelineEventViews, "length", 2)

        const rightId = String(AudioEditorController.timelineEventViews[1].id)
        const rightEvent = findTimelineEventItem(canvas, rightId)
        verify(rightEvent)
        const rightHeader = findVisibleItem(rightEvent,
            "editorEventHeaderInteraction")
        verify(rightHeader)
        verify(nativeDropHelper.dragItem(rightHeader, rightHeader.width * 0.5,
            rightHeader.height * 0.5, 0, 0))
        tryCompare(AudioEditorController, "selectedEventId", rightId)

        verify(copyButton.enabled && pasteButton.enabled === false)
        verify(nativeDropHelper.keyClickItem(canvas, Qt.Key_C,
            Qt.ControlModifier))
        tryVerify(function() {
            return pasteButton.enabled
                && AudioEditorController.actionEnabled("editor.paste")
        })
        verify(nativeDropHelper.dragItem(canvas, canvas.width * 0.8,
            canvas.height * 0.75, 0, 0))
        const beforePasteCount = AudioEditorController.timelineEventViews.length
        const copiedRightBeforePaste = eventViewById(rightId)
        const pasteFrame = Number(AudioEditorController.playheadFrame)
        verify(pasteFrame > Number(copiedRightBeforePaste.timelineStart)
               && pasteFrame < Number(copiedRightBeforePaste.timelineEnd),
               "paste point must force the occupied-event insertion path")
        verify(nativeDropHelper.keyClickItem(canvas, Qt.Key_V,
            Qt.ControlModifier))
        tryCompare(AudioEditorController.timelineEventViews, "length", 4)
        const pastedId = String(AudioEditorController.selectedEventId)
        verify(pastedId.length > 0 && pastedId !== rightId)
        const pastedClone = eventViewById(pastedId)
        compare(Number(pastedClone.sourceStart),
                Number(copiedRightBeforePaste.sourceStart))
        compare(Number(pastedClone.sourceEnd),
                Number(copiedRightBeforePaste.sourceEnd))
        compare(Number(pastedClone.timelineStart), pasteFrame)
        verify(nativeDropHelper.keyClickItem(canvas, Qt.Key_Z, Qt.ControlModifier))
        tryCompare(AudioEditorController.timelineEventViews,
                   "length", beforePasteCount)
        verify(nativeDropHelper.keyClickItem(canvas, Qt.Key_Y, Qt.ControlModifier))
        tryCompare(AudioEditorController.timelineEventViews, "length", 4)
        const pastedEvent = findTimelineEventItem(canvas, pastedId)
        verify(pastedEvent)
        const pastedHeader = findVisibleItem(pastedEvent,
            "editorEventHeaderInteraction")
        verify(pastedHeader)
        verify(nativeDropHelper.dragItem(pastedHeader,
            pastedHeader.width * 0.5, pastedHeader.height * 0.5, 0, 0))
        tryCompare(AudioEditorController, "selectedEventId", pastedId)
        const rightTrim = findVisibleItem(pastedEvent,
            "editorEventRightTrimHandle")
        verify(rightTrim)
        const pastedFramesBeforeTrim = Number(
            AudioEditorController.timelineEventViews.filter(function(event) {
                return String(event.id) === pastedId
            })[0].timelineEnd) - Number(
                AudioEditorController.timelineEventViews.filter(function(event) {
                    return String(event.id) === pastedId
            })[0].timelineStart)
        verify(nativeDropHelper.dragItem(rightTrim, rightTrim.width * 0.5,
            rightTrim.height * 0.5, -Math.min(24, pastedEvent.width * 0.15), 0))
        tryVerify(function() {
            const event = AudioEditorController.timelineEventViews.filter(
                function(candidate) { return String(candidate.id) === pastedId })[0]
            return event && Number(event.timelineEnd) - Number(event.timelineStart)
                < pastedFramesBeforeTrim
        })
        const trimmedPastedEvent = findTimelineEventItem(canvas, pastedId)
        verify(trimmedPastedEvent)
        const trimmedPastedHeader = findVisibleItem(trimmedPastedEvent,
            "editorEventHeaderInteraction")
        verify(trimmedPastedHeader)
        verify(nativeDropHelper.dragItem(trimmedPastedHeader,
            trimmedPastedHeader.width * 0.5,
            trimmedPastedHeader.height * 0.5, 0, 0))
        tryCompare(AudioEditorController, "selectedEventId", pastedId)

        verify(nativeDropHelper.dragItem(muteButton, muteButton.width * 0.5,
            muteButton.height * 0.5, 0, 0))
        tryVerify(function() {
            const event = eventViewById(pastedId)
            return event && event.mute === true
        })
        verify(AudioEditorController.actionEnabled("editor.undo"))
        verify(nativeDropHelper.dragItem(fadeInButton,
            fadeInButton.width * 0.5, fadeInButton.height * 0.5, 0, 0))
        tryVerify(function() {
            const event = AudioEditorController.timelineEventViews.filter(
                function(candidate) { return String(candidate.id) === pastedId })[0]
            return event && Number(event.fadeIn) > 0
        })

        verify(nativeDropHelper.dragItem(deleteButton,
            deleteButton.width * 0.5, deleteButton.height * 0.5, 0, 0))
        tryVerify(function() {
            return !AudioEditorController.timelineEventViews.some(
                function(event) { return String(event.id) === pastedId })
        })
        verify(nativeDropHelper.keyClickItem(canvas, Qt.Key_Z, Qt.ControlModifier))
        tryVerify(function() {
            return AudioEditorController.timelineEventViews.some(
                function(event) { return String(event.id) === pastedId })
        })

        verify(nativeDropHelper.dragItem(canvas, canvas.width * 0.12,
            canvas.height * 0.75, canvas.width * 0.18, 0))
        tryVerify(function() {
            return AudioEditorController.selectionEnd
                > AudioEditorController.selectionStart
                && AudioEditorController.loopEnabled
        })
        primaryPlay.forceActiveFocus()
        tryVerify(function() { return primaryPlay.activeFocus })
        verify(nativeDropHelper.keyClickItem(primaryPlay, Qt.Key_Space,
            Qt.NoModifier))
        tryVerify(function() { return AudioEditorController.playing }, 5000,
            AudioEditorController.errorMessage)
    }

    function test_bodyDragCreatesRangeSelectionWithNativePointerInput() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000),
               "body-drag fixture document creation failed")
        verify(AudioEditorController.setActiveTool("select"),
               "body-drag fixture could not select the range tool")
        const eventId = AudioEditorController.timelineEventViews[0].id
        verify(AudioEditorController.moveEvent(eventId, 48000),
               "body-drag fixture event move failed")
        verify(AudioEditorController.trimEvent(eventId, 0, 96000, 48000),
               "body-drag fixture event trim failed")
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas, "body-drag waveform canvas is missing")
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 144000),
               "body-drag viewport range setup failed")
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventVisualBoundary") !== null
        }, 5000, "body-drag event visual did not become visible")
        const eventVisual = findVisibleItem(canvas, "editorEventVisualBoundary")
        verify(eventVisual, "body-drag event visual disappeared before input")
        const bodyPoint = eventVisual.mapToItem(canvas,
            eventVisual.width * 0.5, 48)
        verify(bodyPoint.y > 32,
               "body-drag point overlaps the clip-header interaction band")

        verify(nativeDropHelper.dragItem(canvas, bodyPoint.x, bodyPoint.y,
            canvas.width * 0.1, 0),
            "native body drag could not be injected")

        verify(AudioEditorController.selectionEnd
            > AudioEditorController.selectionStart,
            "native body drag did not publish a non-empty range selection")
    }

    function test_headerDragMovesTheSelectedEventWithNativePointerInput() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        verify(AudioEditorController.setActiveTool("select"))
        const eventId = AudioEditorController.timelineEventViews[0].id
        verify(AudioEditorController.moveEvent(eventId, 96000))
        verify(AudioEditorController.trimEvent(eventId, 0, 96000, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventHeaderInteraction") !== null
        })
        const header = findVisibleItem(canvas, "editorEventHeaderInteraction")
        verify(header)
        const startX = header.width * 0.75
        const pressOnCanvas = header.mapToItem(canvas, startX,
            header.height * 0.5)
        const pressFrame = canvas.frameAtCanvasPixel(pressOnCanvas.x)
        const destinationOnHeader = canvas.mapToItem(header,
            canvas.pixelAtFrame(pressFrame - 48000), pressOnCanvas.y)

        verify(nativeDropHelper.dragItem(header, startX, header.height * 0.5,
            destinationOnHeader.x - startX, 0))

        compare(AudioEditorController.selectedEventId, String(eventId))
        verify(Number(AudioEditorController.timelineEventViews[0].timelineStart)
            < 96000)
    }

    function test_ctrlHeaderDragCreatesSelectedCopyWithNativeModifierInput() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        verify(AudioEditorController.setActiveTool("select"))
        const originalId = String(AudioEditorController.timelineEventViews[0].id)
        verify(AudioEditorController.moveEvent(originalId, 96000))
        verify(AudioEditorController.trimEvent(originalId, 0, 96000, 96000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventHeaderInteraction") !== null
        })
        const header = findVisibleItem(canvas, "editorEventHeaderInteraction")
        verify(header)
        const modifierObserver = createTemporaryObject(
            mouseModifierObserverComponent, testCase,
            { "target": header })
        verify(modifierObserver)
        const startX = header.width * 0.75
        const pressOnCanvas = header.mapToItem(canvas, startX,
            header.height * 0.5)
        const pressFrame = canvas.frameAtCanvasPixel(pressOnCanvas.x)
        const destinationOnHeader = canvas.mapToItem(header,
            canvas.pixelAtFrame(pressFrame - 96000), pressOnCanvas.y)

        verify(nativeDropHelper.dragItemWithModifiers(
            header, startX, header.height * 0.5,
            destinationOnHeader.x - startX, 0, Qt.ControlModifier))

        verify((modifierObserver.observedModifiers & Qt.ControlModifier) !== 0)

        tryVerify(function() {
            return AudioEditorController.timelineEventViews.length === 2
        }, 1000, "Ctrl+drag copy was rejected; candidate start="
                 + String(header.candidateTimelineStart)
                 + ", error=" + AudioEditorController.errorMessage)
        const events = AudioEditorController.timelineEventViews
        let original = null
        let copied = null
        for (let index = 0; index < events.length; ++index) {
            if (String(events[index].id) === originalId)
                original = events[index]
            else
                copied = events[index]
        }
        verify(original && copied)
        compare(Number(original.timelineStart), 96000)
        compare(Number(copied.timelineStart), 0)
        compare(AudioEditorController.selectedEventId, String(copied.id))
    }

    function test_leftTrimOfFirstClipCropsWithoutLeavingTimelineGap() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventLeftTrimHandle") !== null
        })
        const handle = findVisibleItem(canvas, "editorEventLeftTrimHandle")
        const before = AudioEditorController.timelineEventViews[0]
        verify(nativeDropHelper.dragItem(
            handle, handle.width / 2, handle.height / 2, 120, 0))
        tryVerify(function() {
            return Number(AudioEditorController.timelineEventViews[0].sourceStart)
                > Number(before.sourceStart)
        })
        compare(Number(AudioEditorController.timelineEventViews[0].timelineStart), 0)
        verify(AudioEditorController.totalFrames < 192000)
    }

    function test_rightTrimHandleKeepsNativeGrabAndCommitsOneUndoStep() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventRightTrimHandle") !== null
        })
        const handle = findVisibleItem(canvas, "editorEventRightTrimHandle")
        const before = AudioEditorController.timelineEventViews[0]
        verify(!AudioEditorController.actionEnabled("editor.undo"))
        verify(nativeDropHelper.dragItem(
            handle, handle.width / 2, handle.height / 2, -120, 0))
        tryVerify(function() {
            return Number(AudioEditorController.timelineEventViews[0].sourceEnd)
                < Number(before.sourceEnd)
        })
        verify(AudioEditorController.actionEnabled("editor.undo"))
        verify(AudioEditorController.undo())
        verify(!AudioEditorController.actionEnabled("editor.undo"))
        compare(Number(AudioEditorController.timelineEventViews[0].sourceEnd),
                Number(before.sourceEnd))
    }

    function test_gainCurveOnlyAcceptsNativePointerWithinSixPixels() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventCombinedGainLine") !== null
        })
        const gainLine = findVisibleItem(canvas, "editorEventCombinedGainLine")
        verify(gainLine)
        const onCurve = gainLine.mapToItem(canvas, gainLine.width * 0.6,
                                            gainLine.height * 0.5)

        verify(nativeDropHelper.doubleClickItem(canvas, onCurve.x, onCurve.y + 7))
        wait(0)
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 0,
                "a pointer outside the six-pixel curve target must not add a point")

        verify(nativeDropHelper.doubleClickItem(canvas, onCurve.x, onCurve.y + 20))
        wait(0)
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 0)
        verify(nativeDropHelper.dragItem(canvas, onCurve.x, onCurve.y + 20,
                                         80, 0))
        verify(AudioEditorController.selectionEnd
               > AudioEditorController.selectionStart,
               "twenty pixels from the curve must remain available to range selection")
    }

    function test_fadeNodesDoNotCreateIndependentPointerSurfaces() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventCombinedGainLine") !== null
        })
        verify(findVisibleItem(canvas, "editorEventFadeInNode") === null)
        verify(findVisibleItem(canvas, "editorEventFadeOutNode") === null)
    }

    function test_nativeRightClickOnEligibleCentralCurveOpensAndAppliesFadeMenu() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventGainInteraction") !== null
        })
        let gain = findVisibleItem(canvas, "editorEventGainInteraction")
        verify(gain, "missing central gain interaction")
        let fadeMenu = gain.fadeMenu
        verify(fadeMenu, "missing fade curve menu")
        const eventId = AudioEditorController.timelineEventViews[0].id

        verify(AudioEditorController.setSelection(1000, 3000))
        verify(nativeDropHelper.clickItem(gain, gain.width * 0.5,
                                          gain.height * 0.5,
                                          Qt.RightButton),
               "native right click without fade did not reach the window")
        wait(0)
        compare(fadeMenu.visible, false,
                "a central curve without fades must leave right click to the canvas")
        compare(AudioEditorController.selectionStart, 1000,
                "right click outside the selected range must preserve its start")
        compare(AudioEditorController.selectionEnd, 3000,
                "right click outside the selected range must preserve its end")

        verify(AudioEditorController.setEventFadeOut(eventId, 120000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventGainInteraction") !== null
        })
        gain = findVisibleItem(canvas, "editorEventGainInteraction")
        fadeMenu = gain.fadeMenu
        verify(fadeMenu, "fade curve menu was not recreated with the event")
        verify(nativeDropHelper.clickItem(gain, gain.width * 0.5,
                                          gain.height * 0.5,
                                          Qt.RightButton),
               "native right click on eligible curve did not reach the window")
        tryVerify(function() { return fadeMenu.visible })
        const linear = fadeMenu.itemAt(0)
        verify(linear, "missing linear fade curve menu item")
        verify(nativeDropHelper.clickItem(linear, linear.width * 0.5,
                                          linear.height * 0.5, Qt.LeftButton))
        tryVerify(function() {
            return AudioEditorController.timelineEventViews[0]
                .fadeOutCurve === "linear"
        })
    }

    function test_nativeRightDoubleClickOnFadeCurveDoesNotAddEnvelopePoint() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        const eventId = AudioEditorController.timelineEventViews[0].id
        verify(AudioEditorController.setEventFadeOut(eventId, 120000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventGainInteraction") !== null
        })
        const gain = findVisibleItem(canvas, "editorEventGainInteraction")
        const fadeMenu = gain.fadeMenu
        verify(gain && fadeMenu)

        verify(nativeDropHelper.doubleClickItemWithButton(
            gain, gain.width * 0.5, gain.height * 0.5, Qt.RightButton))
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 0,
                "a right-button double click must not add an Envelope point")
        verify(nativeDropHelper.clickItem(gain, gain.width * 0.5,
                                          gain.height * 0.5, Qt.RightButton))
        tryVerify(function() { return fadeMenu.visible })
    }

    function test_nativeEnvelopePointAddsNearCurveAndDragsBothAxesInOneUndo() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 192000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventCombinedGainLine") !== null
        })
        const gainLine = findVisibleItem(canvas, "editorEventCombinedGainLine")
        verify(gainLine)
        const gainInteraction = findVisibleItem(
            canvas, "editorEventGainInteraction")
        verify(gainInteraction)
        verify(nativeDropHelper.doubleClickItem(
            gainInteraction, gainInteraction.width * 0.55,
            gainInteraction.height * 0.5))
        tryVerify(function() {
            return AudioEditorController.timelineEventViews[0].envelope.length === 1
        }, 5000, "native double click adds one envelope point")
        const beforeDrag = AudioEditorController.timelineEventViews[0]
            .envelope[0]
        const point = findVisibleItem(canvas, "editorEnvelopePoint")
        verify(point)
        verify(nativeDropHelper.dragItem(point, point.width * 0.5,
                                         point.height * 0.5, 72, -64))
        tryVerify(function() {
            const changed = AudioEditorController.timelineEventViews[0].envelope[0]
            return Number(changed.offset) !== Number(beforeDrag.offset)
                && Math.abs(Number(changed.gain) - Number(beforeDrag.gain)) > 0.01
        }, 5000, "native envelope drag changes offset and gain")

        verify(AudioEditorController.undo())
        const restored = AudioEditorController.timelineEventViews[0].envelope[0]
        compare(Number(restored.offset), Number(beforeDrag.offset))
        compare(Number(restored.gain), Number(beforeDrag.gain))
        verify(AudioEditorController.undo(),
               "one additional undo removes the add, proving the drag used one item")
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 0)
    }
}
