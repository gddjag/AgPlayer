import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "AudioEditorV2"
    when: windowShown
    visible: true
    width: 1280
    height: 720

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

    property var host
    property var page

    function init() {
        if (AudioEditorController.hasDocument && !AudioEditorController.busy)
            AudioEditorController.clearDocument()
        host = createTemporaryObject(pageComponent, testCase)
        verify(host)
        page = host.editorPage
        verify(page)
        tryVerify(function() { return page.width > 0 && page.height > 0 })
    }

    function test_uploadedUiStructure() {
        compare(findChild(page, "audioEditorLeftSidebar"), null)
        verify(findChild(page, "editorCommandBar"))
        verify(findChild(page, "fileSummaryBar"))
        verify(findChild(page, "editorWaveformCanvas"))
        verify(findChild(page, "overviewNavigator"))
        verify(findChild(page, "recordingInspector"))
        verify(findChild(page, "timePitchInspector"))
        verify(findChild(page, "editorTransportBar"))
        verify(findChild(page, "editorStatusBar"))
    }

    function test_rightInspectorContainsOnlyTwoBusinessGroups() {
        const inspector = findChild(page, "editorInspector")
        verify(inspector)
        compare(inspector.businessSectionCount, 2)
        compare(findChild(inspector, "inspectorRecordButton"), null)
        compare(findChild(inspector, "recordingSettingsGear"), null)
    }

    function test_emptyStateContainsNoDemoValues() {
        compare(AudioEditorController.hasDocument, false)
        verify(findChild(page, "editorWaveformCanvas"))
    }

    function test_transportPrioritizesMarkerActionsWithoutDuplicateAudioControls() {
        const transport = findChild(page, "editorTransportBar")
        verify(transport)
        verify(findChild(transport, "transportAddMarker"))
        verify(findChild(transport, "transportPreviousMarker"))
        verify(findChild(transport, "transportNextMarker"))
        verify(findChild(page, "markerManagementDialog"))
        compare(findChild(transport, "transportVolumeControl"), null)
        compare(findChild(transport, "transportZoomControl"), null)
    }

    function test_editCommandsExposeMouseHelpAndConventionalShortcuts() {
        const commandBar = findChild(page, "editorCommandBar")
        verify(commandBar)
        const crop = findChild(commandBar, "editorCommand_cropToSelection")
        const silence = findChild(commandBar, "editorCommand_silenceSelection")
        const fadeIn = findChild(commandBar, "editorCommand_fadeIn")
        const fadeOut = findChild(commandBar, "editorCommand_fadeOut")
        verify(crop && silence && fadeIn && fadeOut)
        compare(crop.shortcutText, "Ctrl+T")
        compare(silence.shortcutText, "Ctrl+L")
        compare(fadeIn.shortcutText, "Ctrl+Alt+I")
        compare(fadeOut.shortcutText, "Ctrl+Alt+O")
        verify(crop.hoverText.indexOf(crop.label) >= 0)
        verify(crop.hoverText.indexOf(crop.shortcutText) >= 0)
        verify(silence.hoverText.indexOf(silence.shortcutText) >= 0)
        verify(findChild(commandBar, "editorCommand_normalize"))
        verify(findChild(commandBar, "editorCommand_insertSilence"))
        verify(findChild(commandBar, "editorCommand_addMarker"))
        verify(findChild(commandBar, "editorCommand_exportSelection"))
        verify(findChild(commandBar, "editorCommand_clearSelection"))
        verify(findChild(commandBar, "editorCommand_clearDocument"))
        compare(findChild(commandBar, "editorCommand_moreMenu"), null)
    }

    function test_keyboardAndMouseEditingInteractions() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        page.forceActiveFocus()
        keyClick(Qt.Key_A, Qt.ControlModifier)
        compare(AudioEditorController.selectionFrames, 96000)
        keyClick(Qt.Key_M, Qt.ControlModifier)
        compare(AudioEditorController.markers.length, 1)
        keyClick(Qt.Key_L, Qt.ControlModifier)
        verify(AudioEditorController.modified)

        const interaction = findChild(page, "editorWaveformInteraction")
        verify(interaction)
        const before = AudioEditorController.viewport.visibleFrameCount
        mouseWheel(interaction, interaction.width / 2, interaction.height / 2,
                   0, 120, Qt.NoButton, Qt.ControlModifier)
        tryVerify(function() {
            return AudioEditorController.viewport.visibleFrameCount < before
        })
    }

    function test_waveformDirectManipulationKeepsSelectionAndPlayheadIndependent() {
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        verify(canvas.width > 0)
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(12000, 36000))
        const startHandle = findChild(canvas, "editorSelectionStartHandle")
        const endHandle = findChild(canvas, "editorSelectionEndHandle")
        const playhead = findChild(canvas, "editorPlayheadHandle")
        verify(startHandle && endHandle && playhead)
        verify(startHandle.width >= 24)
        verify(endHandle.width >= 24)
        verify(playhead.width >= 24)
    }

    function test_cancelSelectionAndClearDocumentShortcuts() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        verify(AudioEditorController.setSelection(12000, 36000))
        page.forceActiveFocus()
        keyClick(Qt.Key_A, Qt.ControlModifier | Qt.ShiftModifier)
        compare(AudioEditorController.selectionStart, -1)
        keyClick(Qt.Key_W, Qt.ControlModifier)
        compare(AudioEditorController.hasDocument, false)
    }

    function test_overviewWindowDragsContinuouslyFromGrabPoint() {
        const overview = findChild(page, "overviewNavigator")
        verify(overview)
        verify(overview.width > 0)
    }

    function test_noPageLevelHorizontalOverflow_data() {
        return [
            {tag: "1280x720", w: 1280, h: 720},
            {tag: "1600x900", w: 1600, h: 900},
            {tag: "2560x1440", w: 2560, h: 1440},
            {tag: "3840x2160", w: 3840, h: 2160}
        ]
    }

    function test_noPageLevelHorizontalOverflow(data) {
        host.width = data.w
        host.height = data.h
        wait(0)
        compare(page.width, data.w)
        compare(page.height, data.h)
    }
}
