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
        compare(controls.actionProfile.join(","),
                "listWindowButton,audioToolsButton,equalizerButton,waveformModeButton,previousButton,playPauseButton,nextButton,modeButton,lyricsActionButton,mainVolumeControl,themeModeButton,immersiveActionButton,miniPlayerButton")
    }

    function test_non_immersive_spectral_progress_policy_is_theme_independent() {
        SettingsController.playerShellMode = 0
        tryVerify(function() {
            return findControl("classicPlayerShell") !== null
        }, 1500)
        verify(Theme.nonImmersiveSpectralUnplayedOpacity <= 0.60)
        var expected = Theme.nonImmersiveSpectralUnplayedOpacity
        for (var theme = 0; theme < 3; ++theme) {
            SettingsController.themeMode = theme
            wait(0)
            compare(Theme.nonImmersiveSpectralUnplayedOpacity, expected)
        }
        var positions = [0, 0.001, 0.5, 0.999, 1]
        for (var index = 0; index < positions.length; ++index) {
            var fraction = positions[index]
            compare(Theme.waveformProgressFraction(fraction * 1000, 1000),
                    fraction)
            compare(Theme.waveformProgressClipWidth(
                        800, fraction * 1000, 1000),
                    800 * fraction)
        }
    }

    function test_classic_actions_keep_safe_edges_and_vertical_center_at_863() {
        SettingsController.playerShellMode = 0
        mainWindow.width = 863
        mainWindow.height = 266
        tryVerify(function() {
            return findControl("classicPlayerShell") !== null
        }, 1500)
        var controls = findControl("playerControls")
        verify(controls)
        var names = controls.actionProfile
        var previousRight = -1
        var centerY = controls.height / 2
        for (var index = 0; index < names.length; ++index) {
            var action = findChild(controls, names[index])
            verify(action && action.visible, "missing " + names[index])
            var point = action.mapToItem(controls, 0, 0)
            verify(point.x >= 0 && point.x + action.width <= controls.width + 1,
                   names[index] + " must remain inside the player")
            verify(Math.abs(point.y + action.height / 2 - centerY) <= 1,
                   names[index] + " must share the play-button center line: "
                   + (point.y + action.height / 2) + " vs " + centerY)
            verify(point.x >= previousRight - 1,
                   names[index] + " must not overlap the prior action")
            previousRight = point.x + action.width
        }
        var play = findChild(controls, "playPauseButton")
        var playPoint = play.mapToItem(controls, 0, 0)
        verify(playPoint.y >= 4)
        verify(playPoint.y + play.height <= controls.height - 4)
    }

    function test_theme_popup_stays_above_invoking_icon_at_100_and_150_percent() {
        SettingsController.playerShellMode = 0
        mainWindow.width = 863
        mainWindow.height = 266
        tryVerify(function() {
            return findControl("classicPlayerShell") !== null
        }, 1500)
        var controls = findControl("playerControls")
        var helper = findChild(controls, "experienceActions")
        var button = findChild(controls, "themeModeButton")
        var menu = findChild(controls, "playerShellMenu")
        verify(controls && helper && button && menu)
        mouseClick(button)
        tryVerify(function() { return menu.visible && menu.height > 0 }, 500)
        for (var index = 0; index < 2; ++index) {
            var dpr = index === 0 ? 1.0 : 1.5
            var popupWindow = controls.Window.window
            verify(popupWindow)
            var point = helper.popupPosition(button, menu, controls, dpr,
                                             popupWindow.contentItem)
            var mappedButton = button.mapToItem(popupWindow.contentItem, 0, 0)
            verify(point.y + menu.height <= mappedButton.y + 1 / dpr,
                   "popup must remain above the invoking icon at DPR " + dpr)
            verify(point.x >= 0)
            verify(point.x + menu.width
                   <= popupWindow.contentItem.width + 1 / dpr,
                   "popup right " + (point.x + menu.width)
                   + " (x=" + point.x + ", width=" + menu.width
                   + ") exceeds " + popupWindow.contentItem.width
                   + " at DPR " + dpr
                   + "; controls=" + controls.width)
            verify(point.y >= 0)
            verify(point.y + menu.height
                   <= popupWindow.contentItem.height + 1 / dpr)
        }
        menu.close()
    }
}
