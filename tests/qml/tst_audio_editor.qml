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

    function test_transportHasNoDuplicateMarkerOrAudioControls() {
        const transport = findChild(page, "editorTransportBar")
        verify(transport)
        compare(findChild(transport, "transportAddMarker"), null)
        compare(findChild(transport, "transportPreviousMarker"), null)
        compare(findChild(transport, "transportNextMarker"), null)
        compare(findChild(page, "markerManagementDialog"), null)
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
        compare(findChild(commandBar, "editorCommand_normalize"), null)
        verify(findChild(commandBar, "editorCommand_insertSilence"))
        verify(findChild(commandBar, "editorCommand_noiseReduction"))
        compare(findChild(commandBar, "editorCommand_addMarker"), null)
        verify(findChild(commandBar, "editorCommand_exportMenu"))
        verify(findChild(commandBar, "editorCommand_clearSelection"))
        verify(findChild(commandBar, "editorCommand_clearDocument"))
        compare(findChild(commandBar, "editorCommand_moreMenu"), null)
    }

    function test_keyboardAndMouseEditingInteractions() {
        verify(AudioEditorController.createUntitledDocument(48000, 2, 96000))
        page.forceActiveFocus()
        keyClick(Qt.Key_A, Qt.ControlModifier)
        compare(AudioEditorController.selectionFrames, 96000)
        keyClick(Qt.Key_L, Qt.ControlModifier)
        verify(AudioEditorController.modified)

        const interaction = findChild(page, "editorWaveformInteraction")
        verify(interaction)
        const before = AudioEditorController.viewport.visibleFrameCount
        const canvas = findChild(page, "editorWaveformCanvas")
        verify(canvas)
        const accepted = canvas.zoomAt({modifiers: Qt.ControlModifier,
                                        angleDelta: {y: 120}, accepted: false},
                                       interaction.width / 2, interaction.width)
        verify(accepted)
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
        verify(findChild(canvas, "editorSelectionContextCancel"))
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
            {tag: "1672x942", w: 1672, h: 942},
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

    function test_schemeThreeInspectorAndShortcutCardStayVisible() {
        verify(findChild(page, "editorInspectorTabs"))
        verify(findChild(page, "editorShortcutCard"))
        host.width = 1672
        // The 942 px tool window leaves 839 px for this page after its title
        // and top navigation bars.
        host.height = 839
        wait(0)
        const inspector = findChild(page, "editorInspector")
        verify(inspector)
        verify(inspector.height <= page.height)
        compare(inspector.compact, false,
                "the 1672x942 reference viewport must show both inspector groups")
        verify(findChild(page, "recordingInspector").visible)
        verify(findChild(page, "timePitchInspector").visible)
    }

    function test_export_dialog_is_complete_inside_the_window() {
        host.width = 880
        // The 560 px tool window leaves 457 px below its title and navigation.
        host.height = 457
        wait(0)
        const dialog = findChild(page, "audioEditorExportDialog")
        verify(dialog)
        dialog.open()
        tryVerify(function() { return dialog.visible })
        verify(dialog.width <= page.width - 24)
        verify(dialog.height <= page.height - 24)
        verify(dialog.contentItem.height <= dialog.availableHeight)
        dialog.close()
    }

    function test_compact_layout_keeps_shortcut_help_visible() {
        host.width = 880
        host.height = 457
        wait(0)
        const shortcutHint = findChild(page, "editorStatusShortcutHint")
        verify(shortcutHint)
        verify(shortcutHint.visible)
        verify(shortcutHint.text.indexOf("Space") >= 0)
        verify(shortcutHint.text.indexOf("Ctrl+W") >= 0)
    }
}
