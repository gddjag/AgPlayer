import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "PlayerControls"
    when: windowShown

    property var mainWindow: null
    property int savedShellMode: 0

    function findControl(name) {
        return findChild(mainWindow, name)
    }

    function xInControls(control, controls) {
        return control.mapToItem(controls, 0, 0).x
    }

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined")
        mainWindow = testMainWindow
        verify(mainWindow)
        savedShellMode = SettingsController.playerShellMode
    }

    function cleanup() {
        SettingsController.playerShellMode = 0
        mainWindow.width = 1280
        mainWindow.height = 720
    }

    function cleanupTestCase() {
        SettingsController.playerShellMode = savedShellMode
    }

    function test_shared_experience_actions_and_volume_fit_at_1000() {
        SettingsController.playerShellMode = 0
        tryVerify(function() {
            return findControl("classicPlayerShell") !== null
        }, 1500)
        mainWindow.width = 1000
        mainWindow.height = 480
        wait(50)

        var controls = findControl("playerControls")
        verify(controls)
        var actions = findChild(controls, "experienceActions")
        var lyrics = findChild(controls, "lyricsActionButton")
        var immersive = findChild(controls, "immersiveActionButton")
        var mini = findChild(controls, "miniPlayerButton")
        var volume = findChild(controls, "mainVolumeControl")
        verify(actions)
        verify(lyrics)
        verify(immersive)
        verify(mini)
        verify(volume)

        verify(typeof actions.buttonSize === "function",
               "PlayerControls must instantiate the shared ExperienceActions component")
        compare(actions.buttonSize(), 26)
        tryVerify(function() {
            return xInControls(lyrics, controls) + lyrics.width
                    <= xInControls(immersive, controls)
        }, 500, "lyrics and immersive actions must not overlap")
        tryVerify(function() {
            return xInControls(immersive, controls) + immersive.width
                    <= xInControls(mini, controls)
        }, 500, "immersive action must not overlap the mini-player control")
        var tools = findChild(controls, "audioToolsButton")
        var theme = findChild(controls, "themeModeButton")
        verify(tools)
        verify(theme)
        tryVerify(function() {
            return xInControls(volume, controls) + volume.width
                    <= xInControls(theme, controls)
        }, 500, "volume control must not overlap the right-side actions")
        verify(xInControls(volume, controls) + volume.width <= controls.width,
               "volume control must remain inside a 1000 DIP player")
    }

    function test_expanded_volume_keeps_all_1000_dip_controls_separate() {
        nativeDropHelper.ensureSortableTracks()
        tryVerify(function() { return LibraryModel.count > 0 }, 1500)
        SettingsController.playerShellMode = 0
        tryVerify(function() {
            return findControl("classicPlayerShell") !== null
        }, 1500)
        mainWindow.width = 1000
        mainWindow.height = 480
        wait(50)

        var controls = findControl("playerControls")
        var equalizer = findChild(controls, "equalizerButton")
        var tools = findChild(controls, "audioToolsButton")
        var mini = findChild(controls, "miniPlayerButton")
        var theme = findChild(controls, "themeModeButton")
        var volume = findChild(controls, "mainVolumeControl")
        verify(controls && equalizer && tools && mini && theme && volume)

        volume.expandedForQa = true
        tryVerify(function() { return volume.width > 44 }, 500)
        tryVerify(function() {
            return xInControls(tools, controls) + tools.width
                    <= xInControls(equalizer, controls)
        }, 500,
        "audio tools must stay before and clear the transport controls")
        tryVerify(function() {
            return xInControls(volume, controls) + volume.width
                    <= xInControls(theme, controls)
        }, 500, "expanded volume must not cover the right-side actions")
        verify(xInControls(volume, controls) + volume.width <= controls.width,
               "expanded volume must remain inside a 1000 DIP player")
    }

    function test_classic_reference_action_order_and_single_theme_entry() {
        SettingsController.playerShellMode = 0
        mainWindow.width = 1660
        mainWindow.height = 940
        tryVerify(function() {
            return findControl("classicPlayerShell") !== null
        }, 1500)

        var controls = findControl("playerControls")
        verify(controls)
        var playlist = findChild(controls, "listWindowButton")
        var tools = findChild(controls, "audioToolsButton")
        var equalizer = findChild(controls, "equalizerButton")
        var waveform = findChild(controls, "waveformModeButton")
        var previous = findChild(controls, "previousButton")
        var play = findChild(controls, "playPauseButton")
        var next = findChild(controls, "nextButton")
        var mode = findChild(controls, "modeButton")
        var lyrics = findChild(controls, "lyricsActionButton")
        var volume = findChild(controls, "mainVolumeControl")
        var theme = findChild(controls, "themeModeButton")
        var immersive = findChild(controls, "immersiveActionButton")
        var mini = findChild(controls, "miniPlayerButton")
        verify(playlist && tools && equalizer && waveform && previous && play)
        verify(next && mode && lyrics && volume && theme && immersive && mini)

        var ordered = [playlist, tools, equalizer, waveform, previous, play,
                       next, mode, lyrics, volume, theme, immersive, mini]
        for (var index = 1; index < ordered.length; ++index) {
            verify(xInControls(ordered[index - 1], controls)
                   < xInControls(ordered[index], controls),
                   "reference action order must be strictly left-to-right at "
                   + index + ": "
                   + xInControls(ordered[index - 1], controls) + " >= "
                   + xInControls(ordered[index], controls))
        }
        compare(findChild(controls, "windowLayoutButton"), null,
                "theme/shell selection must have one visible entry")
        compare(theme.icon.width, 20)
        compare(theme.icon.height, 20)
        verify(theme.icon.source.toString().endsWith("/theme-skin.svg"))
        compare(lyrics.icon.width, 20)
        compare(lyrics.icon.height, 20)
        compare(immersive.icon.width, 20)
        compare(immersive.icon.height, 20)
    }
}
