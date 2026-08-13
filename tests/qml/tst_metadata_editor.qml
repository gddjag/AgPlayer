import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MetadataEditorWorkflow"
    when: windowShown
    visible: true
    width: 1280
    height: 760

    MetadataEditPage {
        id: page
        anchors.fill: parent
    }

    function init() {
        if (!MetadataEditor.busy)
            MetadataEditor.clear()
    }

    function test_directTypingSelectsSetModeAndClearIsExplicit() {
        MetadataEditor.loadFiles([testAudioUrl])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        compare(MetadataEditor.fileCount, 1)
        page.selectedIndices = [0]
        page.selectionAnchor = 0

        compare(page.fieldMode("title"), "keep")
        page.setFieldValue("title", "New title")
        compare(page.fieldMode("title"), "set")
        compare(page.fieldPayload().title.value, "New title")
        verify(page.configuredEditCount() > 0)
        verify(page.targetCount() > 0)

        page.setFieldMode("title", "clear")
        compare(page.fieldMode("title"), "clear")
        compare(page.fieldPayload().title.value, "")
    }
}
