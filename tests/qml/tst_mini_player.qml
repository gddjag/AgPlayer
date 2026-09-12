import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MiniPlayerState"
    when: windowShown

    property var mainPlayer: null
    property var miniPlayer: null

    Component {
        id: floatingControlsWindowComponent
        Window {
            width: 420
            height: 880
            visible: true
            property alias panel: floatingPanel
            ImmersiveControlPanel {
                id: floatingPanel
                x: 30
                y: 20
                width: 360
            }
        }
    }

    function test_visual_eq_enabled_real_click_preserves_gains() {
        var savedEnabled = PlayerExperienceController.visualEqEnabled.slice()
        var savedGains = PlayerExperienceController.visualEqGains.slice()
        var host = createTemporaryObject(floatingControlsWindowComponent, testCase)
        verify(host)
        try {
            PlayerExperienceController.visualEqEnabled = [true,true,true,true,true,true,true,true]
            PlayerExperienceController.visualEqGains = [21,32,43,54,65,76,87,98]
            var gains = PlayerExperienceController.visualEqGains.slice()
            host.requestActivate()
            var panel = host.panel
            verify(waitForRendering(panel, 3000))
            tryVerify(function() { return Math.abs(panel.height
                - Math.min(panel.expandedHeight, panel.parent.height - 108)) < .5 })
            var tab = findChild(panel, "immersiveDynamicsTab")
            verify(tab)
            mouseClick(tab, tab.width / 2, tab.height / 2, Qt.LeftButton)
            tryCompare(panel, "currentTab", 2)
            var scroll = findChild(panel, "immersivePanelScroll")
            verify(scroll && scroll.contentItem)
            tryVerify(function() { return scroll.contentItem.contentHeight > scroll.contentItem.height })
            var bands = [0, 7]
            for (var i = 0; i < bands.length; ++i) {
                var band = bands[i]
                var toggle = findChild(panel, "visualEqEnabled_" + band)
                verify(toggle, "Missing visual EQ enable control " + band)
                tryCompare(toggle, "checked", true)
                var flick = scroll.contentItem
                var rowY = toggle.mapToItem(flick.contentItem, 0, 0).y
                flick.contentY = Math.max(0, Math.min(rowY - 70,
                                                    flick.contentHeight - flick.height))
                wait(50)
                var point = toggle.mapToItem(scroll, toggle.width / 2, toggle.height / 2)
                verify(point.y > 0 && point.y < scroll.height, "Toggle must be inside viewport")
                mouseClick(toggle, toggle.width / 2, toggle.height / 2, Qt.LeftButton)
                tryVerify(function() { return PlayerExperienceController.visualEqEnabled[band] === false })
                tryCompare(toggle, "checked", false)
                for (var other = 0; other < 8; ++other) {
                    compare(PlayerExperienceController.visualEqGains[other], gains[other])
                    compare(PlayerExperienceController.visualEqEnabled[other], other !== band)
                }
                // External list replacement must still update the binding
                // after a real user toggle; then click again to verify routing.
                PlayerExperienceController.visualEqEnabled = [true,true,true,true,true,true,true,true]
                tryCompare(toggle, "checked", true)
                mouseClick(toggle, toggle.width / 2, toggle.height / 2, Qt.LeftButton)
                tryVerify(function() { return PlayerExperienceController.visualEqEnabled[band] === false })
                PlayerExperienceController.visualEqEnabled = [true,true,true,true,true,true,true,true]
            }
            for (var gainIndex = 0; gainIndex < 8; ++gainIndex)
                compare(PlayerExperienceController.visualEqGains[gainIndex], gains[gainIndex])
        } finally {
            PlayerExperienceController.visualEqEnabled = savedEnabled
            PlayerExperienceController.visualEqGains = savedGains
            host.close()
        }
    }

    function test_floating_controls_display_and_real_input() {
        var keys = ["floatingBlockMinSize", "floatingBlockMaxSize",
                    "floatingBlockSpeed", "floatingBlockIntensity"]
        var saved = {}
        var initial = [20, 80, 35, 60]
        for (var i = 0; i < keys.length; ++i)
            saved[keys[i]] = PlayerExperienceController[keys[i]]
        var host = createTemporaryObject(floatingControlsWindowComponent, testCase)
        verify(host)
        try {
            host.requestActivate()
            var panel = host.panel
            var tab = findChild(panel, "immersiveDynamicsTab")
            verify(tab)
            // visible=true is asynchronous: the new native window and its
            // RowLayout must be exposed/polished before pointer coordinates
            // are meaningful (especially with the software/offscreen backend).
            verify(waitForRendering(panel, 3000))
            tryVerify(function() { return tab.visible && tab.width > 0 && tab.height > 0 })
            tryVerify(function() { return Math.abs(panel.height
                - Math.min(panel.expandedHeight, panel.parent.height - 108)) < .5 })
            mouseClick(tab, tab.width / 2, tab.height / 2, Qt.LeftButton)
            tryCompare(panel, "currentTab", 2)
            var scroll = findChild(panel, "immersivePanelScroll")
            verify(scroll && scroll.contentItem)
            tryVerify(function() { return scroll.contentItem.contentHeight > scroll.contentItem.height })
            for (var index = 0; index < keys.length; ++index) {
                var key = keys[index]
                PlayerExperienceController[key] = initial[index]
                var slider = findChild(panel, "dynamicSlider_" + key)
                var label = findChild(panel, "dynamicValue_" + key)
                verify(slider && label, "Missing floating control " + key)
                tryCompare(slider, "value", initial[index])
                tryCompare(label, "text", initial[index].toString())
                var flick = scroll.contentItem
                var rowY = slider.parent.mapToItem(flick.contentItem, 0, 0).y
                flick.contentY = Math.max(0, Math.min(rowY - 70,
                                                    flick.contentHeight - flick.height))
                wait(30)
                var point = slider.mapToItem(scroll, slider.width / 2, slider.height / 2)
                verify(point.y > 0 && point.y < scroll.height,
                       "Floating slider must be inside the clipped viewport: " + key)
                // Click the actual handle to establish focus, then send real
                // keys. Do not assign slider.value or invoke moved().
                var handle = slider.handle
                mouseClick(slider, handle.x + handle.width / 2,
                           handle.y + handle.height / 2)
                tryCompare(slider, "activeFocus", true)
                var beforeKey = PlayerExperienceController[key]
                keyClick(Qt.Key_Right)
                tryVerify(function() { return PlayerExperienceController[key] > beforeKey })
                tryCompare(label, "text", Math.round(PlayerExperienceController[key]).toString())
                var beforeDrag = PlayerExperienceController[key]
                mouseDrag(slider, slider.handle.x + slider.handle.width / 2,
                          slider.height / 2, -slider.availableWidth * .12, 0,
                          Qt.LeftButton)
                tryVerify(function() { return PlayerExperienceController[key] < beforeDrag }, 1000,
                          "Real drag must update controller: " + key)
                tryCompare(label, "text", Math.round(PlayerExperienceController[key]).toString())
                // A subsequent external update must still reach the control;
                // interaction must not replace its controller binding.
                PlayerExperienceController[key] = initial[index] + 2
                tryCompare(slider, "value", initial[index] + 2)
                tryCompare(label, "text", (initial[index] + 2).toString())
            }
            // Optional test-harness context property, never a production CLI.
            if (typeof testFloatingControlsScreenshotPath !== "undefined"
                    && testFloatingControlsScreenshotPath.length > 0) {
                var group = findChild(panel, "dynamicsFloatingGroup")
                verify(group)
                var view = scroll.contentItem
                var groupY = group.mapToItem(view.contentItem, 0, 0).y
                view.contentY = Math.max(0, Math.min(groupY - 20,
                                                   view.contentHeight - view.height))
                wait(50)
                var capture = grabImage(panel)
                verify(capture.width > 0 && capture.height > 0)
                capture.save(testFloatingControlsScreenshotPath)
                console.log("Floating controls screenshot:", testFloatingControlsScreenshotPath)
            }
        } finally {
            for (var restore = 0; restore < keys.length; ++restore)
                PlayerExperienceController[keys[restore]] = saved[keys[restore]]
            host.close()
        }
    }

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

    function test_immersive_exit_preserves_rolling_audio_analysis_data() {
        return [{tag: "rolling", shell: 2}, {tag: "classic", shell: 0},
                {tag: "integrated", shell: 1}]
    }

    function test_immersive_exit_preserves_rolling_audio_analysis(data) {
        var savedShell = SettingsController.playerShellMode
        var savedRendering = mainPlayer.immersiveRenderingEnabled
        var coordinator = findChild(mainPlayer, "immersiveCoordinator")
        try {
            SettingsController.playerShellMode = data.shell
            var ids = transportTestSetup.prepareTransportQueue(30)
            compare(ids.length, 3)
            PlaybackController.play()
            tryCompare(PlaybackController, "state", PlaybackController.Playing)
            mainPlayer.immersiveRenderingEnabled = true
            PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
            PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
            tryVerify(function() { return coordinator.surface && coordinator.surface.terrainItem }, 3000)
            var terrain = coordinator.surface.terrainItem
            tryVerify(function() { return terrain.renderStatus === TerrainReactorItem.Ready
                         || terrain.renderStatus === TerrainReactorItem.SoftwareBackend }, 10000)
            if (terrain.renderStatus === TerrainReactorItem.SoftwareBackend) {
                skip("Requires native GPU for real immersive lifecycle")
                return
            }
            tryVerify(function() { return terrain.frameCount > 0 }, 3000)
            PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
            tryCompare(coordinator, "handoffPhase", 0, 3000)
            wait(100)
            compare(AudioVisualFeatureController.active, data.shell === 2,
                    "Immersive teardown must preserve only the rolling consumer")
            if (data.shell === 2) {
                var before = AudioVisualFeatureController.visualSpectrumUpdateCount
                tryVerify(function() { return AudioVisualFeatureController.visualSpectrumUpdateCount > before }, 1500,
                          "Real PCM analysis must continue after immersive renderer detaches")
            }
            SettingsController.playerShellMode = 0
            tryCompare(AudioVisualFeatureController, "active", false)
        } finally {
            PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
            PlaybackController.stop()
            mainPlayer.immersiveRenderingEnabled = savedRendering
            SettingsController.playerShellMode = savedShell
        }
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

    function test_mini_player_control_order_omits_theme_and_starts_with_waveform() {
        var controls = findChild(miniPlayer, "miniPlayerControls")
        verify(controls)
        verifyAscendingX(controls, [
            "miniWaveformModeButton", "miniPreviousButton", "miniPlayPauseButton",
            "miniNextButton", "miniModeButton", "miniMuteButton"
        ])
        compare(findChild(controls, "miniThemeModeButton"), null)
        compare(findChild(controls, "miniPlayerShellMenu"), null)
        verify(findChild(controls, "lyricsActionButton") === null)
        verify(findChild(controls, "immersiveActionButton") === null)
        compare(findChild(controls, "experienceActions"), null,
                "mini must not instantiate a hidden UI registry")
    }

    function test_waveform_data_visibility() {
        var controls = findChild(miniPlayer, "miniPlayerControls")
        var waveform = findChild(miniPlayer, "miniWaveform")
        var session = controls.waveformSession
        var savedLayers = session.layers
        var savedMode = SettingsController.waveformMode
        try {
            SettingsController.waveformMode = 0
            session.layers = {mix: [0.2, 0.8, 0.4]}
            tryCompare(waveform, "layers", session.layers)
            miniPlayer.visible = false
            tryVerify(function() { return !waveform.layers.mix })
            session.layers = {mix: [0.9, 0.3]}
            verify(!waveform.layers.mix)
            miniPlayer.visible = true
            tryCompare(waveform, "layers", session.layers)
            miniPlayer.showMinimized()
            tryVerify(function() { return !waveform.layers.mix })
            miniPlayer.showNormal()
            tryCompare(waveform, "layers", session.layers)
        } finally {
            miniPlayer.showNormal()
            miniPlayer.visible = true
            session.layers = savedLayers
            SettingsController.waveformMode = savedMode
        }
    }

    function test_mini_waveform_is_clipped_to_its_container() {
        var controls = findChild(miniPlayer, "miniPlayerControls")
        var container = findChild(controls, "miniWaveformContainer")
        var sharedView = findChild(controls, "miniFullTrackWaveform")
        var waveform = findChild(controls, "miniWaveform")
        verify(container && sharedView && waveform)
        compare(container.clip, true)
        compare(sharedView.width, container.width)
        compare(waveform.width, sharedView.width)
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

    function test_frequency_waveform_uses_smooth_three_band_progress_overlay() {
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
        compare(String(waveform.lowColor), String(frequencySettings.lowColor))
        compare(String(waveform.midColor), String(frequencySettings.midColor))
        compare(String(waveform.highColor), String(frequencySettings.highColor))
        compare(waveform.frequencyUnplayedOpacity,
                Theme.nonImmersiveSpectralUnplayedOpacity)
        compare(waveform.position, 0,
                "the mini base frequency pass must remain entirely unplayed")
        compare(clip.visible, true,
                "frequency progress must use the precise clipped overlay")
        compare(findChild(miniPlayer, "miniPlayedWaveform").position,
                findChild(miniPlayer, "miniPlayedWaveform").duration)
        tryVerify(function() {
            return Math.abs(clip.width - waveform.waveformCursorX) <= 0.5
        })
        SettingsController.waveformPlaybackGuide = true
        compare(guide.visible, true,
                "the shared playback-guide setting must apply in frequency mode")
        SettingsController.waveformMode = 1
        tryCompare(guide, "visible", true)
        SettingsController.waveformMode = 0
        tryCompare(clip, "visible", true)
        tryCompare(guide, "visible", true)
        SettingsController.waveformMode = previousMode
        SettingsController.waveformPlaybackGuide = previousGuide
    }

    function test_mini_waveform_restores_hover_time_capsule() {
        var previousPreview = SettingsController.waveformHoverTimePreview
        SettingsController.waveformHoverTimePreview = true
        var waveform = findChild(miniPlayer, "miniWaveform")
        var interaction = findChild(miniPlayer, "miniWaveformInteractionSurface")
        var capsule = findChild(miniPlayer, "miniWaveformHoverTimeCapsule")
        verify(waveform && interaction && capsule)
        interaction.updatePreviewAt(interaction.width * 0.75)
        tryCompare(capsule, "visible", true)
        compare(waveform.hoverPosition,
                waveform.timeForX(interaction.width * 0.75))
        SettingsController.waveformHoverTimePreview = previousPreview
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
        compare(favorite.parent, title.parent)
        verify(Math.abs(favorite.x - (title.x + title.width) - 6) <= 1,
               "favorite must immediately follow the content-sized title")
        verify(title.width <= title.implicitWidth + 1)
        verify(!tags.visible,
               "empty tags must be omitted instead of showing placeholder text")
        compare(rating.spacing, 1)
        compare(firstStar.width, firstStar.sourceSize.width)
        compare(firstStar.height, firstStar.sourceSize.height)
        compare(favorite.width, 16)
        compare(favorite.height, 16)
        compare(Math.round(title.mapToItem(metadataRow, 0,
                                          title.height / 2).y),
                Math.round(favorite.mapToItem(metadataRow, 0,
                                              favorite.height / 2).y),
                "title and favorite must share one visual centerline")
        compare(favorite.icon.width, 16)
        compare(favorite.icon.height, 16)
        compare(findChild(miniPlayer, "miniElapsedTime").font.pixelSize,
                Theme.fontSizeCaption)
        compare(findChild(miniPlayer, "miniDurationTime").font.pixelSize,
                Theme.fontSizeCaption)
        compare(play.width, 34)
        compare(play.height, 34)
        tryCompare(slider, "visible", false, 300)
        compare(slider.width, 60)
        compare(slider.handle.width, Theme.sliderHandleExtent)
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
