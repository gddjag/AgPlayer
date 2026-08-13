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
