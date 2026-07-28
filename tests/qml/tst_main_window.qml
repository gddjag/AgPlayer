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

    function init() {
        failOnWarning(/.?/)
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
        verify(findChild(mainWindow, "playerCover"), "player cover should exist")
        verify(findChild(mainWindow, "playerCoverImage").source.toString().length > 0,
               "player cover should always have a fallback source")
        var trackTitle = findChild(mainWindow, "trackTitle")
        var favoriteButton = findChild(mainWindow, "favoriteButton")
        verify(trackTitle, "track title should exist")
        verify(favoriteButton, "favorite button should exist")
        verify(favoriteButton.background === null,
               "favorite button should not render a platform-style square")
        verify(favoriteButton.x <= trackTitle.x + trackTitle.implicitWidth + 40,
               "favorite button should remain next to the title")
        verify(findChild(mainWindow, "trackArtistAlbum"), "artist and album should exist")
        verify(findChild(mainWindow, "trackRating"), "track rating should exist")
        verify(findChild(mainWindow, "mainWaveform"), "main waveform should exist")
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

    function test_failed_import_surfaces_status_in_main() {
        var status = findChild(mainWindow, "importStatusPanel")
        verify(status, "main window should expose shared import status")

        mainWindow.importFiles([Qt.resolvedUrl("file:///agplayer-missing-test.wav")])
        tryVerify(function() {
            return !ImportController.busy && ImportController.errors.length > 0
        }, 3000)

        verify(status.active, "import errors should activate the status panel")
        verify(status.visible, "import errors should be visible in the main window")
        verify(!findChild(mainWindow, "emptyStartup").visible,
               "startup actions should not cover import errors")
    }

    function test_import_files_reaches_real_controller() {
        verify(testAudioUrl.toString().length > 0,
               "generated audio fixture should be available")
        compare(LibraryModel.count, 0)
        mainWindow.importFiles([testAudioUrl])
        tryVerify(function() { return LibraryModel.count === 1 }, 5000)
    }

    function test_waveform_click_seeks_real_playback_controller() {
        var waveform = findChild(mainWindow, "mainWaveform")
        verify(waveform, "main waveform should exist after importing audio")

        PlaybackController.playRow(0)
        tryVerify(function() {
            return PlaybackController.durationMs > 0 && waveform.width > 0
        }, 5000)

        var expected = Math.round(PlaybackController.durationMs * 0.75)
        mouseClick(waveform, waveform.width * 0.75, waveform.height / 2)
        tryVerify(function() {
            return Math.abs(PlaybackController.positionMs - expected) < 150
        }, 1000)
    }

    function test_waveform_modes_reuse_analyzed_layers() {
        var waveform = findChild(mainWindow, "mainWaveform")
        var previousMode = SettingsController.waveformMode
        tryVerify(function() {
            return waveform.layers.mix && waveform.layers.mix.length > 0
        }, 5000)

        SettingsController.waveformMode = 0
        tryVerify(function() {
            return Object.keys(waveform.layers).length === 1
                    && waveform.layers.mix.length > 0
        })
        compare(waveform.waveformColor.toString(), Theme.cyan.toString())

        SettingsController.waveformMode = 1
        tryVerify(function() {
            return Object.keys(waveform.layers).length === 1
                    && waveform.layers.mix.length > 0
        })
        verify(waveform.waveformColor.toString() !== Theme.cyan.toString())

        SettingsController.waveformMode = 2
        tryVerify(function() {
            return Object.keys(waveform.layers).length === 3
                    && waveform.layers.bass.length > 0
                    && waveform.layers.mid.length > 0
                    && waveform.layers.high.length > 0
        })

        SettingsController.waveformMode = previousMode
    }

    function test_z_current_track_metadata_reacts_to_library_changes() {
        var favoriteButton = findChild(mainWindow, "favoriteButton")
        verify(favoriteButton, "current-track favorite button should exist")
        verify(PlaybackController.currentTrackId.length > 0)

        var row = LibraryModel.indexForTrackId(PlaybackController.currentTrackId)
        verify(row >= 0)
        LibraryModel.setFavorite(row, false)
        tryVerify(function() {
            return favoriteButton.icon.source.toString()
                    === Theme.icon("heart-line").toString()
        })

        LibraryModel.setFavorite(row, true)
        tryVerify(function() {
            return favoriteButton.icon.source.toString()
                    === Theme.icon("heart-fill").toString()
        })
        LibraryModel.setFavorite(row, false)
    }

    function test_empty_startup_uses_compact_reference_structure() {
        compare(mainWindow.width, 1228)
        compare(mainWindow.height, 424)

        var startup = findChild(mainWindow, "emptyStartup")
        var controls = findChild(mainWindow, "playerControls")
        verify(startup.visible)
        verify(controls.emptyMode)
        verify(findChild(mainWindow, "titleBrand").visible,
               "brand should be visible in the empty title bar")
        verify(findChild(startup, "startupTitle"),
               "compact startup title should exist")
        verify(findChild(startup, "startupActionArea"),
               "compact startup actions should exist")
        verify(!findChild(startup, "startupHeroArtwork"),
               "the superseded hero artwork should not exist")
    }

    function test_settings_page_is_lazy_until_requested() {
        verify(!findChild(mainWindow, "settingsPage"),
               "settings page should not increase empty-startup cost")

        SettingsController.themeMode = 1
        findChild(mainWindow, "settingsButton").clicked()
        tryVerify(function() {
            return findChild(mainWindow, "settingsPage") !== null
        })
        const page = findChild(mainWindow, "settingsPage")
        const scroll = findChild(page, "settingsScroll")
        const sectionList = findChild(page, "settingsSectionList")
        verify(scroll, "settings content must expose a scroll viewport")
        verify(sectionList, "settings navigation must expose a scrollable list")
        verify(scroll.contentHeight > scroll.availableHeight,
               "settings content must remain reachable in the compact main window")
        verify(sectionList.contentHeight > sectionList.height,
               "all settings sections must remain reachable in the compact main window")
        verify(sectionList.interactive,
               "settings navigation must accept scrolling to cache and about sections")
        SettingsController.themeMode = 2
        page.close()
        tryCompare(SettingsController, "themeMode", 1)

        page.open()
        SettingsController.themeMode = 2
        findChild(page, "settingsSaveButton").clicked()
        tryCompare(SettingsController, "themeMode", 2)
        SettingsController.themeMode = 0
    }

    function test_theme_mode_updates_surfaces_text_and_icons() {
        var previousMode = SettingsController.themeMode

        SettingsController.themeMode = 0
        tryCompare(Theme, "isLight", false)
        var darkBackground = Theme.background.toString()
        var darkText = Theme.primaryText.toString()

        SettingsController.themeMode = 1
        compare(SettingsController.themeMode, 1)
        tryCompare(Theme, "isLight", true)
        verify(Theme.onBrandGradientText.toString()
               !== Theme.onCyanText.toString(),
               "brand gradients and solid cyan controls need separate text colors")
        verify(Theme.background.toString() !== darkBackground,
               "light mode should replace the dark surface")
        verify(Theme.primaryText.toString() !== darkText,
               "light mode should replace the dark text color")
        compare(findChild(mainWindow, "settingsButton").icon.color.toString(),
                Theme.iconSecondary.toString())

        SettingsController.themeMode = 2
        tryCompare(Theme, "followsSystem", true)
        compare(Theme.background.toString(),
                Theme.systemPalette.window.toString())

        SettingsController.themeMode = previousMode
    }
}
