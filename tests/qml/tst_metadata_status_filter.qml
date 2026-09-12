import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MetadataStatusFilter"
    when: windowShown
    visible: true
    width: 1280
    height: 720

    MetadataEditPage {
        id: page
        anchors.fill: parent
    }

    function initTestCase() {
        if (!MetadataEditor.busy)
            MetadataEditor.clear()
        MetadataEditor.loadFiles([testAudioUrl])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        compare(MetadataEditor.fileCount, 1)
    }

    function cleanupTestCase() {
        if (!MetadataEditor.busy)
            MetadataEditor.clear()
    }

    function test_visibleControlFiltersActualRows() {
        const statusFilter = findChild(page, "metadataStatusFilter")
        verify(statusFilter)
        verify(statusFilter.visible)
        compare(page.displayedIndices.length, 1)

        statusFilter.currentIndex = 1
        tryCompare(page, "statusFilter", "ready")
        compare(page.displayedIndices.length, 1)

        statusFilter.currentIndex = 3
        tryCompare(page, "statusFilter", "modified")
        compare(page.displayedIndices.length, 0)

        statusFilter.currentIndex = 0
        tryCompare(page, "statusFilter", "all")
        compare(page.displayedIndices.length, 1)
    }
}
