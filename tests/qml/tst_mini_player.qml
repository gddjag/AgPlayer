import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MiniPlayerState"
    when: windowShown

    property var mainPlayer: null
    property var miniPlayer: null

    // Fake playback controller that mirrors the production PlaybackController API
    // surface (properties + Q_INVOKABLE functions) so MiniPlayerControls bindings
    // resolve without touching real audio. Only positionMs, durationMs, playing
    // and pauseCalls are assertion-relevant; the rest exist to keep bindings
    // warning-free under test.
    QtObject {
        id: playbackFake
        property int positionMs: 0
        property int durationMs: 0
        property int pauseCalls: 0
        property bool playing: true

        // Mirror of PlaybackController API consumed by MiniPlayerControls.
        property int state: PlaybackController.Playing
        property int mode: PlaybackController.Sequential
        property real volume: 1.0
        property bool muted: false
        property int trackIndex: -1
        property int trackCount: 0
        property string currentTrackId: ""
        property string errorMessage: ""

        function publishPlaying(position, duration) {
            positionMs = position
            durationMs = duration
            playing = true
            state = PlaybackController.Playing
        }
        function togglePlayback() {
            if (playing) pauseCalls += 1
            playing = !playing
            state = playing ? PlaybackController.Playing : PlaybackController.Paused
        }
        function play() {}
        function pause() {}
        function seek(position) {}
        function next() {}
        function previous() {}
        function setVolume(v) { volume = v }
        function toggleMuted() { muted = !muted }
        function cycleMode() {}
        function playRow(row) {}
        function toggleFavorite() {}
        function toggleFavoriteRow(row) {}
    }

    // Fake WindowController recording real actions instead of mutating real windows.
    QtObject {
        id: windowController
        property bool alwaysOnTop: false
        property bool mainVisible: false
        property bool miniVisible: true
        property int closeCalls: 0
        function setAlwaysOnTop(value) { alwaysOnTop = value }
        function showMain() { mainVisible = true; miniVisible = false }
        function showMini() { miniVisible = true; mainVisible = false }
        function requestClose() { closeCalls += 1 }
    }

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined", "testMainWindow context property should exist")
        verify(typeof testMiniWindow !== "undefined", "testMiniWindow context property should exist")
        mainPlayer = testMainWindow
        miniPlayer = testMiniWindow
        verify(mainPlayer, "Failed to create Main window")
        verify(miniPlayer, "Failed to create Mini player window")

        // Override playback/windows so both windows share the same fake state
        // source without spinning up a real ag_player core or audio device.
        mainPlayer.playback = playbackFake
        miniPlayer.playback = playbackFake
        miniPlayer.windows = windowController
        miniPlayer.visible = true
        wait(50)
    }

    function cleanupTestCase() {
        mainPlayer = null
        miniPlayer = null
    }

    function verifyAscendingX(parent, names) {
        var previousX = -1
        for (var i = 0; i < names.length; ++i) {
            var item = findChild(parent, names[i])
            verify(item !== null, "missing " + names[i])
            verify(item.visible, names[i] + " must be visible")
            var x = item.mapToItem(parent, 0, 0).x
            verify(x > previousX, names[i] + " is out of order")
            previousX = x
        }
    }

    function test_mini_player_control_order_starts_with_theme_then_waveform() {
        var controls = findChild(miniPlayer, "miniPlayerControls")
        verify(controls)
        verifyAscendingX(controls, [
            "miniThemeModeButton", "miniWaveformModeButton",
            "miniPreviousButton", "miniPlayPauseButton",
            "miniNextButton", "miniModeButton", "miniMuteButton"
        ])
        verify(findChild(controls, "lyricsActionButton") === null)
        verify(findChild(controls, "immersiveActionButton") === null)
        compare(findChild(controls, "experienceActions"), null,
                "mini must not instantiate a hidden UI registry")
    }

    function test_mini_theme_popup_opens_above_icon_at_real_dpr_positions() {
        var controls = findChild(miniPlayer, "miniPlayerControls")
        var button = findChild(controls, "miniThemeModeButton")
        var menu = findChild(controls, "miniPlayerShellMenu")
        verify(controls && button && menu)
        compare(menu.parent, controls.Window.window.contentItem)
        for (var index = 0; index < 2; ++index) {
            var dpr = index === 0 ? 1.0 : 1.5
            controls.popupDevicePixelRatioOverrideForTesting = dpr
            mouseClick(button)
            tryVerify(function() { return menu.visible && menu.height > 0 }, 500)
            var point = controls.themePopupPositionForDpr(dpr)
            verify(isFinite(point.x) && isFinite(point.y))
            verify(Math.abs(point.x * dpr - Math.round(point.x * dpr)) < 0.01)
            verify(Math.abs(point.y * dpr - Math.round(point.y * dpr)) < 0.01)
            var surface = controls.Window.window.contentItem
            var buttonPoint = button.mapToItem(surface, 0, 0)
            var menuPoint = menu.parent.mapToItem(surface, menu.x, menu.y)
            verify(menuPoint.y + menu.height <= buttonPoint.y + 1 / dpr)
            var buttonCenter = buttonPoint.x + button.width / 2
            verify(menuPoint.x <= buttonCenter
                   && buttonCenter <= menuPoint.x + menu.width)
            verify(menuPoint.x >= 0)
            verify(menuPoint.x + menu.width <= surface.width + 1 / dpr)
            menu.close()
            wait(0)
        }
        controls.popupDevicePixelRatioOverrideForTesting = 0
    }

    function test_mini_waveform_is_clipped_to_its_container() {
        var controls = findChild(miniPlayer, "miniPlayerControls")
        var container = findChild(controls, "miniWaveformContainer")
        var waveform = findChild(controls, "miniWaveform")
        verify(container && waveform)
        compare(container.clip, true)
        verify(waveform.x >= 0)
        verify(waveform.x + waveform.width <= container.width + 0.5)
    }

    function test_mini_and_main_share_state() {
        playbackFake.publishPlaying(25000, 286000)
        compare(findChild(miniPlayer, "miniPlayButtonBody").border.color.toString(),
                Theme.playRingPlaying.toString())
        compare(mainPlayer.positionMs, 25000, "main window should mirror shared playback position")
        compare(miniPlayer.positionMs, 25000, "mini window should mirror shared playback position")
        mouseClick(miniPlayer.playPauseButton)
        compare(playbackFake.pauseCalls, 1, "clicking mini playPause should call playback.togglePlayback once")
        compare(findChild(miniPlayer, "miniPlayButtonBody").border.color.toString(),
                Theme.playRingPaused.toString())
        compare(findChild(miniPlayer, "miniPlayButtonBody").border.width, 3)
    }

    function test_windows_keep_a_safe_position_when_playback_is_released() {
        miniPlayer.playback = null
        mainPlayer.playback = null
        compare(miniPlayer.positionMs, 0)
        compare(mainPlayer.positionMs, 0)
        miniPlayer.playback = playbackFake
        mainPlayer.playback = playbackFake
    }

    function test_mini_waveform_prefers_exact_decoded_duration() {
        var controls = findChild(miniPlayer, "miniPlayerControls")
        verify(controls)
        playbackFake.durationMs = 307000
        controls.playback.durationMs = 240000
        controls.waveformDurationMs = 301250
        compare(controls.effectiveDurationMs, 301250)
        controls.waveformDurationMs = 0
        compare(controls.effectiveDurationMs, 240000)
    }

    function test_mini_waveform_uses_precise_playback_clip() {
        playbackFake.publishPlaying(150000, 300000)
        var waveform = findChild(miniPlayer, "miniWaveform")
        var clip = findChild(miniPlayer, "miniWaveformPlayedClip")
        var guide = findChild(miniPlayer, "miniWaveformPlaybackGuide")
        verify(waveform && clip && guide)
        tryVerify(function() {
            return Math.abs(clip.width - waveform.waveformCursorX) <= 0.5
                    && Math.abs(guide.x - waveform.waveformCursorX) <= 0.5
        })
    }

    function test_spectral_waveform_uses_one_palette_render_pass() {
        var previousMode = SettingsController.waveformMode
        var previousGuide = SettingsController.waveformPlaybackGuide
        var waveform = findChild(miniPlayer, "miniWaveform")
        var clip = findChild(miniPlayer, "miniWaveformPlayedClip")
        var guide = findChild(miniPlayer, "miniWaveformPlaybackGuide")
        var controls = findChild(miniPlayer, "miniPlayerControls")
        var session = controls ? controls.waveformSession : null
        var frequencySettings = SettingsController.frequencyColorWaveform
        verify(waveform && clip && guide && session)
        compare(findChild(miniPlayer, "miniWaveformPlaybackFocusDot"), null,
                "frequency focus must be rendered by the native canvas")

        SettingsController.waveformMode = 3
        tryCompare(waveform, "visualMode", 3)
        compare(waveform.spectralPalette.length, 8)
        compare(String(waveform.spectralPalette[0]),
                String(frequencySettings.palette[0]))
        compare(String(waveform.spectralPalette[7]),
                String(frequencySettings.palette[7]))
        for (var theme = 0; theme < 3; ++theme) {
            SettingsController.themeMode = theme
            wait(0)
            compare(waveform.spectralUnplayedOpacity,
                    Theme.nonImmersiveSpectralUnplayedOpacity)
        }
        compare(clip.visible, false,
                "frequency overlays must not be drawn twice in the played region")
        SettingsController.waveformPlaybackGuide = true
        compare(guide.visible, false,
                "frequency mode must hide the legacy QML guide even when enabled")
        SettingsController.waveformMode = 1
        tryCompare(guide, "visible", true)
        SettingsController.waveformMode = 0
        tryCompare(clip, "visible", true)
        tryCompare(guide, "visible", true)
        SettingsController.waveformMode = previousMode
        SettingsController.waveformPlaybackGuide = previousGuide
    }

    function test_mini_player_can_cycle_the_shared_waveform_mode() {
        var button = findChild(miniPlayer, "miniWaveformModeButton")
        verify(button)
        compare(button.icon.source.toString().endsWith("/waveform-switch.svg"), true)
        compare(button.icon.width, 16)
        compare(button.icon.height, 16)
        var previousMode = SettingsController.waveformMode
        mouseClick(button)
        var expectedMode = previousMode === 0 ? 3
                         : previousMode === 3 ? 1
                         : previousMode === 1 ? 2 : 0
        compare(SettingsController.waveformMode,
                expectedMode)
        SettingsController.waveformMode = previousMode
    }

    function test_pin_and_restore_are_real_actions() {
        mouseClick(miniPlayer.pinButton)
        compare(windowController.alwaysOnTop, true, "pin button should call windows.setAlwaysOnTop(true)")
        mouseClick(miniPlayer.restoreButton)
        compare(windowController.mainVisible, true, "restore button should call windows.showMain() -> mainVisible=true")
        compare(windowController.miniVisible, false, "restore button should call windows.showMain() -> miniVisible=false")
    }

    function test_reference_layout_and_scalable_window() {
        compare(miniPlayer.width, 588)
        compare(miniPlayer.height, 186)
        compare(miniPlayer.minimumWidth, 588)
        compare(miniPlayer.minimumHeight, 186)
        verify(findChild(miniPlayer, "miniCover"))
        verify(findChild(miniPlayer, "miniTrackTitle"))
        var volume = findChild(miniPlayer, "miniVolumeSlider")
        verify(volume)
        var flyout = findChild(miniPlayer, "miniVolumeFlyout")
        verify(flyout)
        var closeTimer = findChild(miniPlayer, "miniVolumeCloseTimer")
        verify(closeTimer, "volume flyout must expose its delayed close timer")
        compare(closeTimer.interval, 2000)
        flyout.parent.expanded = true
        tryVerify(function() {
            var left = flyout.parent.x + flyout.x
            return left >= 0 && left + flyout.width <= miniPlayer.width
        }, 300, "expanded mini volume flyout must remain inside the window canvas")
        tryVerify(function() {
            return flyout.x >= 28
                    && flyout.x + flyout.width === flyout.parent.width
        }, 300, "mini volume flyout must expand to the right of its mute button")
        closeTimer.restart()
        wait(1600)
        verify(flyout.parent.expanded,
               "volume flyout must stay open for the two-second pointer transfer")
        flyout.parent.expanded = false
        verify(findChild(miniPlayer, "miniRating"))
        verify(findChild(miniPlayer, "miniWaveform"))
        verify(findChild(miniPlayer, "miniTransport"))
        var play = miniPlayer.playPauseButton
        var slider = findChild(miniPlayer, "miniVolumeSlider")
        var percent = findChild(miniPlayer, "miniVolumePercent")
        verify(play)
        verify(slider)
        verify(percent)
        var metadataRow = findChild(miniPlayer, "miniMetadataRow")
        var title = findChild(miniPlayer, "miniTrackTitle")
        var artist = findChild(miniPlayer, "miniArtist")
        var album = findChild(miniPlayer, "miniAlbum")
        var tags = findChild(miniPlayer, "miniTags")
        var rating = findChild(miniPlayer, "miniRating")
        var favorite = findChild(miniPlayer, "miniFavoriteButton")
        verify(metadataRow && title && artist && album && tags && rating && favorite)
        var firstStar = findChild(rating, "miniRatingStar-0")
        verify(firstStar, "rating stars must expose their native rendered item")
        verify(metadataRow.y >= title.y + title.height,
               "metadata must stay below the title")
        compare(metadataRow.height, 26,
                "metadata must use a compact single-row height")
        compare(artist.wrapMode, Text.NoWrap)
        compare(album.wrapMode, Text.NoWrap)
        compare(tags.wrapMode, Text.NoWrap)
        verify(artist.width <= artist.implicitWidth + 1,
               "artist must not stretch and push later metadata right")
        verify(album.width <= album.implicitWidth + 1,
               "album must not stretch and push later metadata right")
        if (tags.visible)
            verify(tags.width <= tags.implicitWidth + 1,
                   "tags must not stretch and push rating/favorite right")
        verify(Math.abs(artist.mapToItem(metadataRow, 0, artist.height / 2).y
                        - album.mapToItem(metadataRow, 0, album.height / 2).y) <= 1,
               "artist and album must share one baseline")
        verify(rating.x >= artist.x + artist.width,
               "rating must follow the single metadata text run")
        verify(favorite.x >= rating.x + rating.width,
               "favorite must immediately follow rating in the metadata row")
        verify(!tags.visible,
               "empty tags must be omitted instead of showing placeholder text")
        compare(rating.spacing, 1)
        compare(firstStar.width, firstStar.sourceSize.width)
        compare(firstStar.height, firstStar.sourceSize.height)
        compare(favorite.width, 16)
        compare(favorite.height, 16)
        compare(Math.round(firstStar.mapToItem(metadataRow, 0,
                                               firstStar.height / 2).y),
                Math.round(favorite.mapToItem(metadataRow, 0,
                                              favorite.height / 2).y),
                "stars and favorite must share one visual centerline")
        compare(favorite.icon.width, 16)
        compare(favorite.icon.height, 16)
        compare(findChild(miniPlayer, "miniElapsedTime").font.pixelSize, 11)
        compare(findChild(miniPlayer, "miniDurationTime").font.pixelSize, 11)
        compare(play.width, 34)
        compare(play.height, 34)
        tryCompare(slider, "visible", false, 300)
        compare(slider.width, 60)
        verify(slider.handle.width <= 8)
        playbackFake.setVolume(0.37)
        tryCompare(percent, "text", "37%")
    }

    function test_mode_icons_match_main_player_and_follow_system_foreground() {
        var button = findChild(miniPlayer, "miniModeButton")
        verify(button)
        var cases = [
            { mode: PlaybackController.Sequential, icon: "/play-order-line.svg" },
            { mode: PlaybackController.Shuffle, icon: "/shuffle-arrows-line.svg" },
            { mode: PlaybackController.RepeatOne, icon: "/repeat-one-line-alt.svg" },
            { mode: PlaybackController.RepeatAll, icon: "/repeat-list-line.svg" }
        ]

        for (var index = 0; index < cases.length; ++index) {
            playbackFake.mode = cases[index].mode
            verify(button.icon.source.toString().endsWith(cases[index].icon),
                   "mini player must share the four playback mode icons")
            compare(button.icon.color.toString(),
                    Theme.iconPrimary.toString())
        }
        playbackFake.mode = PlaybackController.Sequential
        compare(button.icon.width, 16)
        compare(button.icon.height, 16)
        compare(findChild(miniPlayer, "miniMuteButton").icon.width, 16)
    }

    function test_mini_metadata_refreshes_when_current_track_tags_change() {
        var artist = findChild(miniPlayer, "miniArtist")
        var album = findChild(miniPlayer, "miniAlbum")
        var tags = findChild(miniPlayer, "miniTags")
        verify(artist && album && tags)
        playbackFake.currentTrackId = miniMetadataTrackId
        tryCompare(artist, "text", "Mini Artist")
        tryCompare(album, "text", "Mini Album")
        verify(!tags.visible)

        verify(LibraryModel.setTags(miniMetadataTrackId, ["现场", "测试"]))
        tryVerify(function() {
            return tags.visible && tags.text === "现场、测试"
        }, 1000, "mini player must react to tag edits on the playing track")
        LibraryModel.setTags(miniMetadataTrackId, [])
    }

    function test_titlebar_uses_thin_system_icons() {
        verify(miniPlayer.pinButton.icon.source.toString().endsWith("/pushpin-line.svg"))
        verify(miniPlayer.restoreButton.icon.source.toString().endsWith("/restore-line.svg"))
        compare(miniPlayer.pinButton.icon.width, 16)
        compare(miniPlayer.restoreButton.icon.width, 16)
        var close = findChild(miniPlayer, "miniCloseButton")
        var minimize = findChild(miniPlayer, "miniMinimizeButton")
        verify(close && minimize)
        verify(close.icon.source.toString().endsWith("/close-line.svg"))
        compare(close.icon.width, 16)
        compare(minimize.icon.width, 16)
    }

    function test_mini_spectrum_uses_same_fixed_bars_as_main() {
        var previousMode = SettingsController.waveformMode
        SettingsController.waveformMode = 2
        var waveform = findChild(miniPlayer, "miniWaveform")
        verify(waveform)
        tryCompare(waveform, "visualMode", 2)
        compare(waveform.lineWidth, 3)
        compare(waveform.spectrumBarCount, 128)
        compare(waveform.spectrumBarGap, 2)
        SettingsController.waveformMode = previousMode
    }

    function test_mini_volume_flyout_retracts_after_two_seconds_even_after_slider_focus() {
        var control = findChild(miniPlayer, "miniVolumeControl")
        var slider = findChild(miniPlayer, "miniVolumeSlider")
        var closeTimer = findChild(miniPlayer, "miniVolumeCloseTimer")
        verify(control && slider && closeTimer)
        control.expandedForQa = true
        slider.forceActiveFocus()
        closeTimer.restart()
        wait(1600)
        verify(control.expandedForQa)
        wait(550)
        tryVerify(function() { return !control.expandedForQa }, 300)
    }

    function test_native_close_routes_through_window_controller() {
        miniPlayer.visible = true
        var before = windowController.closeCalls
        miniPlayer.close()
        tryCompare(windowController, "closeCalls", before + 1)
        verify(miniPlayer.visible, "close event should be rejected until controller decides")
    }
}
