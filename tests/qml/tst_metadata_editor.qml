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

    Component {
        id: compactPageComponent
        MetadataEditPage { width: 880; height: 457 }
    }

    function init() {
        testCase.width = 1280
        testCase.height = 760
        if (!MetadataEditor.busy)
            MetadataEditor.clear()
    }

    function test_compact_layout_keeps_list_editor_and_actions_visible() {
        const compactPage = createTemporaryObject(compactPageComponent, testCase)
        verify(compactPage)
        wait(0)
        const files = findChild(compactPage, "metadataFilePanel")
        const inspector = findChild(compactPage, "metadataInspectorPanel")
        const bottom = findChild(compactPage, "metadataBottomBar")
        const apply = findChild(compactPage, "metadataApplyButton")
        const tabs = findChild(compactPage, "metadataCompactTabs")
        verify(files && inspector && bottom && apply && tabs)
        verify(files.visible)
        verify(files.width >= 820)
        tabs.currentIndex = 1
        tryVerify(function() { return inspector.width > 0 })
        verify(inspector.visible)
        verify(inspector.width >= 780, "inspector width=" + inspector.width)
        verify(inspector.mapToItem(compactPage, inspector.width, 0).x <= compactPage.width)
        verify(apply.x + apply.width <= bottom.width)
    }

    function test_directFieldsPreserveUntilEditedAndEmptyMeansClear() {
        MetadataEditor.loadFiles([testAudioUrl])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        compare(MetadataEditor.fileCount, 1)
        page.selectedIndices = [0]
        page.selectionAnchor = 0
        page.refreshFields()

        compare(page.fieldMode("title"), "keep")
        compare(page.fieldValue("title"),
                String(MetadataEditor.entryAt(0).title || ""))
        compare(findChild(page, "metadataModeButton_title_keep"), null)

        page.setFieldValue("title", "New title")
        compare(page.fieldMode("title"), "set")
        compare(page.fieldPayload().title.value, "New title")
        verify(page.configuredEditCount() > 0)
        verify(page.targetCount() > 0)

        page.setFieldValue("title", "")
        compare(page.fieldMode("title"), "clear")
        compare(page.fieldPayload().title.value, "")
    }
}
