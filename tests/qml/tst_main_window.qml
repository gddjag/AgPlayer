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
        verify(findChild(mainWindow, "playPauseButton"), "playPauseButton should exist")
        verify(findChild(mainWindow, "previousButton"), "previousButton should exist")
        verify(findChild(mainWindow, "nextButton"), "nextButton should exist")
        verify(findChild(mainWindow, "modeButton"), "modeButton should exist")
        verify(findChild(mainWindow, "volumeSlider"), "volumeSlider should exist")
        verify(findChild(mainWindow, "miniPlayerButton"), "miniPlayerButton should exist")
        verify(findChild(mainWindow, "settingsButton"), "settingsButton should exist in Phase 2 settings task")
        verify(findChild(mainWindow, "audioToolsButton"), "audioToolsButton should exist in Phase 2.1")
        verify(findChild(mainWindow, "listWindowButton"), "listWindowButton should exist")
    }

    function test_empty_library_shows_startup_actions() {
        var startup = findChild(mainWindow, "emptyStartup")
        verify(startup, "empty startup surface should exist")
        verify(startup.visible, "empty startup surface should be visible")
        verify(findChild(startup, "openFileButton"), "open file action should exist")
        verify(findChild(startup, "importFolderButton"), "import folder action should exist")
        verify(findChild(mainWindow, "waveformModeButton"),
               "waveform mode action should exist in the bottom control bar")
        verify(findChild(mainWindow, "audioToolsButton").visible,
               "audio tools action should be visible in the bottom control bar")
        verify(findChild(mainWindow, "listWindowButton").visible,
               "playlist action should be visible in the bottom control bar")
        verify(findChild(mainWindow, "miniPlayerButton").visible,
               "mini player action should be visible in the bottom control bar")
    }
}
