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
        verify(xInControls(lyrics, controls) + lyrics.width
               <= xInControls(immersive, controls),
               "lyrics and immersive actions must not overlap")
        verify(xInControls(immersive, controls) + immersive.width
               <= xInControls(mini, controls),
               "immersive action must not overlap the mini-player control")
        var tools = findChild(controls, "audioToolsButton")
        verify(tools)
        verify(xInControls(volume, controls) + volume.width
               <= xInControls(tools, controls),
               "volume control must not overlap the secondary actions")
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
        var volume = findChild(controls, "mainVolumeControl")
        verify(controls && equalizer && tools && mini && volume)

        volume.expandedForQa = true
        tryVerify(function() { return volume.width >= 190 }, 500)
        verify(xInControls(equalizer, controls) + equalizer.width
               <= xInControls(tools, controls),
               "transport controls must clear the audio-tools control: equalizer="
               + xInControls(equalizer, controls) + "+" + equalizer.width
               + ", tools=" + xInControls(tools, controls))
        verify(xInControls(volume, controls) + volume.width
               <= xInControls(tools, controls),
               "expanded volume must not cover the secondary actions")
        verify(xInControls(volume, controls) + volume.width <= controls.width,
               "expanded volume must remain inside a 1000 DIP player")
    }
}
