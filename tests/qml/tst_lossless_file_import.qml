import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: suite
    name: "LosslessFileImport"
    when: windowShown
    property var main: testMainWindow
    property var page
    property var host
    Component {
        id: pageComponent
        Window {
            width: 1100
            height: 700
            visible: true
            property alias page: contentPage
            LosslessIdentifyPage { id: contentPage; anchors.fill: parent }
        }
    }

    function init() {
        LosslessAnalysisController.clear()
        main.showNormal()
        main.requestActivate()
        host = createTemporaryObject(pageComponent, null)
        verify(host)
        page = host.page
        verify(page)
        wait(50)
    }

    function cleanup() {
        LosslessAnalysisController.clear()
    }

    function checkImportedAndAnalyze() {
        tryCompare(LosslessAnalysisController, "totalCount", 1, 5000)
        compare(LosslessAnalysisController.error, "")
        LosslessAnalysisController.start()
        tryCompare(LosslessAnalysisController, "completedCount", 1, 15000)
    }

    function test_fileDialogAcceptedSelectionReachesQueue() {
        page.openFileDialog()
        const dialog = findChild(page, "losslessFileDialog")
        verify(dialog)
        // Exercise the real URL property and accepted handler. Native panel
        // clicks are separate from this QML boundary check.
        dialog.selectedFile = testAudioUrl
        compare(String(dialog.selectedFile), String(testAudioUrl))
        dialog.accepted()
        checkImportedAndAnalyze()
    }

    function test_dragEnterMoveDropReachesQueue() {
        const target = findChild(page, "losslessFileDropArea")
        verify(target)
        verify(nativeDropHelper.sendUrls(target, [testAudioUrl]))
        checkImportedAndAnalyze()
    }

    function test_invalidFileShowsActualError() {
        page.importSelectedFiles(["file:///agplayer-nonexistent-import-fixture.wav"])
        tryVerify(function() { return LosslessAnalysisController.error.length > 0 })
        const status = findChild(page, "losslessImportStatus")
        compare(status.text, LosslessAnalysisController.error)
    }

    function test_emptyAcceptedSelectionReportsError() {
        page.importSelectedFiles([])
        verify(LosslessAnalysisController.error.length > 0)
        compare(LosslessAnalysisController.totalCount, 0)
        compare(findChild(page, "losslessImportStatus").text,
                LosslessAnalysisController.error)
    }
}
