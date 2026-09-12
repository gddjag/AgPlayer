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

    Component { id: trackListFixture; TrackList { width: 900; height: 300 } }
    Component { id: signalSpyFixture; SignalSpy {} }

    QtObject {
        id: fakePlayback
        property int positionMs: 60000
        property int durationMs: 120000
        property string currentTrackId: "rolling-track"
        property real speedRatio: 1.0
        property real sourceBpm: 120.0
        property real targetBpm: sourceBpm * speedRatio
        property real beatGridBpm: 128.0
        property int beatGridOffsetMs: 250
        property bool beatGridCalibrated: true
        property bool beatGridEstimatedBpm: false
        property int cuePositionMs: 45000
        property var hotCuePositions: [-1, -1, -1, -1, -1, -1, -1, -1]
        property bool cueAuditioning: false
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
        property int cuePressCount: 0
        property int cueReleaseCount: 0
        property int cueCancelCount: 0
        property int gridFirstBeatCount: 0
        property int gridNudgeTotal: 0
        property int gridBpmCount: 0
        property int gridResetCount: 0
        property int toggleCount: 0
        property real lastSeek: -1
        property real lastScratchRate: 0

        function reset() {
            positionMs = 60000
            durationMs = 120000
            speedRatio = 1.0
            sourceBpm = 120.0
            keepPitch = true
            beatGridBpm = 128.0
            beatGridOffsetMs = 250
            beatGridCalibrated = true
            beatGridEstimatedBpm = false
            cuePositionMs = 45000
            hotCuePositions = [-1, -1, -1, -1, -1, -1, -1, -1]
            cueAuditioning = false
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
            cuePressCount = 0
            cueReleaseCount = 0
            cueCancelCount = 0
            gridFirstBeatCount = 0
            gridNudgeTotal = 0
            gridBpmCount = 0
            gridResetCount = 0
            toggleCount = 0
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
        function cuePress() { ++cuePressCount; cueAuditioning = true }
        function cueRelease() { ++cueReleaseCount; cueAuditioning = false }
        function cancelCue() { ++cueCancelCount; cueAuditioning = false }
        function setBeatGridFirstBeat() {
            ++gridFirstBeatCount
            beatGridOffsetMs = positionMs
            beatGridCalibrated = true
        }
        function nudgeBeatGrid(deltaMs) {
            gridNudgeTotal += deltaMs
            beatGridOffsetMs += deltaMs
        }
        function setBeatGridBpm(value) {
            ++gridBpmCount
            beatGridBpm = value
            beatGridCalibrated = true
            beatGridEstimatedBpm = false
        }
        function resetBeatGrid() {
            ++gridResetCount
            beatGridBpm = sourceBpm > 0 ? sourceBpm : 120
            beatGridOffsetMs = 0
            beatGridCalibrated = false
            beatGridEstimatedBpm = sourceBpm > 0
        }
        function togglePlayback() { ++toggleCount }
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
        property var trackTags: ["现场", "电子"]
        signal dataChanged()
        function trackForId(trackId) {
            ++lookupCount
            return {
                "title": "光辉岁月 Psy techno DJ CHEN REMIX",
                "artist": "测试艺术家",
                "album": "测试专辑",
                "tags": trackTags,
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
        compare(mainWindow.minimumWidth, 1180)
        compare(mainWindow.minimumHeight, 720)
    }

    function test_rolling_list_ignores_classic_thumbnail_switch() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        try {
            SettingsController.listWaveformThumbnailEnabled = false
            var rolling = rollingWithFakes()
            var list = findChild(rolling, "rollingTrackList")
            verify(list)
            compare(list.layoutProfile, "rolling")
            compare(list.waveformThumbnailsVisible, true)
            compare(list.rowHeight, Theme.mediaListRowHeight)
        } finally {
            SettingsController.listWaveformThumbnailEnabled = previousEnabled
        }
    }

    function test_single_window_duration_header_centers_over_values() {
        for (var mode = 1; mode <= 2; ++mode) {
            var currentShell = enterMode(mode)
            var list = findChild(currentShell, mode === 2
                                 ? "rollingTrackList" : "integratedTrackList")
            verify(list)
            var header = findChild(list, "trackHeaderDuration")
            verify(header)
            compare(header.horizontalAlignment, Text.AlignHCenter,
                    "duration heading must share the centered values alignment")
        }
    }

    function test_rolling_control_groups_center_in_transport_bar() {
        var rolling = rollingWithFakes()
        mainWindow.width = rolling.defaultWindowWidth
        mainWindow.height = rolling.defaultWindowHeight
        wait(0)
        var bottom = findChild(rolling, "rollingBottomBar")
        var groups = [findChild(rolling, "rollingBeatGridSwitch").parent,
                      findChild(rolling, "rollingKeepPitchControl").parent,
                      findChild(rolling, "rollingShellActions")]
        for (var i = 0; i < groups.length; ++i) {
            var center = groups[i].mapToItem(bottom, 0, groups[i].height / 2).y
            verify(Math.abs(center - bottom.height / 2) <= 1,
                   "control group must center within the whole transport bar")
        }
    }

    function test_rolling_bpm_input_is_compact_and_centered() {
        var rolling = rollingWithFakes()
        var target = findChild(rolling, "rollingTargetBpm")
        var grouping = findChild(rolling, "rollingBeatGridGrouping")
        compare(target.height, grouping.height)
        compare(target.height, 24)
        compare(target.width, 62)
        compare(grouping.width, 62)
        compare(findChild(target, "themedTextFieldFrame").height, target.height)
        target.text = "128.16"
        verify(target.contentWidth <= target.width - target.leftPadding - target.rightPadding,
               "128.16 must fit without horizontal scrolling or clipping")
        compare(target.verticalAlignment, TextInput.AlignVCenter)
    }

    function test_rolling_wrapped_controls_center_without_overflow() {
        var rolling = rollingWithFakes()
        var bottom = findChild(rolling, "rollingBottomBar")
        var flow = findChild(rolling, "rollingTempoControls")
        for (var width of [1180, 1386, 1600]) {
            mainWindow.width = width
            mainWindow.height = rolling.defaultWindowHeight
            wait(0)
            var top = Number.POSITIVE_INFINITY
            var end = 0
            for (var i = 0; i < flow.children.length; ++i) {
                var group = flow.children[i]
                if (!group.visible || group.height <= 1) continue
                var pos = group.mapToItem(bottom, 0, 0)
                var contentTop = Number.POSITIVE_INFINITY
                var contentEnd = 0
                for (var child of group.children) {
                    if (!child.visible || child.height <= 0) continue
                    contentTop = Math.min(contentTop, child.y)
                    contentEnd = Math.max(contentEnd, child.y + child.height)
                }
                verify(Math.abs((contentTop + contentEnd) / 2
                                - group.height / 2) <= 1,
                       "visible group contents, not only their container, must center vertically")
                top = Math.min(top, pos.y)
                end = Math.max(end, pos.y + group.height)
                verify(pos.x >= 0 && pos.x + group.width <= bottom.width + 1)
                verify(pos.y >= 0 && pos.y + group.height <= bottom.height + 1)
            }
            verify(Math.abs(top - (bottom.height - end)) <= 1,
                   "all wrapped rows must have balanced top and bottom clearance")
        }
    }

    function test_classic_favorite_icon_is_compact() {
        var list = createTemporaryObject(trackListFixture, testCase)
        verify(list)
        compare(list.favoriteIconSize, 20)
    }

    function test_rolling_default_window_is_1386_wide_and_shows_ten_rows() {
        var rolling = rollingWithFakes()
        mainWindow.width = rolling.defaultWindowWidth
        mainWindow.height = rolling.defaultWindowHeight
        wait(0)

        var list = findChild(rolling, "rollingTrackList")
        verify(list && list.headerItem)
        compare(mainWindow.width, 1386)
        compare(list.mapToItem(rolling, 0, 0).x, 233)
        compare(list.width, 856)
        compare(list.height, rolling.defaultTrackListHeight)
        compare((list.height - list.headerItem.height) / list.rowHeight, 10)
    }

    function test_waveform_mode_reuses_single_three_band_analysis() {
        var trackIds = nativeDropHelper.ensureSortableTracks()
        verify(trackIds.length > 0)
        PlaybackController.playRow(LibraryModel.indexForTrackId(trackIds[0]))
        tryCompare(PlaybackController, "currentTrackId", trackIds[0], 1000)
        enterMode(0)
        var session = findChild(mainWindow, "sharedWaveformSession")
        verify(session)
        session.loadWaveform()
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
                "frequency mode must reuse the single three-band analysis")
    }

    function test_three_shells_share_exact_control_order_and_skin_menu() {
        nativeDropHelper.ensureSortableTracks()
        mainWindow.width = 1440
        mainWindow.height = 760
        for (var mode = 0; mode < 3; ++mode) {
            var names = mode === 2
                    ? ["previousButton", "playPauseButton", "nextButton",
                       "modeButton", "equalizerButton",
                       "audioToolsButton", "mainVolumeControl",
                       "themeModeButton", "immersiveActionButton",
                       "miniPlayerButton"]
                    : mode === 1
                      ? ["audioToolsButton", "equalizerButton",
                         "waveformModeButton", "previousButton",
                         "playPauseButton", "nextButton", "modeButton",
                         "mainVolumeControl", "themeModeButton",
                         "immersiveActionButton", "miniPlayerButton"]
                      : ["listWindowButton", "audioToolsButton",
                         "equalizerButton", "waveformModeButton",
                         "previousButton", "playPauseButton", "nextButton",
                         "modeButton", "lyricsActionButton",
                         "mainVolumeControl", "themeModeButton",
                         "immersiveActionButton", "miniPlayerButton"]
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
            if (mode === 2) {
                compare(waveformMode, null,
                        "rolling must not instantiate a waveform style action")
            } else {
                verify(waveformMode && waveformMode.visible,
                       "classic and integrated retain waveform style switching")
            }
            var transport = findChild(controls,
                                      mode === 1
                                      ? "integratedTransportControls"
                                      : "centerPlaybackControls")
            verify(transport)
            compare(transport.waveformPlacement,
                    mode === 2 ? "afterMode" : "beforePrevious")
            compare(findChild(controls, "listWindowButton").visible, mode === 0,
                    "single and rolling shells expose their shared list directly")
            var lyricsAction = findChild(controls, "lyricsActionButton")
            compare(lyricsAction !== null && lyricsAction.visible, mode === 0,
                    "single and rolling shells expose lyrics in the persistent side pane")
            if (mode === 2)
                compare(findChild(controls, "experienceActions"), null,
                        "rolling must not instantiate a hidden action registry")
            if (mode === 2)
                compare(findChild(controls, "themeModeButton").parent.objectName,
                        "rollingShellActions")
            if (mode === 2) {
                var rollingVolume = findChild(controls, "mainVolumeControl")
                var rollingSlider = findChild(controls, "volumeSlider")
                var rollingActions = findChild(activeShell,
                                               "rollingShellActions")
                verify(rollingVolume && rollingSlider && rollingActions)
                compare(rollingVolume.emptyMode, false,
                        "rolling mode must keep the shared volume flyout")
                rollingVolume.expandedForQa = true
                tryVerify(function() { return rollingSlider.visible
                                              && rollingSlider.width >= 48 },
                          500)
                rollingVolume.expandedForQa = false
                var actionNames = ["themeModeButton", "immersiveActionButton",
                                   "miniPlayerButton"]
                for (var actionIndex = 0; actionIndex < actionNames.length;
                     ++actionIndex) {
                    var action = findChild(controls, actionNames[actionIndex])
                    verify(action)
                    verify(Math.abs(action.mapToItem(rollingActions, 0, 0).y
                                    - (rollingActions.height - action.height) / 2)
                           <= 1,
                           actionNames[actionIndex]
                           + " must be vertically centered after zoom reset")
                }
            }
        }

        var rolling = enterMode(2)
        var skinButton = findChild(rolling, "themeModeButton")
        verify(skinButton)
        verify(skinButton.icon.source.toString()
               .endsWith("/theme-skin.svg"))
        mouseClick(skinButton)
        var menu = findChild(rolling, "playerShellMenu")
        verify(menu)
        compare(menu.count, 3)
        compare(findChild(menu, "classicShellMenuItem").text, "经典双窗口")
        compare(findChild(menu, "integratedShellMenuItem").text, "集成单窗口")
        compare(findChild(menu, "rollingShellMenuItem").text, "专业模式")
        menu.close()
    }

    function test_rolling_keeps_frequency_rendering_without_changing_other_shell_preferences() {
        var rolling = rollingWithFakes()
        var overview = findChild(rolling, "rollingOverviewWaveform")
        var waveform = findChild(rolling, "rollingMainWaveform")
        verify(overview && waveform)
        for (var mode = 0; mode < 4; ++mode) {
            SettingsController.waveformMode = mode
            wait(0)
            compare(overview.visualMode, 3)
            compare(waveform.visualMode, 3)
            compare(SettingsController.waveformMode, mode,
                    "rolling must not overwrite the other players' waveform preference")
        }
    }

    function test_overview_click_seeks_once_and_starts_playback() {
        var rolling = rollingWithFakes()
        var overview = findChild(rolling, "rollingOverviewWaveform")
        var interaction = findChild(rolling, "rollingOverviewInteraction")
        var progress = findChild(rolling, "rollingOverviewProgress")
        var playhead = findChild(rolling, "rollingOverviewPlayhead")
        var hoverCapsule = findChild(rolling, "rollingOverviewHoverCapsule")
        var hoverText = findChild(rolling, "rollingOverviewHoverText")
        verify(overview && interaction && progress && playhead
               && hoverCapsule && hoverText)
        compare(overview.visibleStartMs, 0)
        compare(overview.visibleEndMs, 120000)
        compare(overview.visualMode, 3)
        compare(overview.position, 0)
        compare(progress.width, overview.width * 0.5)
        compare(progress.color.toString(), Theme.waveformMagenta.toString())
        compare(playhead.color.toString(),
                Theme.onBrandGradientText.toString())
        compare(playhead.height, overview.height)
        verify(Math.abs(playhead.mapToItem(overview, playhead.width / 2, 0).x
                        - overview.pixelForTime(fakePlayback.positionMs)) <= 1)

        mouseMove(interaction, interaction.width * 0.25,
                  interaction.height / 2)
        tryCompare(hoverCapsule, "visible", true)
        compare(hoverText.text, "00:30")

        mouseClick(interaction, interaction.width * 0.25,
                   interaction.height / 2, Qt.LeftButton)
        compare(fakePlayback.seekCount, 1)
        verify(Math.abs(fakePlayback.lastSeek - 30000) <= 1)
        compare(fakePlayback.playCount, 1)
        tryVerify(function() {
            return Math.abs(
                        playhead.mapToItem(overview,
                                           playhead.width / 2, 0).x
                        - overview.width * 0.25) <= 1
        })
    }

    function test_main_waveform_scrolls_under_fixed_center_playhead() {
        var rolling = rollingWithFakes()
        var canvas = findChild(rolling, "rollingMainWaveformCanvas")
        var waveform = findChild(rolling, "rollingMainWaveform")
        var playhead = findChild(rolling, "rollingCenterPlayhead")
        verify(canvas && waveform && playhead)
        compare(waveform.visualMode, 3)
        tryCompare(waveform, "position", fakePlayback.positionMs)
        rolling.syncWaveformViewport()
        var firstStart = waveform.visibleStartMs
        var referenceX = waveform.mapToItem(
                    canvas, waveform.pixelForTime(60000), 0).x
        compare(Math.round(playhead.mapToItem(canvas,
                                             playhead.width / 2, 0).x),
                Math.round(canvas.width / 2))

        fakePlayback.positionMs = 61000
        rolling.syncWaveformViewport()
        compare(waveform.position, 61000)
        compare(waveform.visibleStartMs, firstStart)
        verify(waveform.mapToItem(
                   canvas, waveform.pixelForTime(60000), 0).x < referenceX,
               "a fixed source point must move left during playback")
        compare(Math.round(playhead.mapToItem(canvas,
                                             playhead.width / 2, 0).x),
                Math.round(canvas.width / 2))
    }

    function test_main_waveform_uses_bounded_render_window_while_scrolling() {
        var rolling = rollingWithFakes()
        var canvas = findChild(rolling, "rollingMainWaveformCanvas")
        var waveform = findChild(rolling, "rollingMainWaveform")
        verify(canvas && waveform)
        rolling.syncWaveformViewport()
        wait(0)

        var startSpy = createTemporaryObject(signalSpyFixture, rolling, {
            "target": waveform,
            "signalName": "visibleStartMsChanged"
        })
        var endSpy = createTemporaryObject(signalSpyFixture, rolling, {
            "target": waveform,
            "signalName": "visibleEndMsChanged"
        })
        verify(startSpy && endSpy)
        startSpy.clear()
        endSpy.clear()

        var initialRangeMs = waveform.visibleEndMs - waveform.visibleStartMs
        var expectedBufferedRangeMs = Math.min(fakePlayback.durationMs,
                                               rolling.viewportSpanMs * 2)
        var previousX = waveform.x
        var translatedTicks = 0
        var startedAtMs = Date.now()
        for (var tick = 0; tick < 240; ++tick) {
            fakePlayback.positionMs += 16
            rolling.syncWaveformViewport()
            if (waveform.x < previousX)
                ++translatedTicks
            previousX = waveform.x
            var sourceX = waveform.mapToItem(
                        canvas,
                        waveform.pixelForTime(fakePlayback.positionMs), 0).x
            verify(Math.abs(sourceX - canvas.width / 2) <= 1,
                   "the current source time must stay under the fixed needle")
        }
        var elapsedMs = Date.now() - startedAtMs
        console.info("rolling-waveform-240-ticks-ms=" + elapsedMs
                     + " visible-range-updates="
                     + (startSpy.count + endSpy.count)
                     + " translated-ticks=" + translatedTicks)

        compare(translatedTicks, 240,
                "ordinary playback ticks must translate the cached waveform left")
        verify(Math.abs(initialRangeMs - expectedBufferedRangeMs) <= 1,
               "the cached render window must cover two viewports without changing px/ms")
        verify(startSpy.count + endSpy.count <= 2,
               "240 ordinary ticks must not rebuild the four-layer geometry every frame")

        var pointsPerMs = waveform.width
                / (waveform.visibleEndMs - waveform.visibleStartMs)
        verify(Math.abs(pointsPerMs
                        - (canvas.width - 2) / rolling.viewportSpanMs) < 0.0001,
               "the two-viewport buffer must retain the original pixel density")

        var lowColor = waveform.lowColor.toString()
        var midColor = waveform.midColor.toString()
        var highColor = waveform.highColor.toString()
        var rebaseBoundary = waveform.visibleEndMs
                - rolling.viewportSpanMs / 2
        fakePlayback.positionMs = Math.floor(rebaseBoundary)
        rolling.syncWaveformViewport()
        var sampleTime = fakePlayback.positionMs
        var beforeRebaseX = waveform.mapToItem(
                    canvas, waveform.pixelForTime(sampleTime), 0).x
        var updatesBeforeRebase = startSpy.count + endSpy.count
        var stepMs = 16
        fakePlayback.positionMs += stepMs
        rolling.syncWaveformViewport()
        var afterRebaseX = waveform.mapToItem(
                    canvas, waveform.pixelForTime(sampleTime), 0).x
        var expectedShift = -stepMs * rolling.pxPerSec / 1000

        compare(startSpy.count + endSpy.count - updatesBeforeRebase, 2,
                "crossing the guard boundary must perform exactly one atomic range rebase")
        verify(Math.abs((afterRebaseX - beforeRebaseX) - expectedShift) <= 1,
               "a render-window rebase must not jump a source point on screen")
        compare(waveform.lowColor.toString(), lowColor)
        compare(waveform.midColor.toString(), midColor)
        compare(waveform.highColor.toString(), highColor)
    }

    function test_scratch_mapping_threshold_direction_end_cancel_and_buffering() {
        var rolling = rollingWithFakes()
        var surface = findChild(rolling, "rollingScratchSurface")
        var buffering = findChild(rolling, "rollingScratchStatus")
        verify(surface && buffering)
        var tenthSecondPixels = rolling.pxPerSec * 0.1
        verify(Math.abs(rolling.signedRateForDrag(
                            -tenthSecondPixels, 100) - 1) < 0.0001)
        verify(Math.abs(rolling.signedRateForDrag(
                            tenthSecondPixels, 100) + 1) < 0.0001)
        compare(rolling.signedRateForDrag(-rolling.pxPerSec, 10), 3)
        verify(Math.abs(rolling.deltaMsForPixels(
                            -tenthSecondPixels) - 100) < 0.0001)

        mousePress(surface, surface.width / 2, surface.height / 2,
                   Qt.LeftButton)
        mouseMove(surface, surface.width / 2 - 3, surface.height / 2, 20)
        compare(fakePlayback.beginCount, 0)
        var viewStartBeforeDrag = rolling.viewStartTimeSec
        mouseMove(surface, surface.width / 2 - 18, surface.height / 2, 80)
        compare(fakePlayback.beginCount, 1)
        verify(fakePlayback.lastScratchRate > 0)
        verify(rolling.viewStartTimeSec > viewStartBeforeDrag,
               "left drag continuously advances the visible time window")
        var releasedCenter = Math.round(rolling.viewportCenterMs)
        mouseRelease(surface, surface.width / 2 - 18,
                     surface.height / 2, Qt.LeftButton)
        compare(fakePlayback.endCount, 1)
        compare(fakePlayback.lastSeek, releasedCenter,
                "release must commit the visible center, not the rate-integrated audio position")
        compare(fakePlayback.playCount, 1)

        mousePress(surface, surface.width / 2, surface.height / 2,
                   Qt.LeftButton)
        mouseMove(surface, surface.width / 2 + 18, surface.height / 2, 80)
        verify(fakePlayback.lastScratchRate < 0)
        rolling.cancelScratchGesture()
        compare(fakePlayback.cancelCount, 1)
        compare(fakePlayback.seekCount, 1, "cancel must not commit a second seek")

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

    function test_frequency_waveform_colors_are_theme_independent() {
        var rolling = rollingWithFakes()
        var overview = findChild(rolling, "rollingOverviewWaveform")
        var mainWaveform = findChild(rolling, "rollingMainWaveform")
        verify(overview && mainWaveform)

        compare(mainWaveform.preserveSourcePeakDensity, false,
                "the rolling viewport must keep dense waveform detail instead of "
                + "stretching sparse source peaks across the canvas")
        compare(mainWaveform.sourceAnchoredSampling, true,
                "rolling must keep each source sample's amplitude and colour stable while panning")
        compare(overview.sourceAnchoredSampling, false,
                "source-anchored sampling stays local to the rolling viewport")

        var low = String(rolling.frequencyWaveformSettings.lowColor)
        var high = String(rolling.frequencyWaveformSettings.highColor)

        SettingsController.themeMode = 1
        tryCompare(overview, "frequencyUnplayedOpacity",
                   1.0)
        compare(mainWaveform.frequencyUnplayedOpacity, 1.0)
        compare(String(overview.lowColor), low)
        compare(String(mainWaveform.highColor), high)
        SettingsController.themeMode = 0
        compare(String(overview.lowColor), low)
        compare(String(mainWaveform.highColor), high)
    }

    function test_rolling_tempo_meter_and_zoom_controls_are_live() {
        var rolling = rollingWithFakes()
        compare(rolling.visibleBeats, 32,
                "each rolling entry starts at the approved 32-beat viewport")
        compare(rolling.viewTimeSpanSec, 15.0,
                "grid BPM, not target playback BPM, maps the rolling viewport")
        var sourceBpm = findChild(rolling, "rollingSourceBpm")
        var targetBpm = findChild(rolling, "rollingTargetBpm")
        var keepPitch = findChild(rolling, "rollingKeepPitchControl")
        var leftMeter = findChild(rolling, "rollingLeftMeter")
        var rightMeter = findChild(rolling, "rollingRightMeter")
        verify(sourceBpm && targetBpm && keepPitch && leftMeter && rightMeter)
        compare(sourceBpm.text, "BPM")
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

        rolling.visibleBeats = 8
        rolling.zoomIn()
        compare(rolling.visibleBeats, 4)
        rolling.zoomIn()
        compare(rolling.visibleBeats, 2)
        rolling.zoomOut()
        compare(rolling.visibleBeats, 4)
        rolling.visibleBeats = 64
        rolling.zoomOut()
        compare(rolling.visibleBeats, 64)
        rolling.resetZoom()
        compare(rolling.visibleBeats, 32)

        rolling.visibleBeats = 1
        tryCompare(rolling, "visibleBeats", 2)
        rolling.visibleBeats = 100
        tryCompare(rolling, "visibleBeats", 64)
    }

    function test_bpm_drives_continuous_viewport_time_and_pixel_mapping() {
        var rolling = rollingWithFakes()
        var canvas = findChild(rolling, "rollingMainWaveformCanvas")
        var capsule = findChild(rolling, "rollingCurrentTimeCapsule")
        var playhead = findChild(rolling, "rollingCenterPlayhead")
        verify(canvas && capsule && playhead)
        compare(capsule.background, null,
                "the fixed-needle time readout must use a transparent background")

        rolling.visibleBeats = 8
        compare(rolling.visibleBeats, 8)
        compare(rolling.effectiveBpm, 128)
        verify(Math.abs(rolling.beatSec - 0.46875) < 0.0001)
        verify(Math.abs(rolling.viewTimeSpanSec - 3.75) < 0.0001)
        verify(Math.abs(rolling.viewStartTimeSec - 58.125) < 0.0001)
        verify(Math.abs(rolling.viewEndTimeSec - 61.875) < 0.0001)
        verify(Math.abs(rolling.pxPerSec - canvas.width / 3.75) < 0.0001)
        verify(Math.abs(rolling.timeToX(fakePlayback.positionMs / 1000)
                        - canvas.width * 0.5) < 0.0001)
        compare(capsule.text, "01:00:00")
        verify(capsule.mapToItem(canvas, capsule.width, 0).x
               < playhead.mapToItem(canvas, 0, 0).x,
               "the current-time capsule stays on the top-left of the needle")

        fakePlayback.speedRatio = 1.25
        tryVerify(function() {
            return Math.abs(rolling.effectiveBpm - 128) < 0.0001
                    && Math.abs(rolling.viewTimeSpanSec - 3.75) < 0.0001
                    && Math.abs(rolling.viewStartTimeSec - 58.125) < 0.0001
                    && Math.abs(rolling.viewEndTimeSec - 61.875) < 0.0001
        })
        verify(Math.abs(rolling.timeToX(fakePlayback.positionMs / 1000)
                        - canvas.width * 0.5) < 0.0001)

        fakePlayback.beatGridBpm = 0
        fakePlayback.sourceBpm = 0
        tryCompare(rolling, "effectiveBpm", 120)
        verify(Math.abs(rolling.beatSec - 0.5) < 0.0001)
    }

    function test_single_and_rolling_subtitle_joins_optional_tags() {
        fakeLibrary.trackTags = ["现场", "电子"]
        var rolling = rollingWithFakes()
        var subtitle = findChild(rolling, "rollingTrackSubtitle")
        verify(subtitle)
        tryCompare(subtitle, "text", "测试艺术家 · 测试专辑 · 现场 · 电子")

        fakeLibrary.trackTags = []
        fakeLibrary.dataChanged()
        tryCompare(subtitle, "text", "测试艺术家 · 测试专辑")
        verify(findChild(rolling, "rollingFavoriteButton").visible)
        verify(findChild(rolling, "rollingTrackRating").visible)
    }

    function test_default_scroll_speed_and_centisecond_readout() {
        var rolling = rollingWithFakes()
        var canvas = findChild(rolling, "rollingMainWaveformCanvas")
        var time = findChild(rolling, "rollingCurrentTimeCapsule")
        verify(canvas && time)
        compare(rolling.pxPerSec, canvas.width / 15,
                "the 32-beat default uses grid BPM without changing audio speed")
        fakePlayback.positionMs = 72009
        tryCompare(time, "text", "01:12:00")
        fakePlayback.positionMs = 72010
        tryCompare(time, "text", "01:12:01")
        fakePlayback.positionMs = 71999
        tryCompare(time, "text", "01:11:99")
    }

    function test_rolling_waveform_retains_vertical_clearance() {
        var savedHeight = SettingsController.waveformHeight
        try {
            SettingsController.waveformHeight = 1.5
            var rolling = rollingWithFakes()
            var canvas = findChild(rolling, "rollingMainWaveformCanvas")
            var waveform = findChild(rolling, "rollingMainWaveform")
            verify(canvas && waveform)
            compare(canvas.height, rolling.height < 800 ? 124 : 136)
            compare(waveform.y, 4)
            compare(waveform.y + waveform.height, canvas.height - 4)
            compare(waveform.amplitudeScale, 1.5,
                    "rolling preserves the configured amplitude without a hidden cap")
            compare(waveform.preserveSourcePeakDensity, false)
        } finally {
            SettingsController.waveformHeight = savedHeight
        }
    }

    function test_grid_controls_step_exact_viewports_and_persist_display_choices() {
        var savedGridEnabled = SettingsController.rollingBeatGridEnabled
        var savedGrouping = SettingsController.rollingBeatGridGrouping
        try {
            SettingsController.rollingBeatGridEnabled = true
            SettingsController.rollingBeatGridGrouping = 4
            var rolling = rollingWithFakes()
            var gridSwitch = findChild(rolling, "rollingBeatGridSwitch")
            var grouping = findChild(rolling, "rollingBeatGridGrouping")
            var viewport = findChild(rolling, "rollingViewportBeats")
            var minus = findChild(rolling, "rollingZoomMinus")
            var plus = findChild(rolling, "rollingZoomPlus")
            var zoomReset = findChild(rolling, "rollingZoomReset")
            var targetBpm = findChild(rolling, "rollingTargetBpm")
            var tempoReset = findChild(rolling, "rollingTempoReset")
            var calibration = findChild(
                        rolling, "rollingGridCalibrationButton")
            verify(gridSwitch && grouping && viewport && minus && plus
                   && zoomReset && targetBpm && tempoReset && calibration)
            compare(gridSwitch.checked, true)
            compare(grouping.currentText, "4")
            compare(viewport.currentText, "32")
            compare(viewport.displayText, "32")
            compare(viewport.contentItem.text, "32")
            verify(viewport.contentItem.paintedWidth > 0)
            verify(viewport.contentItem.width
                   - viewport.contentItem.leftPadding
                   - viewport.contentItem.rightPadding
                   >= viewport.contentItem.paintedWidth)
            compare(viewport.contentItem.truncated, false)
            compare(minus.enabled, true)
            compare(plus.enabled, true)
            compare(gridSwitch.height, 28)
            compare(grouping.height, 24)
            compare(viewport.height, 24)
            compare(viewport.width, 62)
            compare(minus.height, 28)
            compare(plus.height, 28)
            compare(zoomReset.height, 28)
            compare(targetBpm.height, grouping.height)
            compare(targetBpm.verticalAlignment, TextInput.AlignVCenter)
            compare(tempoReset.height, 28)
            compare(calibration.height, 28)
            compare(zoomReset.icon.width, 18)
            compare(zoomReset.icon.height, 18)
            compare(tempoReset.icon.width, 18)
            compare(tempoReset.icon.height, 18)

            var gridRight = gridSwitch.mapToItem(
                        rolling, gridSwitch.width, 0).x
            var calibrationLeft = calibration.mapToItem(rolling, 0, 0).x
            var calibrationRight = calibration.mapToItem(
                        rolling, calibration.width, 0).x
            var groupingLeft = grouping.mapToItem(rolling, 0, 0).x
            verify(calibrationLeft >= gridRight,
                   "calibration belongs immediately after the grid switch")
            verify(groupingLeft >= calibrationRight,
                   "grouping follows calibration in the same control flow")

            var controlCenters = [gridSwitch, calibration, grouping, viewport,
                                  targetBpm, tempoReset]
            var firstCenter = controlCenters[0].mapToItem(
                        rolling, 0, controlCenters[0].height / 2).y
            for (var centerIndex = 1; centerIndex < controlCenters.length;
                 ++centerIndex) {
                var center = controlCenters[centerIndex].mapToItem(
                            rolling, 0,
                            controlCenters[centerIndex].height / 2).y
                verify(Math.abs(center - firstCenter) <= 1,
                       "rolling control centres share one baseline")
            }

            mouseClick(plus)
            compare(rolling.visibleBeats, 16)
            compare(viewport.currentText, "16")
            compare(viewport.displayText, "16")
            compare(viewport.contentItem.text, "16")
            verify(viewport.contentItem.paintedWidth > 0)
            compare(viewport.contentItem.truncated, false)
            while (plus.enabled)
                mouseClick(plus)
            compare(rolling.visibleBeats, 2)
            compare(plus.enabled, false)
            mouseClick(minus)
            compare(rolling.visibleBeats, 4)
            rolling.resetZoom()
            compare(rolling.visibleBeats, 32)

            rolling.visibleBeats = 8
            enterMode(0)
            tryVerify(function() {
                return shell("rollingPlayerShell") === null
            }, 1000, "rolling shell must finish unloading before re-entry")
            rolling = rollingWithFakes()
            compare(rolling.visibleBeats, 32,
                    "re-entering rolling resets the viewport while track changes do not")

            gridSwitch = findChild(rolling, "rollingBeatGridSwitch")
            grouping = findChild(rolling, "rollingBeatGridGrouping")
            verify(gridSwitch && grouping)

            mouseClick(gridSwitch)
            compare(SettingsController.rollingBeatGridEnabled, false)
            grouping.currentIndex = 1
            grouping.activated(grouping.currentIndex)
            compare(SettingsController.rollingBeatGridGrouping, 8)
        } finally {
            SettingsController.rollingBeatGridEnabled = savedGridEnabled
            SettingsController.rollingBeatGridGrouping = savedGrouping
        }
    }

    function test_visible_grid_and_cue_share_source_time_geometry() {
        var rolling = rollingWithFakes()
        rolling.visibleBeats = 8
        var canvas = findChild(rolling, "rollingMainWaveformCanvas")
        var grid = findChild(rolling, "rollingBeatGrid")
        var cue = findChild(rolling, "rollingMainCueMarker")
        var overview = findChild(rolling, "rollingOverviewWaveform")
        var overviewCue = findChild(rolling, "rollingOverviewCueMarker")
        verify(canvas && grid && cue && overview && overviewCue)
        compare(grid.firstBeatMs, 250)
        compare(grid.bpm, 128)
        compare(grid.grouping, SettingsController.rollingBeatGridGrouping)
        compare(grid.fourBeatColor, Theme.success)
        compare(grid.eightBeatColor, Theme.danger)
        verify(grid.visibleBeatCount() <= 12,
               "renderer enumerates only beats intersecting the viewport")
        compare(grid.isFourBeat(4), true)
        compare(grid.isFourBeat(5), false)
        compare(grid.isEightBeat(8), true)
        compare(grid.isEightBeat(4), false)

        var expectedMainX = rolling.timeToX(fakePlayback.cuePositionMs / 1000)
        compare(Math.round(cue.mapToItem(canvas, cue.width / 2, 0).x),
                Math.round(expectedMainX))
        compare(Math.round(overviewCue.mapToItem(overview, overviewCue.width / 2, 0).x),
                Math.round(overview.pixelForTime(fakePlayback.cuePositionMs)))
    }

    function test_unset_cue_is_hidden_and_hot_cues_share_source_time_geometry() {
        var rolling = rollingWithFakes()
        rolling.visibleBeats = 8
        fakePlayback.cuePositionMs = -1
        fakePlayback.hotCuePositions = [59000, 60500, -1, -1, -1, -1, -1, -1]

        var canvas = findChild(rolling, "rollingMainWaveformCanvas")
        var cue = findChild(rolling, "rollingMainCueMarker")
        var overviewCue = findChild(rolling, "rollingOverviewCueMarker")
        tryVerify(function() {
            return findChild(rolling, "rollingHotCueMarker1") !== null
                   && findChild(rolling, "rollingHotCueMarker2") !== null
                   && findChild(rolling, "rollingHotCueMarker3") !== null
        })
        var hotCue1 = findChild(rolling, "rollingHotCueMarker1")
        var hotCue2 = findChild(rolling, "rollingHotCueMarker2")
        var unsetHotCue = findChild(rolling, "rollingHotCueMarker3")
        verify(canvas && cue && overviewCue && hotCue1 && hotCue2 && unsetHotCue)

        compare(cue.visible, false, "an unset CUE must not appear in a negative viewport")
        compare(overviewCue.visible, false, "an unset CUE must not appear in the overview")
        compare(hotCue1.visible, true)
        compare(hotCue2.visible, true)
        compare(unsetHotCue.visible, false)
        compare(hotCue1.text, "▲1")
        compare(hotCue1.color, Theme.warning)
        compare(Math.round(hotCue1.mapToItem(canvas, hotCue1.width / 2, 0).x),
                Math.round(rolling.timeToX(59)))
        compare(Math.round(hotCue2.mapToItem(canvas, hotCue2.width / 2, 0).x),
                Math.round(rolling.timeToX(60.5)))
        compare(Math.round(hotCue1.mapToItem(canvas, 0, hotCue1.height).y),
                canvas.height - 2, "Hot Cue markers sit at the waveform bottom")
    }

    function test_hidden_grid_stops_paint_requests_and_repaints_when_enabled() {
        var savedGridEnabled = SettingsController.rollingBeatGridEnabled
        try {
            SettingsController.rollingBeatGridEnabled = false
            var rolling = rollingWithFakes()
            var grid = findChild(rolling, "rollingBeatGrid")
            verify(grid)
            wait(50)
            var hiddenRequests = grid.paintRequestCount
            var hiddenPaints = grid.paintPassCount
            fakePlayback.positionMs += 1000
            wait(50)
            compare(grid.paintRequestCount, hiddenRequests,
                    "hidden grid must not schedule work as playback advances")
            compare(grid.paintPassCount, hiddenPaints,
                    "hidden grid must not draw as playback advances")

            SettingsController.rollingBeatGridEnabled = true
            tryVerify(function() {
                return grid.paintRequestCount > hiddenRequests
            }, 500, "showing the grid must request a fresh frame")
            tryVerify(function() {
                return grid.paintPassCount > hiddenPaints
            }, 500, "showing the grid must draw a fresh frame")
        } finally {
            SettingsController.rollingBeatGridEnabled = savedGridEnabled
        }
    }

    function test_profile_grid_on_off_canvas_work() {
        var savedGridEnabled = SettingsController.rollingBeatGridEnabled
        var iterations = 10000
        try {
            SettingsController.rollingBeatGridEnabled = false
            var rolling = rollingWithFakes()
            var grid = findChild(rolling, "rollingBeatGrid")
            verify(grid)
            wait(50)

            var disabledRequests = grid.paintRequestCount
            var disabledPaints = grid.paintPassCount
            var disabledStart = Date.now()
            for (var offIndex = 0; offIndex < iterations; ++offIndex)
                fakePlayback.positionMs = 1000 + offIndex
            var disabledMs = Date.now() - disabledStart
            wait(50)
            compare(grid.paintRequestCount, disabledRequests)
            compare(grid.paintPassCount, disabledPaints)

            SettingsController.rollingBeatGridEnabled = true
            wait(50)
            var enabledRequests = grid.paintRequestCount
            var enabledPaints = grid.paintPassCount
            var enabledStart = Date.now()
            for (var onIndex = 0; onIndex < iterations; ++onIndex)
                fakePlayback.positionMs = 20000 + onIndex
            var enabledMs = Date.now() - enabledStart
            wait(50)
            var requestDelta = grid.paintRequestCount - enabledRequests
            var paintDelta = grid.paintPassCount - enabledPaints
            verify(requestDelta >= iterations)
            verify(paintDelta > 0)
            console.info("GRID_CANVAS_PROFILE iterations=" + iterations
                         + " disabledUiMs=" + disabledMs
                         + " enabledUiMs=" + enabledMs
                         + " enabledRequests=" + requestDelta
                         + " enabledPaintPasses=" + paintDelta)
        } finally {
            SettingsController.rollingBeatGridEnabled = savedGridEnabled
        }
    }

    function test_calibration_popup_drag_and_reopen_position() {
        var rolling = rollingWithFakes()
        var button = findChild(rolling, "rollingGridCalibrationButton")
        var popup = findChild(rolling, "rollingGridCalibrationPopup")
        mouseClick(button)
        tryCompare(popup, "opened", true)
        var handle = findChild(popup, "rollingGridCalibrationDragHandle")
        verify(handle, "calibration needs a title-only drag handle")
        var initialX = popup.x
        var initialY = popup.y
        var buttonTop = button.mapToItem(popup.parent, 0, 0).y
        verify(popup.y + popup.height <= buttonTop)
        mousePress(handle, handle.width / 2, handle.height / 2)
        mouseMove(handle, handle.width / 2 + 55, handle.height / 2 - 35, 30, Qt.LeftButton)
        mouseRelease(handle, handle.width / 2, handle.height / 2, Qt.LeftButton)
        verify(Math.abs(popup.x - initialX) > 10 || Math.abs(popup.y - initialY) > 10)
        verify(popup.x >= 0 && popup.y >= 0)
        verify(popup.x + popup.width <= popup.parent.width)
        verify(popup.y + popup.height <= popup.parent.height)
        mousePress(handle, handle.width / 2, handle.height / 2)
        var corner = popup.parent.mapToItem(handle, 0, 0)
        mouseMove(handle, corner.x, corner.y, 30, Qt.LeftButton)
        corner = popup.parent.mapToItem(handle, 0, 0)
        mouseRelease(handle, corner.x, corner.y, Qt.LeftButton)
        compare(popup.x, 8)
        compare(popup.y, 8)
        var field = findChild(popup, "rollingGridBpmField")
        var draggedX = popup.x
        var draggedY = popup.y
        mouseClick(field)
        compare(popup.x, draggedX)
        compare(popup.y, draggedY)
        popup.close()
        tryCompare(popup, "visible", false)
        mouseClick(button)
        tryCompare(popup, "opened", true)
        compare(popup.x, initialX)
        compare(popup.y, Math.max(8, button.mapToItem(popup.parent, 0, 0).y
                                   - popup.height - 4))
        popup.close()
    }

    function test_cue_hold_release_cancel_and_grid_calibration_actions() {
        var rolling = rollingWithFakes()
        var cueButton = findChild(rolling, "rollingCueButton")
        var setFirst = findChild(rolling, "rollingGridSetFirstBeat")
        var nudgeLeft = findChild(rolling, "rollingGridNudgeLeft")
        var nudgeRight = findChild(rolling, "rollingGridNudgeRight")
        var bpm = findChild(rolling, "rollingGridBpmField")
        var reset = findChild(rolling, "rollingGridReset")
        var status = findChild(rolling, "rollingGridStatusLabel")
        var calibrationButton = findChild(
                    rolling, "rollingGridCalibrationButton")
        var calibrationPopup = findChild(
                    rolling, "rollingGridCalibrationPopup")
        verify(cueButton && setFirst && nudgeLeft && nudgeRight
               && bpm && reset && status && calibrationButton
               && calibrationPopup)
        compare(bpm.height, 28)
        compare(bpm.text, "128.000")
        var play = findChild(rolling, "playPauseButton")
        var playBody = findChild(rolling, "playButtonBody")
        verify(playBody)
        compare(cueButton.width, play.width)
        compare(cueButton.height, play.height)
        compare(playBody.border.color, Theme.success,
                "rolling play ring stays green while paused")
        verify(cueButton.mapToItem(play.parent, cueButton.width, 0).x
               <= play.x + 1)

        mousePress(cueButton, cueButton.width / 2, cueButton.height / 2,
                   Qt.LeftButton)
        compare(fakePlayback.cuePressCount, 1)
        mouseRelease(cueButton, cueButton.width / 2, cueButton.height / 2,
                     Qt.LeftButton)
        compare(fakePlayback.cueReleaseCount, 1)

        mousePress(cueButton, cueButton.width / 2, cueButton.height / 2,
                   Qt.LeftButton)
        compare(fakePlayback.cuePressCount, 2)
        play.clicked()
        compare(fakePlayback.toggleCount, 1)
        compare(fakePlayback.cueCancelCount, 0,
                "moving focus to play must let playback latch the audition")
        rolling.cancelCueHold()
        compare(fakePlayback.cueCancelCount, 1,
                "window/cancel lifecycle ends an outstanding cue hold")
        mouseRelease(cueButton, cueButton.width / 2, cueButton.height / 2,
                     Qt.LeftButton)

        mouseClick(calibrationButton)
        tryCompare(calibrationPopup, "visible", true)
        mouseClick(setFirst)
        compare(fakePlayback.gridFirstBeatCount, 1)
        mouseClick(nudgeLeft)
        mouseClick(nudgeRight)
        compare(fakePlayback.gridNudgeTotal, 0)
        bpm.text = "126.5"
        var speedBeforeGridEdit = fakePlayback.speedRatio
        bpm.editingFinished()
        compare(fakePlayback.gridBpmCount, 1)
        compare(fakePlayback.beatGridBpm, 126.5)
        compare(fakePlayback.speedRatio, speedBeforeGridEdit,
                "grid calibration must not alter playback speed")

        fakePlayback.beatGridEstimatedBpm = true
        tryVerify(function() { return status.text.indexOf("估算") === 0 })
        fakePlayback.beatGridCalibrated = false
        tryCompare(calibrationButton, "text", "估算 120")
        fakePlayback.beatGridBpm = 0
        fakePlayback.sourceBpm = 0
        tryCompare(status, "text", "估算 · 回退 120 BPM")
        mouseClick(reset)
        compare(fakePlayback.gridResetCount, 1)
    }

    function test_header_four_rows_align_as_one_cover_centered_block() {
        var rolling = rollingWithFakes()
        var cover = findChild(rolling, "rollingTrackCover")
        var rows = [findChild(rolling, "rollingTitleRow"),
                    findChild(rolling, "rollingSubtitleRow"),
                    findChild(rolling, "rollingMetadataBadges"),
                    findChild(rolling, "rollingOverviewInteraction")]
        for (var index = 0; index < rows.length; ++index)
            verify(rows[index])
        var top = rows[0].mapToItem(rolling, 0, 0).y
        var bottom = rows[3].mapToItem(rolling, 0, rows[3].height).y
        verify(Math.abs((top + bottom) / 2
                        - cover.mapToItem(rolling, 0, cover.height / 2).y) < 1,
               "title, subtitle, metadata and overview together must center on the cover")
        verify(bottom - top <= cover.height,
               "the overview belongs inside the cover-aligned information block")
        for (var row = 1; row < rows.length; ++row) {
            var gap = rows[row].mapToItem(rolling, 0, 0).y
                    - rows[row - 1].mapToItem(rolling, 0, rows[row - 1].height).y
            var expectedGap = row === 3 ? 4 : (rolling.height < 700 ? 4 : 3)
            verify(Math.abs(gap - expectedGap) < 1,
                   "text rows are uniform while the overview keeps its existing clearance")
        }
    }

    function test_header_three_rows_are_evenly_spaced_and_cover_centered() {
        var rolling = rollingWithFakes()
        compare(findChild(rolling, "rollingTrackSubtitle").verticalAlignment, Text.AlignVCenter)
        var cover = findChild(rolling, "rollingTrackCover")
        var info = findChild(rolling, "rollingHeaderInfo")
        var title = findChild(rolling, "rollingTitleRow")
        var subtitle = findChild(rolling, "rollingSubtitleRow")
        var badges = findChild(rolling, "rollingMetadataBadges")
        verify(cover && info && title && subtitle && badges)
        compare(info.height, rolling.height < 700 ? 96 : 112,
                "equalizing text rows must not move the overview or grow the header")
        compare(title.height, subtitle.height)
        compare(subtitle.height, badges.height)
        var favorite = findChild(rolling, "rollingFavoriteButton")
        var rating = findChild(rolling, "rollingTrackRating")
        compare(favorite.icon.width, 20)
        compare(favorite.icon.height, 20)
        compare(favorite.padding, 0,
                "compact rows must not shrink the enlarged icon through button padding")
        var stars = 0
        for (var child of rating.children) {
            if (child.tint !== undefined) {
                compare(child.width, 16)
                compare(child.height, 16)
                stars++
            }
        }
        compare(stars, 5)
        var center = info.mapToItem(rolling, 0, info.height / 2).y
        verify(Math.abs(center - cover.mapToItem(rolling, 0, cover.height / 2).y) < 1)
        var firstGap = subtitle.mapToItem(rolling, 0, 0).y
                     - title.mapToItem(rolling, 0, title.height).y
        var secondGap = badges.mapToItem(rolling, 0, 0).y
                      - subtitle.mapToItem(rolling, 0, subtitle.height).y
        verify(firstGap >= 3 && Math.abs(firstGap - secondGap) < 1)
        compare(title.mapToItem(rolling, 0, 0).x,
                subtitle.mapToItem(rolling, 0, 0).x)
        compare(subtitle.mapToItem(rolling, 0, 0).x,
                badges.mapToItem(rolling, 0, 0).x)
        verify(findChild(rolling, "rollingOverviewWaveform")
               .mapToItem(rolling, 0, 0).y >= badges.mapToItem(rolling, 0, badges.height).y)
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
        compare(column.width, Theme.playerInspectorWidth)
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
        tryCompare(column, "width", Theme.playerInspectorWidth)
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
        compare(trackList.singleWindowWaveformHeight, 26)
        compare(trackList.trailingColumnGap, 6)

        mainWindow.width = 1000
        mainWindow.height = 720
        wait(0)
        verify(overview.y + overview.height <= waveform.y + 1)
        verify(waveform.y + waveform.height <= bottom.y + 1)
        verify(bottom.y + bottom.height <= library.y + 1)
        verify(library.y + library.height <= rolling.height + 1)
        compare(trackList.trackModel, rolling.filterModel)
        compare(trackList.playlistModel, rolling.playlistModel)
        var trailingNames = ["trackHeaderDuration", "trackHeaderRating",
                             "trackHeaderFavorite"]
        for (var trailingIndex = 0; trailingIndex < trailingNames.length;
             ++trailingIndex) {
            var trailing = findChild(trackList,
                                     trailingNames[trailingIndex])
            verify(trailing && trailing.visible)
            verify(trailing.mapToItem(trackList, trailing.width, 0).x
                   <= trackList.width + 1)
        }
        var controls = findChild(rolling, "playerControls")
        verify(controls)
        var minimumWidthSharedActions = [
            "equalizerButton", "themeModeButton",
            "immersiveActionButton", "miniPlayerButton"
        ]
        for (var actionIndex = 0;
             actionIndex < minimumWidthSharedActions.length; ++actionIndex) {
            var action = findChild(controls,
                                   minimumWidthSharedActions[actionIndex])
            verify(action && action.visible,
                   minimumWidthSharedActions[actionIndex]
                   + " stays available at the rolling minimum width")
        }
        compare(findChild(controls, "waveformModeButton"), null)
        compare(findChild(controls, "listWindowButton").visible, false)
        var transport = findChild(controls, "centerPlaybackControls")
        verify(transport)
        verify(transport.mapToItem(controls, 0, 0).x < controls.width * 0.30,
               "rolling transport belongs on the left side")
        var firstGroup = findChild(rolling, "rollingBeatGridSwitch").parent
        var lastGroup = findChild(rolling, "rollingShellActions")
        verify(lastGroup.mapToItem(bottom, 0, 0).y
               >= firstGroup.mapToItem(bottom, 0, firstGroup.height).y,
                "narrow rolling controls wrap to a second row instead of clipping")
        var rightControls = findChild(rolling, "rollingTempoControls")
        verify(rightControls && rightControls.height > 64)
        var boundedControlNames = [
            "rollingBeatGridSwitch", "rollingBeatGridGrouping",
            "rollingViewportBeats", "rollingZoomMinus", "rollingZoomPlus",
            "rollingZoomReset", "rollingSpeedMinus", "rollingSpeedPlus",
            "rollingTargetBpm", "rollingTempoReset",
            "rollingKeepPitchControl", "rollingGridCalibrationButton",
            "themeModeButton", "immersiveActionButton", "miniPlayerButton"
        ]
        for (var boundedIndex = 0;
             boundedIndex < boundedControlNames.length; ++boundedIndex) {
            var boundedControl = findChild(
                        rolling, boundedControlNames[boundedIndex])
            verify(boundedControl)
            var topLeft = boundedControl.mapToItem(bottom, 0, 0)
            var bottomRight = boundedControl.mapToItem(
                        bottom, boundedControl.width, boundedControl.height)
            verify(topLeft.x >= 15 && topLeft.y >= -1
                   && bottomRight.x <= bottom.width - 15
                   && bottomRight.y <= bottom.height + 1,
                   boundedControlNames[boundedIndex]
                   + " must stay inside the wrapped bottom bar: "
                   + topLeft + " -> " + bottomRight
                   + " in " + bottom.width + "x" + bottom.height)
        }
        var miniAction = findChild(controls, "miniPlayerButton")
        verify(miniAction.mapToItem(bottom, miniAction.width, 0).x
               <= bottom.width - 15, "shell actions retain the right inset")

        mainWindow.width = 1800
        mainWindow.height = 600
        wait(0)
        var playhead = findChild(rolling, "rollingCenterPlayhead")
        compare(Math.round(playhead.mapToItem(waveform,
                                             playhead.width / 2, 0).x),
                Math.round(waveform.width / 2))
    }
}
