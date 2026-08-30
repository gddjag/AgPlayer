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
}
