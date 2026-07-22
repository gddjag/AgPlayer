import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MainWindowControls"
    when: windowShown

    property var mainWindow: null

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined", "testMainWindow context property should exist")
        mainWindow = testMainWindow
        verify(mainWindow, "Failed to create Main window")
        wait(50)
    }

    function cleanupTestCase() {
        mainWindow = null
    }

    function test_visible_controls_have_actions() {
        verify(mainWindow !== null, "Main window should exist")
        verify(findChild(mainWindow, "importButton"), "importButton should exist")
        verify(findChild(mainWindow, "playPauseButton"), "playPauseButton should exist")
        verify(findChild(mainWindow, "previousButton"), "previousButton should exist")
        verify(findChild(mainWindow, "nextButton"), "nextButton should exist")
        verify(findChild(mainWindow, "modeButton"), "modeButton should exist")
        verify(findChild(mainWindow, "volumeSlider"), "volumeSlider should exist")
        verify(findChild(mainWindow, "miniPlayerButton"), "miniPlayerButton should exist")
        compare(findChild(mainWindow, "settingsButton"), null, "settingsButton should not exist in Phase 1")
        compare(findChild(mainWindow, "audioToolsButton"), null, "audioToolsButton should not exist in Phase 1")
    }

    function test_track_list_and_empty_library_exist() {
        verify(findChild(mainWindow, "trackList"), "trackList should exist")
        verify(findChild(mainWindow, "emptyLibrary"), "emptyLibrary should exist")
    }
}
