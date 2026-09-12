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
        compare(actions.buttonSize(), 32)
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

    function test_classic_information_block_stays_centered_on_cover() {
        SettingsController.playerShellMode = 0
        tryVerify(function() { return findControl("classicPlayerShell") !== null }, 1500)
        var sizes = [[863, 266], [1000, 320], [1280, 480]]
        for (var index = 0; index < sizes.length; ++index) {
            mainWindow.width = sizes[index][0]
            mainWindow.height = sizes[index][1]
            wait(60)
            var cover = findControl("playerCover")
            var subtitle = findControl("trackArtistRatingRow")
            var title = findControl("trackTitleViewport").parent
            var badges = findControl("trackMetadataBadges")
            var block = subtitle.parent
            verify(cover && subtitle && title && badges && block)
            var coverCenter = cover.mapToItem(block, 0, cover.height / 2).y
            verify(Math.abs(block.height / 2 - coverCenter) <= 1,
                   "the complete title/subtitle/metadata block must be centered on the cover")
            verify(Math.abs((subtitle.y - title.y - title.height)
                            - (badges.y - subtitle.y - subtitle.height)) <= 1,
                   "all three information rows must have equal spacing")
            var artistText = findControl("trackArtistAlbum")
            verify(subtitle.height >= artistText.implicitHeight,
                   "subtitle row cannot compress below its actual text height")
        }
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
        compare(theme.icon.width, 22)
        compare(theme.icon.height, 22)
        verify(theme.icon.source.toString().endsWith("/theme-skin.svg"))
        compare(lyrics.icon.width, 22)
        compare(lyrics.icon.height, 22)
        var immersiveIcon = findChild(immersive, "animatedImmersiveIcon")
        verify(immersiveIcon)
        compare(immersiveIcon.width, 20)
        compare(immersiveIcon.height, 20)
        var transport = findChild(controls, "centerPlaybackControls")
        verify(transport)
        compare(transport.waveformPlacement, "beforePrevious")
        compare(transport.showWaveformMode, true)
    }

    function test_non_immersive_spectral_progress_uses_one_setting_in_all_themes() {
        SettingsController.playerShellMode = 0
        tryVerify(function() {
            return findControl("classicPlayerShell") !== null
        }, 1500)
        var waveform = findControl("mainWaveform")
        verify(waveform)
        var originalOpacity = SettingsController.frequencyColorWaveform.unplayedOpacity
        SettingsController.frequencyColorWaveform.unplayedOpacity = 0.60
        compare(Theme.nonImmersiveSpectralUnplayedOpacity, 0.60)
        SettingsController.frequencyColorWaveform.unplayedOpacity = 0.88
        compare(Theme.nonImmersiveSpectralUnplayedOpacity, 0.88)
        var expected = Theme.nonImmersiveSpectralUnplayedOpacity
        for (var theme = 0; theme < 3; ++theme) {
            SettingsController.themeMode = theme
            wait(0)
            compare(Theme.nonImmersiveSpectralUnplayedOpacity, expected)
            compare(waveform.frequencyUnplayedOpacity, expected)
        }
        SettingsController.frequencyColorWaveform.unplayedOpacity = 0.20
        SettingsController.themeMode = 0
        compare(Theme.nonImmersiveSpectralUnplayedOpacity, 0.20)
        SettingsController.themeMode = 1
        compare(Theme.nonImmersiveSpectralUnplayedOpacity, 0.20)
        compare(waveform.frequencyUnplayedOpacity, 0.20)
        SettingsController.frequencyColorWaveform.unplayedOpacity = originalOpacity
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
        var names = [
            "listWindowButton", "audioToolsButton", "equalizerButton",
            "waveformModeButton", "previousButton", "playPauseButton",
            "nextButton", "modeButton", "lyricsActionButton",
            "mainVolumeControl", "themeModeButton",
            "immersiveActionButton", "miniPlayerButton"
        ]
        var previousRight = -1
        var centerY = controls.height / 2 - 2
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
        var button = findChild(controls, "themeModeButton")
        var menu = findChild(controls, "playerShellMenu")
        verify(controls && button && menu)
        var playerWindow = controls.Window.window
        var previousWidth = playerWindow.width
        var previousHeight = playerWindow.height
        playerWindow.width = 863
        playerWindow.height = 266
        tryVerify(function() {
            var surface = controls.Window.window.contentItem
            var buttonPoint = button.mapToItem(surface, 0, 0)
            return Math.abs(surface.width - 863) <= 1
                    && controls.width <= surface.width + 1
                    && buttonPoint.x + button.width <= surface.width + 1
        }, 500, "the compact player layout must settle inside its window")
        compare(menu.parent, controls.Window.window.contentItem)
        for (var index = 0; index < 2; ++index) {
            var dpr = index === 0 ? 1.0 : 1.5
            var popupWindow = controls.Window.window
            verify(popupWindow)
            controls.popupDevicePixelRatioOverrideForTesting = dpr
            compare(controls.themePopupDevicePixelRatio, dpr)
            // This case verifies popup anchoring, not pointer delivery. Emit
            // the same public clicked signal used by a real ToolButton click;
            // synthetic offscreen mouse events intermittently miss controls
            // while a preceding window-resize polish is still being applied.
            button.clicked()
            tryVerify(function() { return menu.visible && menu.height > 0 }, 500)
            var point = controls.themePopupPositionForDpr(dpr)
            verify(Math.abs(point.x * dpr - Math.round(point.x * dpr)) < 0.01)
            verify(Math.abs(point.y * dpr - Math.round(point.y * dpr)) < 0.01)
            var mappedButton = button.mapToItem(popupWindow.contentItem, 0, 0)
            var mappedMenu = menu.parent.mapToItem(popupWindow.contentItem,
                                                   menu.x, menu.y)
            verify(mappedMenu.y + menu.height <= mappedButton.y + 1 / dpr,
                   "popup must remain above the invoking icon at DPR " + dpr
                   + "; menuY=" + mappedMenu.y
                   + "; menuHeight=" + menu.height
                   + "; buttonY=" + mappedButton.y)
            var buttonCenter = mappedButton.x + button.width / 2
            verify(mappedMenu.x <= buttonCenter
                   && buttonCenter <= mappedMenu.x + menu.width,
                   "popup must remain horizontally anchored to the icon at DPR "
                   + dpr + "; menuX=" + mappedMenu.x
                   + "; menuWidth=" + menu.width
                   + "; buttonCenter=" + buttonCenter)
            verify(mappedMenu.x >= 0)
            verify(mappedMenu.x + menu.width
                   <= popupWindow.contentItem.width + 1 / dpr,
                   "popup right " + (mappedMenu.x + menu.width)
                   + " (x=" + mappedMenu.x + ", width=" + menu.width
                   + ") exceeds " + popupWindow.contentItem.width
                   + " at DPR " + dpr
                   + "; controls=" + controls.width)
            verify(mappedMenu.y >= 0)
            verify(mappedMenu.y + menu.height
                   <= popupWindow.contentItem.height + 1 / dpr)
            menu.close()
            wait(0)
        }
        controls.popupDevicePixelRatioOverrideForTesting = 0
        playerWindow.width = previousWidth
        playerWindow.height = previousHeight
    }
}
