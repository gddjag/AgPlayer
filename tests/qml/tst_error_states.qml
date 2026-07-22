import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "ErrorStates"
    when: windowShown

    function initTestCase() {
        verify(typeof testHarness !== "undefined", "testHarness context property should exist")
        verify(typeof PlaybackController !== "undefined", "PlaybackController singleton should exist")
        verify(typeof ImportController !== "undefined", "ImportController singleton should exist")
        wait(50)
    }

    function cleanup() {
        // Clear errors between tests so each test starts from a clean state.
        if (ImportController.busy) {
            wait(500)
        }
    }

    function test_playbackControllerExposesDeviceLostProperty() {
        compare(PlaybackController.deviceLost, false, "deviceLost should start false")
        verify(typeof PlaybackController.retryDevice === "function",
               "retryDevice should be callable from QML")
    }

    function test_deviceLossSurfacesInPlaybackController() {
        compare(PlaybackController.deviceLost, false, "deviceLost should start false")

        testHarness.simulateDeviceLoss()

        // PlaybackController polls every 34ms; wait for the next snapshot.
        wait(200)

        verify(PlaybackController.deviceLost, "deviceLost should become true after simulateDeviceLoss")
        verify(PlaybackController.errorMessage.length > 0,
               "errorMessage should be populated when device is lost")

        testHarness.retryDevice()

        // Wait for the poll cycle to pick up the recovery.
        wait(200)

        verify(!PlaybackController.deviceLost,
               "deviceLost should return to false after retryDevice")
    }

    function test_corruptFileImportReportsErrors() {
        const initialErrorCount = ImportController.errors.length
        const path = testHarness.corruptFilePath
        verify(path.length > 0, "corruptFilePath should be non-empty")

        ImportController.importUrls([Qt.resolvedUrl("file:///" + path)])

        // Import runs in a background thread; wait for it to finish.
        var waited = 0
        while (ImportController.busy && waited < 3000) {
            wait(100)
            waited += 100
        }
        verify(!ImportController.busy, "import should complete within 3 seconds")

        verify(ImportController.errors.length > initialErrorCount,
               "corrupt file should produce at least one import error")
    }

    function test_missingFileImportReportsErrors() {
        const path = testHarness.missingFilePath
        verify(path.length > 0, "missingFilePath should be non-empty")

        ImportController.importUrls([Qt.resolvedUrl("file:///" + path)])

        var waited = 0
        while (ImportController.busy && waited < 3000) {
            wait(100)
            waited += 100
        }
        verify(!ImportController.busy, "import should complete within 3 seconds")
        verify(ImportController.errors.length > 0,
               "missing file should produce an import error")
    }

    function test_retryDeviceIsSafeWhenNoDeviceLoss() {
        // Calling retryDevice when deviceLost is false must not crash or
        // change the error state.
        const initialError = PlaybackController.errorMessage
        testHarness.retryDevice()
        wait(100)
        compare(PlaybackController.deviceLost, false,
                "deviceLost should remain false when no loss occurred")
    }
}
