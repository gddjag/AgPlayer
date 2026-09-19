import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "SixTrackNativeInput"
    when: windowShown
    visible: true
    width: 1672
    height: 822
    property var host
    property var page
    Component {
        id: pageComponent
        Window {
            width: 1672; height: 822; visible: true
            property alias editorPage: page
            AudioEditorPage { id: page; anchors.fill: parent }
        }
    }
    Component { id: shellComponent; AudioToolsWindow { visible: true } }
    function destroyCurrentHost() {
        if (!host) return
        const closingHost = host
        host = null
        page = null
        verify(nativeDropHelper.destroyItem(closingHost),
               "native editor shell did not finish deterministic cleanup")
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

    function test_nativeDropImportsBatchIntoSeparateTracks() {
        destroyCurrentHost()
        host = createTemporaryObject(shellComponent, testCase)
        verify(nativeDropHelper.prepareItem(host))
        page = findChild(host, "audioEditorPage")
        const dropArea = findChild(page, "editorAudioDropArea")
        verify(dropArea && testAudioUrl && testAudioUrl.toString().length > 0)
        const second = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        const third = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(second.toString().length && third.toString().length)
        verify(nativeDropHelper.sendUrls(page, [testAudioUrl, second, third]))
        tryVerify(function() {
            return !AudioEditorController.busy
                && AudioEditorController.timelineEventViews.length === 3
        }, 10000)
        const events = AudioEditorController.timelineEventViews
        compare(events[0].trackIndex, 0)
        compare(events[1].trackIndex, 1)
        compare(events[2].trackIndex, 2)
        for (const event of events) compare(Number(event.timelineStart), 0)
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        const dropPosition = canvas.mapToItem(host.contentItem, canvas.width / 3, canvas.rowHeight * 4.5)
        const expectedFrame = canvas.frameAtCanvasPixel(canvas.width / 3)
        verify(nativeDropHelper.sendUrls(host, [testAudioUrl], dropPosition.x, dropPosition.y))
        tryVerify(function() { return !AudioEditorController.busy && AudioEditorController.timelineEventViews.length === 4 }, 10000)
        const dropped = AudioEditorController.timelineEventViews.filter(function(clip) { return clip.trackIndex === 4 })[0]
        verify(dropped)
        verify(Math.abs(Number(dropped.timelineStart) - expectedFrame) <= 2)
    }
}
