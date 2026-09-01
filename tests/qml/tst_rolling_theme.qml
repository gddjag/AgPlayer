import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "RollingTheme"
    when: windowShown

    property var mainWindow: null
    property int savedShellMode: 0
    property int savedThemeMode: 0
    property int savedWaveformMode: 0

    QtObject {
        id: fakePlayback
        property int positionMs: 60000
        property int durationMs: 120000
        property string currentTrackId: "rolling-track"
        property real speedRatio: 1.0
        property real sourceBpm: 120.0
        property real targetBpm: sourceBpm * speedRatio
        property bool keepPitch: true
        property bool scratchActive: false
        property bool scratchReady: false
        property bool scratchBuffering: false
        property int seekCount: 0
        property int playCount: 0
        property int beginCount: 0
        property int updateCount: 0
        property int endCount: 0
        property int cancelCount: 0
        property int resetCount: 0
        property int keepPitchCount: 0
        property real lastSeek: -1
        property real lastScratchRate: 0

        function reset() {
            positionMs = 60000
            durationMs = 120000
            speedRatio = 1.0
            sourceBpm = 120.0
            keepPitch = true
            scratchActive = false
            scratchReady = false
            scratchBuffering = false
            seekCount = 0
            playCount = 0
            beginCount = 0
            updateCount = 0
            endCount = 0
            cancelCount = 0
            resetCount = 0
            keepPitchCount = 0
            lastSeek = -1
            lastScratchRate = 0
        }
        function seek(value) {
            ++seekCount
            lastSeek = value
            positionMs = value
        }
        function play() { ++playCount }
        function beginScratch() {
            ++beginCount
            scratchActive = true
            return true
        }
        function updateScratch(value) {
            ++updateCount
            lastScratchRate = value
            return true
        }
        function endScratch() {
            ++endCount
            scratchActive = false
            return true
        }
        function cancelScratch() {
            ++cancelCount
            scratchActive = false
            return true
        }
        function setSpeedRatio(value) { speedRatio = value }
        function setTargetBpm(value) { speedRatio = value / sourceBpm }
        function resetTempo() { ++resetCount; speedRatio = 1.0 }
        function setKeepPitch(value) {
            ++keepPitchCount
            keepPitch = value
        }
    }

    QtObject {
        id: fakeWaveformSession
        property real durationMs: 120000
        property bool frequencyReady: true
        property int libraryRevision: 0
        property var layers: ({
            "mix": [0.1, 0.6, 0.3, 0.8, 0.2],
            "low": [0.2, 0.5, 0.1, 0.7, 0.2],
            "mid": [0.1, 0.4, 0.3, 0.6, 0.1],
            "high": [0.05, 0.3, 0.2, 0.5, 0.1],
            "_sampleRate": 48000,
            "_totalSamples": 5760000,
            "_peakCount": 5
        })
    }

    QtObject {
        id: fakeVisualFeatures
        property real leftPeak: 0.82
        property real rightPeak: 0.64
        property real leftRms: 0.46
        property real rightRms: 0.34
    }

    QtObject {
        id: fakeLyricsService
        property bool enabled: true
        property int status: LyricsService.Ready
        property string previousLine: "上一句共享歌词"
        property string currentLine: "当前共享歌词"
        property string nextLine: "下一句共享歌词"
        property int offsetMs: 0
        function retry() {}
        function pauseFollow(milliseconds) {}
        function importLrc(url) { return true }
    }

    QtObject {
        id: fakeLibrary
        property int count: 1
        property int lookupCount: 0
        signal dataChanged()
        function trackForId(trackId) {
            ++lookupCount
            return {
                "title": "光辉岁月 Psy techno DJ CHEN REMIX",
                "artist": "测试艺术家",
                "album": "测试专辑",
                "coverUrl": "",
                "favorite": true,
                "rating": 5,
                "format": "wav",
                "bitDepth": 24,
                "sampleRate": 48000,
                "bitRate": 2116000,
                "bpm": 120,
                "fileSize": 36175872
            }
        }
        function indexForTrackId(trackId) { return 0 }
        function setFavorite(row, value) {}
    }

    function shell(name) {
        return findChild(mainWindow, name)
    }

    function enterMode(mode) {
        SettingsController.playerShellMode = mode
        var expected = mode === 2 ? "rollingPlayerShell"
                     : mode === 1 ? "integratedPlayerShell"
                                  : "classicPlayerShell"
        tryVerify(function() { return shell(expected) !== null }, 1500)
        return shell(expected)
    }

    function rollingWithFakes() {
        var rolling = enterMode(2)
        verify(rolling.libraryModel !== undefined)
        verify(rolling.visualFeatures !== undefined)
        fakePlayback.reset()
        fakeLibrary.lookupCount = 0
        rolling.playback = fakePlayback
        rolling.waveformSession = fakeWaveformSession
        rolling.libraryModel = fakeLibrary
        rolling.visualFeatures = fakeVisualFeatures
        return rolling
    }

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined")
        mainWindow = testMainWindow
        verify(mainWindow)
        savedShellMode = SettingsController.playerShellMode
        savedThemeMode = SettingsController.themeMode
        savedWaveformMode = SettingsController.waveformMode
    }

    function cleanup() {
        SettingsController.playerShellMode = 0
        SettingsController.themeMode = savedThemeMode
        SettingsController.waveformMode = savedWaveformMode
        tryVerify(function() {
            return shell("classicPlayerShell") !== null
        }, 1500)
    }

    function cleanupTestCase() {
        SettingsController.playerShellMode = savedShellMode
    }

    function test_mode_two_loads_only_rolling_shell() {
        SettingsController.playerShellMode = 2

        tryVerify(function() {
            return shell("rollingPlayerShell") !== null
        }, 1500)
        tryVerify(function() {
            return shell("classicPlayerShell") === null
                   && shell("integratedPlayerShell") === null
        }, 1500)
        compare(mainWindow.minimumWidth, 1000)
        compare(mainWindow.minimumHeight, 720)
    }

    function test_rolling_list_ignores_classic_thumbnail_switch() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        try {
            SettingsController.listWaveformThumbnailEnabled = false
            var rolling = rollingWithFakes()
            var list = findChild(rolling, "rollingTrackList")
            verify(list)
            compare(list.waveformThumbnailsVisible, true)
            compare(list.rowHeight, 50)
        } finally {
            SettingsController.listWaveformThumbnailEnabled = previousEnabled
        }
    }

    function test_waveform_modes_reuse_shared_spectral_analysis() {
        var trackIds = nativeDropHelper.ensureSortableTracks()
        verify(trackIds.length > 0)
        PlaybackController.playRow(LibraryModel.indexForTrackId(trackIds[0]))
        tryCompare(PlaybackController, "currentTrackId", trackIds[0], 1000)
        enterMode(0)
        var session = findChild(mainWindow, "sharedWaveformSession")
        verify(session)
        tryVerify(function() { return session.generation > 0 }, 1000)

        SettingsController.waveformMode = 0
        wait(0)
        var generationBeforeRenderOnlyChange = session.generation
        SettingsController.waveformMode = 1
        wait(0)
        compare(session.generation, generationBeforeRenderOnlyChange,
                "render-only waveform modes must reuse the loaded layers")

        var generationBeforeFrequencyChange = session.generation
        SettingsController.waveformMode = 3
        wait(0)
        compare(session.generation, generationBeforeFrequencyChange,
                "spectral indices are cached with the shared waveform analysis")
    }

    function test_three_shells_share_exact_control_order_and_skin_menu() {
        nativeDropHelper.ensureSortableTracks()
        mainWindow.width = 1440
        mainWindow.height = 760
        for (var mode = 0; mode < 3; ++mode) {
            var names = ["listWindowButton", "equalizerButton"]
            if (mode !== 2)
                names.push("waveformModeButton")
            names = names.concat([
                "previousButton", "playPauseButton", "nextButton", "modeButton",
                "mainVolumeControl", "audioToolsButton", "lyricsActionButton",
                "themeModeButton", "immersiveActionButton", "windowLayoutButton"
            ])
            var activeShell = enterMode(mode)
            var controls = mode === 1
                    ? findChild(mainWindow, "integratedPlayerControls")
                    : findChild(activeShell, "playerControls")
            verify(controls, "mode " + mode + " must instantiate PlayerControls")
            wait(50)
            var previousX = -1
            for (var index = 0; index < names.length; ++index) {
                var control = findChild(controls, names[index])
                verify(control, "missing " + names[index] + " in mode " + mode)
                verify(control.visible,
                       names[index] + " must be visible in mode " + mode)
                var mapped = control.mapToItem(controls, 0, 0).x
                verify(mapped > previousX,
                       names[index] + " is out of order in mode " + mode
                       + ": " + mapped + " <= " + previousX
                       + "; parent=" + control.parent.objectName
                       + "; experience="
                       + (findChild(controls, "experienceActions")
                          ? findChild(controls, "experienceActions")
                              .mapToItem(controls, 0, 0).x : -1))
                previousX = mapped
            }
            var waveformMode = findChild(controls, "waveformModeButton")
            verify(waveformMode)
            compare(waveformMode.visible, mode !== 2,
                    "rolling mode fixes the renderer to spectral waveform")
        }

        var rolling = enterMode(2)
        var skinButton = findChild(rolling, "windowLayoutButton")
        verify(skinButton)
        verify(skinButton.icon.source.toString()
               .endsWith("/player-shell-mode.svg"))
        mouseClick(skinButton)
        var menu = findChild(rolling, "playerShellMenu")
        verify(menu)
        compare(menu.count, 3)
        compare(findChild(menu, "classicShellMenuItem").text, "经典双窗口")
        compare(findChild(menu, "integratedShellMenuItem").text, "集成单窗口")
        compare(findChild(menu, "rollingShellMenuItem").text, "滚动播放模式")
        menu.close()
    }

    function test_overview_click_seeks_once_and_starts_playback() {
        var rolling = rollingWithFakes()
        var overview = findChild(rolling, "rollingOverviewWaveform")
        var interaction = findChild(rolling, "rollingOverviewInteraction")
        var progress = findChild(rolling, "rollingOverviewProgress")
        var playedClip = findChild(rolling, "rollingOverviewPlayedClip")
        var hoverCapsule = findChild(rolling, "rollingOverviewHoverCapsule")
        var hoverText = findChild(rolling, "rollingOverviewHoverText")
        verify(overview && interaction && progress && playedClip
               && hoverCapsule && hoverText)
        compare(overview.visibleStartMs, 0)
        compare(overview.visibleEndMs, 120000)
        compare(overview.visualMode, 3)
        compare(overview.position, 0)
        compare(playedClip.width, overview.width * 0.5)
        compare(progress.color.toString(), Theme.waveformMagenta.toString())

        mouseMove(interaction, interaction.width * 0.25,
                  interaction.height / 2)
        tryCompare(hoverCapsule, "visible", true)
        compare(hoverText.text, "00:30")

        mouseClick(interaction, interaction.width * 0.25,
                   interaction.height / 2, Qt.LeftButton)
        compare(fakePlayback.seekCount, 1)
        verify(Math.abs(fakePlayback.lastSeek - 30000) <= 1)
        compare(fakePlayback.playCount, 1)
    }

    function test_main_waveform_scrolls_under_fixed_center_playhead() {
        var rolling = rollingWithFakes()
        var canvas = findChild(rolling, "rollingMainWaveformCanvas")
        var waveform = findChild(rolling, "rollingMainWaveform")
        var playhead = findChild(rolling, "rollingCenterPlayhead")
        verify(canvas && waveform && playhead)
        compare(waveform.visualMode, 3)
        compare(waveform.position, fakePlayback.positionMs)
        rolling.waveformPixelsPerSecond = 120
        rolling.syncWaveformViewport()
        var firstStart = waveform.visibleStartMs
        var referenceX = waveform.pixelForTime(59000)
        compare(Math.round(playhead.mapToItem(canvas,
                                             playhead.width / 2, 0).x),
                Math.round(canvas.width / 2))

        fakePlayback.positionMs = 61000
        rolling.syncWaveformViewport()
        compare(waveform.position, 61000)
        verify(waveform.visibleStartMs > firstStart)
        verify(waveform.pixelForTime(59000) < referenceX,
               "a fixed source point must move left during playback")
        compare(Math.round(playhead.mapToItem(canvas,
                                             playhead.width / 2, 0).x),
                Math.round(canvas.width / 2))
    }

    function test_scratch_mapping_threshold_direction_end_cancel_and_buffering() {
        var rolling = rollingWithFakes()
        var surface = findChild(rolling, "rollingScratchSurface")
        var buffering = findChild(rolling, "rollingScratchStatus")
        verify(surface && buffering)
        compare(rolling.signedRateForDrag(-12, 100), 1)
        compare(rolling.signedRateForDrag(12, 100), -1)
        compare(rolling.signedRateForDrag(-120, 10), 3)
        compare(rolling.deltaMsForPixels(-12), 100)

        mousePress(surface, surface.width / 2, surface.height / 2,
                   Qt.LeftButton)
        mouseMove(surface, surface.width / 2 - 3, surface.height / 2, 20)
        compare(fakePlayback.beginCount, 0)
        mouseMove(surface, surface.width / 2 - 18, surface.height / 2, 80)
        compare(fakePlayback.beginCount, 1)
        verify(fakePlayback.lastScratchRate > 0)
        mouseRelease(surface, surface.width / 2 - 18,
                     surface.height / 2, Qt.LeftButton)
        compare(fakePlayback.endCount, 1)

        mousePress(surface, surface.width / 2, surface.height / 2,
                   Qt.LeftButton)
        mouseMove(surface, surface.width / 2 + 18, surface.height / 2, 80)
        verify(fakePlayback.lastScratchRate < 0)
        rolling.cancelScratchGesture()
        compare(fakePlayback.cancelCount, 1)

        fakePlayback.scratchBuffering = true
        tryCompare(buffering, "visible", true)
        fakePlayback.scratchBuffering = false
        tryCompare(buffering, "visible", false)
    }

    function test_scratch_drag_stops_the_vinyl_after_35ms_idle() {
        var rolling = rollingWithFakes()
        var surface = findChild(rolling, "rollingScratchSurface")
        verify(surface)

        mousePress(surface, surface.width / 2, surface.height / 2,
                   Qt.LeftButton)
        mouseMove(surface, surface.width / 2 - 20, surface.height / 2, 30)
        verify(fakePlayback.lastScratchRate > 0)
        var updatesBeforeIdle = fakePlayback.updateCount
        wait(60)
        verify(fakePlayback.updateCount > updatesBeforeIdle)
        compare(fakePlayback.lastScratchRate, 0)
        mouseRelease(surface, surface.width / 2 - 20, surface.height / 2,
                     Qt.LeftButton)
    }

    function test_main_waveform_keeps_the_playhead_centered_at_track_edges() {
        var rolling = rollingWithFakes()
        var canvas = findChild(rolling, "rollingMainWaveformCanvas")
        var waveform = findChild(rolling, "rollingMainWaveform")
        verify(canvas && waveform)
        rolling.waveformPixelsPerSecond = 120

        var edgePositions = [0, 100, fakePlayback.durationMs - 100,
                             fakePlayback.durationMs]
        for (var index = 0; index < edgePositions.length; ++index) {
            var position = edgePositions[index]
            fakePlayback.positionMs = position
            rolling.syncWaveformViewport()
            wait(0)
            tryVerify(function() {
                var sourceX = waveform.mapToItem(
                            canvas, waveform.pixelForTime(position), 0).x
                return Math.abs(sourceX - canvas.width / 2) <= 1
            }, 1000, "source time " + position
                     + " must remain beneath the fixed playhead")
            if (position === 0)
                verify(rolling.viewportStartMs < 0,
                       "track start keeps virtual lead-in padding")
            if (position === fakePlayback.durationMs)
                verify(rolling.viewportEndMs > fakePlayback.durationMs,
                       "track end keeps virtual lead-out padding")
        }
    }

    function test_track_metadata_reacts_to_waveform_and_library_revisions() {
        var rolling = rollingWithFakes()
        verify(rolling.metadataBadges.indexOf("WAV") >= 0)
        verify(rolling.metadataBadges.indexOf("24-bit") >= 0)
        verify(rolling.metadataBadges.indexOf("48 kHz") >= 0)
        verify(rolling.metadataBadges.indexOf("2116 kbps") >= 0)
        verify(rolling.metadataBadges.indexOf("120 BPM") >= 0)
        verify(rolling.metadataBadges.indexOf("34.5 MB") >= 0)
        var lookupsBeforeWaveformRevision = fakeLibrary.lookupCount
        ++fakeWaveformSession.libraryRevision
        tryVerify(function() {
            return fakeLibrary.lookupCount > lookupsBeforeWaveformRevision
        })

        var lookupsBeforeDataChange = fakeLibrary.lookupCount
        fakeLibrary.dataChanged()
        tryVerify(function() {
            return fakeLibrary.lookupCount > lookupsBeforeDataChange
        })
    }

    function test_spectral_waveform_palette_is_theme_independent() {
        var rolling = rollingWithFakes()
        var overview = findChild(rolling, "rollingOverviewWaveform")
        var mainWaveform = findChild(rolling, "rollingMainWaveform")
        verify(overview && mainWaveform)

        var first = String(rolling.frequencyWaveformSettings.palette[0])
        var last = String(rolling.frequencyWaveformSettings.palette[7])

        SettingsController.themeMode = 1
        tryCompare(overview, "spectralUnplayedOpacity",
                   rolling.frequencyWaveformSettings.unplayedOpacity)
        compare(String(overview.spectralPalette[0]), first)
        compare(String(mainWaveform.spectralPalette[7]), last)
        SettingsController.themeMode = 0
        compare(String(overview.spectralPalette[0]), first)
        compare(String(mainWaveform.spectralPalette[7]), last)
    }

    function test_rolling_tempo_meter_and_zoom_controls_are_live() {
        var rolling = rollingWithFakes()
        var sourceBpm = findChild(rolling, "rollingSourceBpm")
        var targetBpm = findChild(rolling, "rollingTargetBpm")
        var keepPitch = findChild(rolling, "rollingKeepPitchControl")
        var leftMeter = findChild(rolling, "rollingLeftMeter")
        var rightMeter = findChild(rolling, "rollingRightMeter")
        verify(sourceBpm && targetBpm && keepPitch && leftMeter && rightMeter)
        compare(sourceBpm.text, "120.00 BPM")
        compare(targetBpm.text, "120.00")
        compare(leftMeter.peak, fakeVisualFeatures.leftPeak)
        compare(rightMeter.rms, fakeVisualFeatures.rightRms)

        rolling.adjustSpeed(0.05)
        compare(fakePlayback.speedRatio, 1.05)
        rolling.commitTargetBpm("150")
        compare(fakePlayback.targetBpm, 150)
        mouseClick(keepPitch)
        compare(fakePlayback.keepPitch, false)
        rolling.resetRollingTempo()
        compare(fakePlayback.resetCount, 1)

        rolling.waveformPixelsPerSecond = 120
        rolling.zoomIn()
        compare(rolling.waveformPixelsPerSecond, 150)
        rolling.zoomOut()
        compare(rolling.waveformPixelsPerSecond, 120)
        rolling.waveformPixelsPerSecond = 470
        rolling.zoomIn()
        compare(rolling.waveformPixelsPerSecond, 480)
        rolling.resetZoom()
        compare(rolling.waveformPixelsPerSecond, 120)
    }

    function test_rolling_reuses_collapsible_tag_and_lyrics_side_panel() {
        var rolling = rollingWithFakes()
        rolling.lyricsService = fakeLyricsService
        var navigation = findChild(rolling, "rollingLibraryNavigation")
        var column = findChild(rolling, "rollingTagColumn")
        var tagTab = findChild(rolling, "rollingTagTabButton")
        var lyricsTab = findChild(rolling, "rollingLyricsTabButton")
        var tagContent = findChild(rolling, "rollingTagContent")
        var lyricsContent = findChild(rolling, "rollingLyricsContent")
        var lyricsPanel = findChild(rolling, "rollingLyricsPanel")
        var toggle = findChild(rolling, "rollingSidePanelToggleButton")
        verify(navigation && column && tagTab && lyricsTab && tagContent
               && lyricsContent && lyricsPanel && toggle)
        compare(navigation.showTagManagementEntry, false)
        compare(column.width, 232)
        verify(tagContent.visible)
        compare(lyricsContent.visible, false)

        mouseClick(lyricsTab)
        compare(tagContent.visible, false)
        verify(lyricsContent.visible)
        compare(findChild(lyricsPanel, "currentLyricLine").text,
                "当前共享歌词")

        mouseClick(toggle)
        tryCompare(column, "width", 42)
        mouseClick(toggle)
        tryCompare(column, "width", 232)
        verify(lyricsContent.visible)
    }

    function test_rolling_layout_stays_non_overlapping_at_minimum_and_ultrawide() {
        var rolling = rollingWithFakes()
        var overview = findChild(rolling, "rollingOverviewRegion")
        var waveform = findChild(rolling, "rollingMainWaveformCanvas")
        var bottom = findChild(rolling, "rollingBottomBar")
        var library = findChild(rolling, "rollingLibraryWorkspace")
        var trackList = findChild(rolling, "rollingTrackList")
        var filter = findChild(rolling, "rollingSearchFilter")
        var tags = findChild(rolling, "rollingTagManagementPanel")
        verify(overview && waveform && bottom && library
               && trackList && filter && tags)

        mainWindow.width = 1000
        mainWindow.height = 720
        wait(0)
        verify(overview.y + overview.height <= waveform.y + 1)
        verify(waveform.y + waveform.height <= bottom.y + 1)
        verify(bottom.y + bottom.height <= library.y + 1)
        verify(library.y + library.height <= rolling.height + 1)
        compare(trackList.trackModel, rolling.filterModel)
        compare(trackList.playlistModel, rolling.playlistModel)
        var controls = findChild(rolling, "playerControls")
        verify(controls)
        var minimumWidthSharedActions = [
            "equalizerButton", "windowLayoutButton",
            "lyricsActionButton", "immersiveActionButton", "miniPlayerButton"
        ]
        for (var actionIndex = 0;
             actionIndex < minimumWidthSharedActions.length; ++actionIndex) {
            var action = findChild(controls,
                                   minimumWidthSharedActions[actionIndex])
            verify(action && action.visible,
                   minimumWidthSharedActions[actionIndex]
                   + " stays available at the rolling minimum width")
        }
        compare(findChild(controls, "waveformModeButton").visible, false)
        var transport = findChild(controls, "centerPlaybackControls")
        verify(transport)
        verify(transport.mapToItem(controls, 0, 0).x < controls.width * 0.30,
               "rolling transport belongs on the left side")
        compare(bottom.height, 64)

        mainWindow.width = 1800
        mainWindow.height = 600
        wait(0)
        var playhead = findChild(rolling, "rollingCenterPlayhead")
        compare(Math.round(playhead.mapToItem(waveform,
                                             playhead.width / 2, 0).x),
                Math.round(waveform.width / 2))
    }
}
