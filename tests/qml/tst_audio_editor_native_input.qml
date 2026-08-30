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

    function init() {
        AudioToolsController.selectTool(0)
        if (AudioEditorController.hasDocument && !AudioEditorController.busy) {
            if (!AudioEditorController.clearDocument())
                verify(AudioEditorController.confirmDiscardAndOpen())
        }
        host = createTemporaryObject(pageComponent, testCase)
        verify(host)
        host.requestActivate()
        page = host.editorPage
        tryVerify(function() { return page.width > 0 && page.height > 0 })
        tryVerify(function() { return host.active })
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
            host.destroy()
            host = null
            page = null
            wait(0)
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
        host.destroy()
        wait(0)
        host = createTemporaryObject(shellComponent, testCase)
        verify(host)
        host.requestActivate()
        tryVerify(function() { return host.active })
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

    function test_bodyDragCreatesRangeSelectionWithNativePointerInput() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 192000))
        verify(AudioEditorController.setActiveTool("select"))
        const eventId = AudioEditorController.timelineEventViews[0].id
        verify(AudioEditorController.moveEvent(eventId, 48000))
        verify(AudioEditorController.trimEvent(eventId, 0, 96000, 48000))
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        AudioEditorController.viewport.setViewportWidth(canvas.width)
        verify(AudioEditorController.viewport.setVisibleRange(0, 144000))
        tryVerify(function() {
            return findVisibleItem(canvas, "editorEventVisualBoundary") !== null
        })
        const eventVisual = findVisibleItem(canvas, "editorEventVisualBoundary")
        verify(eventVisual)
        const bodyPoint = eventVisual.mapToItem(canvas,
            eventVisual.width * 0.5, 48)
        verify(bodyPoint.y > 32)

        verify(nativeDropHelper.dragItem(canvas, bodyPoint.x, bodyPoint.y,
            canvas.width * 0.1, 0))

        verify(AudioEditorController.selectionEnd
            > AudioEditorController.selectionStart)
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

        const events = AudioEditorController.timelineEventViews
        compare(events.length, 2)
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
                "a central curve without fades must leave right click to range clearing")
        compare(AudioEditorController.selectionStart, -1)

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
        const onCurve = gainLine.mapToItem(canvas, gainLine.width * 0.55,
                                            gainLine.height * 0.5)
        verify(nativeDropHelper.doubleClickItem(canvas, onCurve.x, onCurve.y + 5))
        tryVerify(function() {
            return AudioEditorController.timelineEventViews[0].envelope.length === 1
        })
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
        })

        verify(AudioEditorController.undo())
        const restored = AudioEditorController.timelineEventViews[0].envelope[0]
        compare(Number(restored.offset), Number(beforeDrag.offset))
        compare(Number(restored.gain), Number(beforeDrag.gain))
        verify(AudioEditorController.undo(),
               "one additional undo removes the add, proving the drag used one item")
        compare(AudioEditorController.timelineEventViews[0].envelope.length, 0)
    }
}
