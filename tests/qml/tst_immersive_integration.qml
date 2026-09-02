import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "ImmersiveIntegration"
    when: windowShown

    property var mainWindow: null
    property var miniWindow: null
    property int originalShellMode: 0
    property int originalImmersiveMode: 0
    property int originalHostMode: 0
    property bool originalLyricsVisible: false
    property bool originalImmersiveRenderingEnabled: false
    property var originalLyricSettings: ({})
    property var originalDynamicsSettings: ({})
    property var transientLyricsPanel: null
    property var transientWaveformView: null

    QtObject {
        id: waveformPlaybackFake
        property real positionMs: 25000
        property real durationMs: 100000
        function seek(positionMs) { waveformPlaybackFake.positionMs = positionMs }
    }

    QtObject {
        id: waveformSessionFake
        property real durationMs: 100000
        property var layers: ({ mix: [1, 1, 1, 1], bass: [1, 1, 1, 1] })
    }

    QtObject {
        id: queuePlayback
        property var queueTrackIds: ["one", "two", "three"]
        property string currentTrackId: "two"
        property var lastQueue: []
        property string lastTrackId: ""
        function playTrackIds(queue, trackId) {
            lastQueue = queue.slice()
            lastTrackId = trackId
        }
    }

    QtObject {
        id: queueLibrary
        function trackForId(trackId) {
            return {
                "trackId": trackId,
                "title": "Track " + trackId,
                "durationMs": trackId === "two" ? 125000 : 181000,
                "coverUrl": ""
            }
        }
    }

    QtObject {
        id: lyricsFake
        property bool enabled: true
        property int status: LyricsService.Ready
        property string previousLine: "上一句"
        property string currentLine: "当前句"
        property string nextLine: "下一句"
        property string sourceProvider: "Unison"
        property string sourceAttribution: "Lyrics from Unison (https://unison.boidu.dev)"
        property bool synchronizedLyrics: true
        property bool instrumental: false
        property string untimedLyrics: ""
        property var routeNotice: ({})
        property var routeAttempts: []
        property int offsetMs: 0
        property int retryCalls: 0
        property int pauseCalls: 0
        property int importCalls: 0
        property url lastImportUrl: ""
        function retry() { retryCalls += 1 }
        function pauseFollow(milliseconds) { pauseCalls += 1 }
        function importLrc(url) {
            importCalls += 1
            lastImportUrl = url
            return true
        }
    }

    QtObject {
        id: loadingLyricsFake
        property bool enabled: true
        property int status: LyricsService.Loading
        property string previousLine: ""
        property string currentLine: ""
        property string nextLine: ""
        property string sourceProvider: ""
        property string sourceAttribution: ""
        property bool synchronizedLyrics: true
        property bool instrumental: false
        property string untimedLyrics: ""
        property var routeNotice: ({})
        property var routeAttempts: []
        property int offsetMs: 0
        function retry() {}
        function pauseFollow(milliseconds) {}
        function importLrc(url) { return true }
    }

    QtObject {
        id: readyLyricsFake
        property bool enabled: true
        property int status: LyricsService.Ready
        property string previousLine: "B 上一句"
        property string currentLine: "B 当前句"
        property string nextLine: "B 下一句"
        property string sourceProvider: "Unison"
        property string sourceAttribution: ""
        property bool synchronizedLyrics: true
        property bool instrumental: false
        property string untimedLyrics: ""
        property var routeNotice: ({})
        property var routeAttempts: []
        property int offsetMs: 0
        function retry() {}
        function pauseFollow(milliseconds) {}
        function importLrc(url) { return true }
    }

    Component {
        id: queueDrawerComponent
        ImmersiveQueueDrawer {
            width: 340
            height: 700
            playback: queuePlayback
            library: queueLibrary
        }
    }

    Component {
        id: lyricsPanelComponent
        LyricsPanel {
            width: 560
            height: 104
            service: lyricsFake
        }
    }

    Component {
        id: immersiveControlPanelComponent
        ImmersiveControlPanel { currentTab: 2 }
    }

    Component {
        id: sharedWaveformComponent
        SharedWaveformView {
            width: 800
            height: 52
            playback: waveformPlaybackFake
            waveformSession: waveformSessionFake
        }
    }

    function mappedBounds(item, target) {
        var points = [item.mapToItem(target, 0, 0),
                      item.mapToItem(target, item.width, 0),
                      item.mapToItem(target, 0, item.height),
                      item.mapToItem(target, item.width, item.height)]
        var left = points[0].x
        var right = points[0].x
        var top = points[0].y
        var bottom = points[0].y
        for (var index = 1; index < points.length; ++index) {
            left = Math.min(left, points[index].x)
            right = Math.max(right, points[index].x)
            top = Math.min(top, points[index].y)
            bottom = Math.max(bottom, points[index].y)
        }
        return { "left": left, "right": right,
                 "top": top, "bottom": bottom }
    }

    function verifyMappedLyricBounds(item, surface, waveform, label) {
        if (!item.visible)
            return
        var bounds = mappedBounds(item, surface)
        var details = label + " mapped=" + bounds.left.toFixed(2) + ","
                + bounds.top.toFixed(2) + ".." + bounds.right.toFixed(2)
                + "," + bounds.bottom.toFixed(2)
                + " local=" + item.x.toFixed(2) + "," + item.y.toFixed(2)
                + " " + item.width.toFixed(2) + "x" + item.height.toFixed(2)
                + " surface=" + surface.width + "x" + surface.height
                + " waveformY=" + waveform.y.toFixed(2)
        verify(bounds.left >= 20, details)
        verify(bounds.right <= surface.width - 20, details)
        verify(bounds.top >= 56, details)
        verify(bounds.bottom <= waveform.y - 12, details)
    }

    function verifyMappedLyricsAtExistingExtremes(panel, surface, waveform,
                                                   label) {
        var previousLine = findChild(panel, "previousLyricLine")
        var currentLine = findChild(panel, "currentLyricLine")
        var nextLine = findChild(panel, "nextLyricLine")
        verify(previousLine && currentLine && nextLine)
        var endpoints = [0, 100]
        var sizes = [60, 140]
        for (var placement = PlayerExperienceController.Left;
             placement <= PlayerExperienceController.Right; ++placement) {
            PlayerExperienceController.lyricPosition = placement
            for (var xIndex = 0; xIndex < endpoints.length; ++xIndex) {
                PlayerExperienceController.lyricPositionX = endpoints[xIndex]
                for (var yIndex = 0; yIndex < endpoints.length; ++yIndex) {
                    PlayerExperienceController.lyricPositionY = endpoints[yIndex]
                    for (var sizeIndex = 0; sizeIndex < sizes.length;
                         ++sizeIndex) {
                        PlayerExperienceController.lyricSize = sizes[sizeIndex]
                        for (var depthIndex = 0; depthIndex < endpoints.length;
                             ++depthIndex) {
                            PlayerExperienceController.lyricDepth
                                    = endpoints[depthIndex]
                            var stage = currentLine.parent
                            tryVerify(function() {
                                stage.forceLayout()
                                var expectedCurrentY = previousLine.visible
                                        ? previousLine.y + previousLine.height
                                          + stage.spacing : 0
                                var expectedNextY = currentLine.y
                                        + currentLine.height + stage.spacing
                                return Math.abs(currentLine.y
                                                - expectedCurrentY) < 0.01
                                        && (!nextLine.visible
                                            || Math.abs(nextLine.y
                                                        - expectedNextY) < 0.01)
                                        && (!nextLine.visible
                                            || nextLine.y + nextLine.height
                                               <= panel.height + 0.5)
                            }, 200)
                            var state = label + " placement=" + placement
                                    + " x=" + endpoints[xIndex]
                                    + " y=" + endpoints[yIndex]
                                    + " size=" + sizes[sizeIndex]
                                    + " depth=" + endpoints[depthIndex]
                            verifyMappedLyricBounds(previousLine, surface,
                                                    waveform,
                                                    state + " previous")
                            verifyMappedLyricBounds(currentLine, surface,
                                                    waveform,
                                                    state + " current")
                            verifyMappedLyricBounds(nextLine, surface,
                                                    waveform,
                                                    state + " next")
                        }
                    }
                }
            }
        }
    }

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined")
        verify(typeof testMiniWindow !== "undefined")
        mainWindow = testMainWindow
        miniWindow = testMiniWindow
        verify(mainWindow && miniWindow)
        mainWindow.visible = true
        miniWindow.visible = true
        originalShellMode = SettingsController.playerShellMode
        originalImmersiveMode = PlayerExperienceController.immersiveMode
        originalHostMode = PlayerExperienceController.hostMode
        originalLyricsVisible = PlayerExperienceController.lyricsVisible
        originalImmersiveRenderingEnabled = mainWindow.immersiveRenderingEnabled
        originalLyricSettings = {
            "lyricPosition": PlayerExperienceController.lyricPosition,
            "lyricPositionX": PlayerExperienceController.lyricPositionX,
            "lyricPositionY": PlayerExperienceController.lyricPositionY,
            "lyricSize": PlayerExperienceController.lyricSize,
            "lyricClarity": PlayerExperienceController.lyricClarity,
            "lyricDepth": PlayerExperienceController.lyricDepth,
            "lyricOpacity": PlayerExperienceController.lyricOpacity
        }
        originalDynamicsSettings = {
            "inputCompression": PlayerExperienceController.inputCompression,
            "audioResponse": PlayerExperienceController.audioResponse,
            "responseRange": PlayerExperienceController.responseRange,
            "subjectClarity": PlayerExperienceController.subjectClarity,
            "centerHighlight": PlayerExperienceController.centerHighlight,
            "depthOfField": PlayerExperienceController.depthOfField,
            "autoRotateSpeed": PlayerExperienceController.autoRotateSpeed,
            "rhythmSensitivity": PlayerExperienceController.rhythmSensitivity,
            "rhythmStrength": PlayerExperienceController.rhythmStrength,
            "streamHighlightEnabled": PlayerExperienceController.streamHighlightEnabled,
            "songAdaptiveColorEnabled": PlayerExperienceController.songAdaptiveColorEnabled,
            "autoRotate": PlayerExperienceController.autoRotate,
            "idleBreathingEnabled": PlayerExperienceController.idleBreathingEnabled,
            "floatingCubesEnabled": PlayerExperienceController.floatingCubesEnabled,
            "ripplesEnabled": PlayerExperienceController.ripplesEnabled,
            "burstEnabled": PlayerExperienceController.burstEnabled,
            "meteorsEnabled": PlayerExperienceController.meteorsEnabled
        }
        PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        wait(50)
    }

    function cleanup() {
        if (transientLyricsPanel) {
            transientLyricsPanel.destroy()
            transientLyricsPanel = null
        }
        if (transientWaveformView) {
            transientWaveformView.destroy()
            transientWaveformView = null
        }
        SettingsController.playerShellMode = originalShellMode
        PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        PlayerExperienceController.lyricsVisible = false
        PlayerExperienceController.panelVisible = true
        for (var lyricKey in originalLyricSettings)
            PlayerExperienceController[lyricKey] = originalLyricSettings[lyricKey]
        for (var dynamicKey in originalDynamicsSettings)
            PlayerExperienceController[dynamicKey]
                    = originalDynamicsSettings[dynamicKey]
        mainWindow.immersiveRenderingEnabled = originalImmersiveRenderingEnabled
        queuePlayback.lastQueue = []
        queuePlayback.lastTrackId = ""
        lyricsFake.enabled = true
        lyricsFake.status = LyricsService.Ready
        lyricsFake.previousLine = "上一句"
        lyricsFake.currentLine = "当前句"
        lyricsFake.nextLine = "下一句"
        loadingLyricsFake.enabled = true
        loadingLyricsFake.status = LyricsService.Loading
        loadingLyricsFake.previousLine = ""
        loadingLyricsFake.currentLine = ""
        loadingLyricsFake.nextLine = ""
        readyLyricsFake.enabled = true
        readyLyricsFake.status = LyricsService.Ready
        readyLyricsFake.previousLine = "B 上一句"
        readyLyricsFake.currentLine = "B 当前句"
        readyLyricsFake.nextLine = "B 下一句"
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        if (coordinator) {
            if (coordinator.surface) {
                var immersiveLyrics = findChild(coordinator.surface,
                                                "immersiveLyricsPanel")
                if (immersiveLyrics)
                    immersiveLyrics.service = Qt.binding(function() {
                        return LyricsService
                    })
            }
            tryCompare(coordinator, "handoffPhase", 0, 6000)
            if (coordinator.surface && coordinator.surface.terrainItem)
                tryCompare(coordinator.surface.terrainItem,
                           "renderingRequested", false, 6000)
        }
        wait(50)
    }

    function test_independent_window_hides_player_group_and_return_restores_it() {
        WindowController.showListWindow()
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        tryCompare(WindowController, "immersivePresentationActive", true, 2000)
        compare(WindowController.mainVisible, false)
        compare(WindowController.miniVisible, false)
        compare(WindowController.listWindowVisible, false)

        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        var returnButton = findChild(coordinator.surface,
                                     "immersiveReturnToWindowButton")
        verify(returnButton)
        compare(returnButton.display, AbstractButton.IconOnly)
        returnButton.clicked()

        tryCompare(PlayerExperienceController, "immersiveMode",
                   PlayerExperienceController.Off, 1000)
        tryCompare(WindowController, "immersivePresentationActive", false, 2000)
        compare(WindowController.mainVisible, true)
        compare(WindowController.listWindowVisible, true)
    }

    function cleanupTestCase() {
        mainWindow = null
        miniWindow = null
    }

    function test_shared_actions_reuse_one_state_source() {
        var mainActions = findChild(mainWindow, "experienceActions")
        var miniControls = findChild(miniWindow, "miniPlayerControls")
        verify(mainActions && miniControls)
        compare(findChild(mainActions, "themeActionButton"), null)
        compare(findChild(miniControls, "lyricsActionButton"), null)
        compare(findChild(miniControls, "immersiveActionButton"), null)
        compare(findChild(mainActions, "lyricsActionButton").icon.width, 20)
        compare(findChild(mainActions, "lyricsActionButton").icon.height, 20)

        mainActions.compact = true
        compare(findChild(mainActions, "lyricsActionButton").icon.width, 20)
        compare(findChild(mainActions, "lyricsActionButton").icon.height, 20)
        mainActions.compact = false

        PlayerExperienceController.lyricsVisible = false
        findChild(mainActions, "lyricsActionButton").clicked()
        compare(PlayerExperienceController.lyricsVisible, true)

        PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
        var immersiveAction = findChild(mainWindow, "immersiveActionButton")
        verify(immersiveAction)
        immersiveAction.clicked()
        compare(PlayerExperienceController.immersiveMode,
                PlayerExperienceController.TerrainReactor)
        compare(immersiveAction.checked, true)
    }

    function test_plain_text_lyrics_remain_visible_and_timed_source_is_attributed() {
        lyricsFake.status = LyricsService.Ready
        lyricsFake.sourceProvider = "lyrics.ovh"
        lyricsFake.sourceAttribution = ""
        lyricsFake.synchronizedLyrics = false
        var longLines = []
        for (var index = 0; index < 24; ++index)
            longLines.push("long complete plain lyric line " + index
                           + " wraps across the available lyrics panel width")
        lyricsFake.untimedLyrics = longLines.join("\n")
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        wait(20)
        var plainText = findChild(panel, "untimedLyricsText")
        var timingNotice = findChild(panel, "lyricsTimingNotice")
        var source = findChild(panel, "lyricsSourceText")
        var flickable = findChild(panel, "untimedLyricsFlickable")
        verify(plainText)
        verify(timingNotice)
        verify(source)
        verify(flickable)
        verify(plainText.text.indexOf("long complete plain lyric line 23") >= 0)
        compare(plainText.wrapMode, Text.Wrap)
        verify(flickable.contentHeight > flickable.height)
        var originalContentY = flickable.contentY
        flickable.flick(0, -1200)
        tryVerify(function() { return flickable.contentY > originalContentY }, 1500)
        verify(timingNotice.visible,
               "plain timing notice must remain visible after scroll; panel="
               + panel.visible + ", flickable=" + flickable.visible)
        verify(source.text.indexOf("lyrics.ovh") >= 0)
        lyricsFake.synchronizedLyrics = true
        lyricsFake.untimedLyrics = ""
        lyricsFake.sourceProvider = "Unison"
        lyricsFake.sourceAttribution = "Lyrics from Unison (https://unison.boidu.dev)"
        tryCompare(source, "text", lyricsFake.sourceAttribution)
        panel.destroy()
    }

    function test_spatial_plain_text_and_route_failures_remain_readable() {
        lyricsFake.status = LyricsService.Ready
        lyricsFake.previousLine = ""
        lyricsFake.currentLine = ""
        lyricsFake.nextLine = ""
        lyricsFake.sourceProvider = "lyrics.ovh"
        lyricsFake.sourceAttribution = ""
        lyricsFake.synchronizedLyrics = false
        lyricsFake.untimedLyrics = "spatial plain lyric one\nspatial plain lyric two"
        lyricsFake.routeNotice = ({
            providerId: "lrclib", providerName: "LRCLIB",
            diagnostic: "timeout", httpStatus: 0
        })

        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem,
                                                      { spatialMode: true })
        verify(panel)
        wait(20)
        compare(findChild(panel, "cinematicLyricsStage").visible, false)
        verify(findChild(panel, "untimedLyricsFlickable").visible)
        verify(findChild(panel, "untimedLyricsText").text.indexOf(
                   "spatial plain lyric two") >= 0)
        verify(findChild(panel, "lyricsSourceText").visible)
        var routeNotice = findChild(panel, "lyricsRouteNotice")
        verify(routeNotice.visible)
        compare(findChild(routeNotice, "lyricsRouteNoticeText").text,
                "LRCLIB: " + panel.routeReason("timeout"))

        lyricsFake.status = LyricsService.Error
        lyricsFake.untimedLyrics = ""
        lyricsFake.routeAttempts = [
            { providerId: "lrclib", providerName: "LRCLIB",
              diagnostic: "provider-unavailable", httpStatus: 0 }
        ]
        wait(0)
        verify(findChild(panel, "lyricsRouteAttempts").visible)
        panel.destroy()

        lyricsFake.status = LyricsService.Ready
        lyricsFake.previousLine = "上一句"
        lyricsFake.currentLine = "当前句"
        lyricsFake.nextLine = "下一句"
        lyricsFake.sourceProvider = "Unison"
        lyricsFake.sourceAttribution =
                "Lyrics from Unison (https://unison.boidu.dev)"
        lyricsFake.synchronizedLyrics = true
        lyricsFake.routeNotice = ({})
        lyricsFake.routeAttempts = []
    }

    function test_route_reason_distinguishes_runtime_failures() {
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        var genericReason = panel.routeReason("unknown")
        var diagnostics = [
            "timeout", "network-error", "invalid-response",
            "circuit-open", "provider-unavailable"
        ]
        for (var index = 0; index < diagnostics.length; ++index) {
            verify(panel.routeReason(diagnostics[index]) !== genericReason,
                   diagnostics[index] + " must have a specific explanation")
        }
        panel.destroy()
    }

    function test_route_feedback_is_non_modal_safe_and_keeps_controls_available() {
        lyricsFake.status = LyricsService.NotFound
        lyricsFake.routeNotice = ({
            providerId: "lrclib", providerName: "LRCLIB",
            diagnostic: "provider-error", httpStatus: 503
        })
        lyricsFake.routeAttempts = [
            { providerId: "lrclib", providerName: "LRCLIB",
              diagnostic: "provider-error", httpStatus: 503,
              query: "secret-query", path: "C:/secret/music.flac" },
            { providerId: "unison", providerName: "Unison",
              diagnostic: "empty-search", httpStatus: 200,
              query: "secret-query", path: "C:/secret/music.flac" },
            { providerId: "lyrics-ovh", providerName: "lyrics.ovh",
              diagnostic: "not-found", httpStatus: 404,
              query: "secret-query", path: "C:/secret/music.flac" }
        ]
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        wait(20)
        var notice = findChild(panel, "lyricsRouteNotice")
        var attempts = findChild(panel, "lyricsRouteAttempts")
        var attemptRepeater = findChild(panel, "lyricsRouteAttemptRepeater")
        var retry = findChild(panel, "lyricsRetryButton")
        var importButton = findChild(panel, "lyricsImportButton")
        verify(notice && notice.visible,
               "route notice must be visible; panel=" + panel.visible
               + ", notice=" + (notice ? notice.visible : "missing")
               + ", provider=" + lyricsFake.routeNotice.providerName)
        verify(attempts && attempts.visible)
        verify(attemptRepeater)
        compare(attemptRepeater.count, 3)
        compare(notice.modal, undefined)
        for (var index = 0; index < attemptRepeater.count; ++index) {
            var attempt = attemptRepeater.itemAt(index)
            verify(attempt)
            verify(attempt.text.indexOf("secret-query") < 0)
            verify(attempt.text.indexOf("C:/secret") < 0)
            verify(attempt.text.indexOf("provider-error") < 0)
        }
        var firstAttempt = attemptRepeater.itemAt(0)
        verify(firstAttempt.text.indexOf("服务") >= 0
               || firstAttempt.text.indexOf("Service") >= 0)
        verify(retry && retry.enabled)
        verify(importButton && importButton.enabled)
        var retryCalls = lyricsFake.retryCalls
        retry.clicked()
        compare(lyricsFake.retryCalls, retryCalls + 1)
        var importCalls = lyricsFake.importCalls
        var importUrl = Qt.resolvedUrl("route-feedback-import.lrc")
        verify(panel.importSelectedFile(importUrl))
        compare(lyricsFake.importCalls, importCalls + 1)
        compare(lyricsFake.lastImportUrl.toString(), importUrl.toString())
        panel.destroy()
        lyricsFake.status = LyricsService.Ready
        lyricsFake.routeNotice = ({})
        lyricsFake.routeAttempts = []
    }

    function test_lyrics_panel_reuses_shared_tokens_in_dark_and_light_themes() {
        var previousThemeMode = SettingsController.themeMode
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        var surface = findChild(panel, "lyricsPanelSurface")
        var currentLine = findChild(panel, "currentLyricLine")
        var source = findChild(panel, "lyricsSourceText")
        verify(surface && currentLine && source)
        try {
            SettingsController.themeMode = 0
            tryCompare(Theme, "isLight", false)
            compare(surface.color.toString(), Theme.glassSurface.toString())
            compare(surface.border.color.toString(), Theme.glassBorder.toString())
            compare(currentLine.color.toString(), Theme.primaryText.toString())
            compare(source.color.toString(), Theme.textSecondary.toString())

            SettingsController.themeMode = 1
            tryCompare(Theme, "isLight", true)
            compare(surface.color.toString(), Theme.glassSurface.toString())
            compare(surface.border.color.toString(), Theme.glassBorder.toString())
            compare(currentLine.color.toString(), Theme.primaryText.toString())
            compare(source.color.toString(), Theme.textSecondary.toString())
        } finally {
            panel.destroy()
            SettingsController.themeMode = previousThemeMode
        }
    }

    function test_immersive_waveform_drives_rendered_progress_not_only_cursor() {
        transientWaveformView = createTemporaryObject(sharedWaveformComponent,
                                                       testCase)
        verify(transientWaveformView)
        var rendered = findChild(transientWaveformView, "immersiveWaveform")
        verify(rendered)
        compare(rendered.position, 25000)
        waveformPlaybackFake.positionMs = 75000
        compare(rendered.position, 75000)
    }

    function test_one_terrain_item_stays_in_independent_window_for_all_hosts() {
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        verify(coordinator)
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        var terrain = findChild(mainWindow, "terrainReactor")
        verify(terrain)
        verify(terrain.liveRendererCount <= 1)
        var immersiveWindow = findChild(mainWindow, "immersiveVisualWindow")
        verify(immersiveWindow)
        compare(immersiveWindow.visible, true)
        var embeddedHost = findChild(mainWindow, "windowedImmersiveHost")
        verify(embeddedHost)
        verify(coordinator.surface.parent !== embeddedHost)
        verify(coordinator.surface.parent === immersiveWindow.contentItem)

        var hosts = [PlayerExperienceController.Fullscreen,
                      PlayerExperienceController.Desktop,
                     PlayerExperienceController.Windowed]
        for (var index = 0; index < hosts.length; ++index) {
            PlayerExperienceController.hostMode = hosts[index]
            tryCompare(coordinator, "attachedHostMode", hosts[index], 2500)
            compare(coordinator.handoffPhase, 0)
            verify(terrain.liveRendererCount <= 1)
        }
        compare(findChild(mainWindow, "immersiveFullscreenWindow"), null)
        compare(findChild(mainWindow, "immersiveDesktopWindow"), null)
    }

    function test_fullscreen_idle_and_manual_camera_timers_match_contract() {
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        tryCompare(findChild(mainWindow, "immersiveCoordinator"),
                   "attachedHostMode", PlayerExperienceController.Windowed, 2000)
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        var surface = coordinator ? coordinator.surface : null
        verify(surface)
        compare(findChild(surface, "immersiveBrandTitle"), null)
        compare(findChild(surface, "immersivePanelToggleButton"), null)
        var fullscreenButton = findChild(surface, "immersiveFullscreenButton")
        var minimizeButton = findChild(surface, "immersiveMinimizeButton")
        var returnButton = findChild(surface, "immersiveReturnToWindowButton")
        verify(fullscreenButton)
        verify(minimizeButton)
        verify(returnButton)
        compare(fullscreenButton.display, AbstractButton.IconOnly)
        compare(minimizeButton.display, AbstractButton.IconOnly)
        verify(returnButton.x < minimizeButton.x)
        verify(minimizeButton.x < fullscreenButton.x)
        tryCompare(AudioVisualFeatureController, "active",
                   surface.terrainItem.renderingRequested, 1000)
        compare(findChild(surface, "immersivePanelIdleTimer").interval, 3000)
        compare(findChild(surface, "immersivePanelAutoHideTimer").interval, 5000)
        compare(findChild(surface, "immersiveCameraResumeTimer").interval, 4000)
        surface.notePointerActivity()
        compare(surface.panelIdle, false)
        surface.noteManualCameraActivity()
        compare(surface.manualCameraActive, true)

        var immersiveWindow = findChild(mainWindow, "immersiveVisualWindow")
        verify(immersiveWindow)
        minimizeButton.clicked()
        tryCompare(immersiveWindow, "visibility", Window.Minimized, 1500)
        tryCompare(AudioVisualFeatureController, "active", false, 1500)
        immersiveWindow.showNormal()
        tryCompare(immersiveWindow, "visibility", Window.Windowed, 1500)
        tryCompare(AudioVisualFeatureController, "active",
                   surface.terrainItem.renderingRequested, 1500)

        fullscreenButton.clicked()
        tryCompare(PlayerExperienceController, "hostMode",
                   PlayerExperienceController.Fullscreen, 1000)
        var escapeShortcut = findChild(mainWindow, "immersiveEscapeShortcut")
        verify(escapeShortcut)
        escapeShortcut.activated()
        compare(PlayerExperienceController.hostMode,
                PlayerExperienceController.Windowed)
    }

    function test_windowed_immersive_host_exposes_native_move_region_only_before_fullscreen() {
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        var immersiveWindow = findChild(mainWindow, "immersiveVisualWindow")
        var moveRegion = findChild(immersiveWindow,
                                   "immersiveWindowMoveRegion")
        var returnButton = findChild(coordinator.surface,
                                     "immersiveReturnToWindowButton")
        verify(immersiveWindow && moveRegion && returnButton)
        compare(moveRegion.enabled, true)
        verify(moveRegion.mapToItem(coordinator.surface, moveRegion.width, 0).x
               <= returnButton.mapToItem(coordinator.surface, 0, 0).x)

        PlayerExperienceController.hostMode = PlayerExperienceController.Fullscreen
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Fullscreen, 2500)
        compare(moveRegion.enabled, false)

        PlayerExperienceController.hostMode = PlayerExperienceController.Desktop
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Desktop, 2500)
        compare(moveRegion.enabled, false)
    }

    function test_queue_drawer_timers_scope_and_transform_only_magnification() {
        var drawer = queueDrawerComponent.createObject(mainWindow.contentItem)
        verify(drawer)
        compare(findChild(drawer, "queueOpenTimer").interval, 140)
        compare(findChild(drawer, "queueHideTimer").interval, 2000)
        compare(findChild(drawer, "queueTriggerZone").width, 20)
        drawer.requestOpen()
        wait(170)
        compare(drawer.opened, true)
        drawer.dragSuppressed = true
        drawer.requestClose()
        wait(2050)
        compare(drawer.opened, true)
        drawer.dragSuppressed = false

        var list = findChild(drawer, "immersiveQueueList")
        verify(list)
        var delegate = list.itemAtIndex(1)
        verify(delegate)
        var delegateHeight = delegate.height
        delegate.hoveredForQa = true
        tryCompare(delegate, "scale", 1.12)
        compare(delegate.height, delegateHeight)
        delegate.activateForQa()
        compare(queuePlayback.lastTrackId, "two")
        compare(queuePlayback.lastQueue.length, 3)
        compare(queuePlayback.lastQueue[0], "one")
        drawer.destroy()
    }

    function test_v46_panel_exposes_nine_presets_lyrics_and_real_dynamics() {
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        var panel = coordinator.surface
                ? findChild(coordinator.surface, "immersiveControlPanelHost")
                : null
        verify(panel)
        compare(panel.currentTab, 0)
        verify(panel.height < 500)
        var presetCards = []
        for (var preset = 0; preset < 9; ++preset) {
            var presetCard = findChild(panel, "immersivePresetCard" + preset)
            verify(presetCard)
            presetCards.push(presetCard)
        }
        verify(presetCards[0].width < panel.width * 0.4)
        compare(presetCards[0].y, presetCards[1].y)
        compare(presetCards[1].y, presetCards[2].y)
        verify(presetCards[1].x > presetCards[0].x)
        verify(presetCards[2].x > presetCards[1].x)
        verify(presetCards[3].y > presetCards[0].y)
        verify(presetCards[6].y > presetCards[3].y)
        var panelScroll = findChild(panel, "immersivePanelScroll")
        verify(panelScroll)
        verify(panelScroll.contentWidth <= panelScroll.width)
        compare(panelScroll.contentItem.contentX, 0)

        verify(findChild(panel, "immersiveLyricsVisibleSwitch"))
        verify(findChild(panel, "lyricSlider_lyricPositionX"))
        verify(findChild(panel, "lyricSlider_lyricPositionY"))
        verify(findChild(panel, "lyricSlider_lyricSize"))
        verify(findChild(panel, "lyricSlider_lyricDepth"))
        verify(findChild(panel, "dynamicSlider_responseRange"))
        verify(findChild(panel, "dynamicSlider_rhythmStrength"))
        verify(findChild(panel, "effectToggle_burstEnabled"))
        verify(findChild(panel, "effectToggle_streamHighlightEnabled"))
        for (var feature = 0; feature < 5; ++feature) {
            var featureCard = findChild(panel, "semanticFeature" + feature)
            var featureBar = findChild(panel, "semanticFeatureBar" + feature)
            verify(featureCard)
            verify(featureBar)
            verify(featureBar.value >= 0 && featureBar.value <= 1)
        }
        mainWindow.immersiveRenderingEnabled = true
        tryVerify(function() {
            return coordinator.surface.terrainItem
                    && coordinator.surface.terrainItem.useSyntheticFeatures
                       !== undefined
        }, 2000)
        var terrain = coordinator.surface.terrainItem
        terrain.useSyntheticFeatures = true
        terrain.setSyntheticFeatures([1.0, 0.9, 0.8, 0.7,
                                      0.6, 0.5, 0.4, 0.3],
                                     0.72, 0.64, true, false)
        tryVerify(function() {
            return Math.abs(findChild(panel, "semanticFeatureBar0").value
                            - 3.4 / 4.6) < 0.01
        }, 1000)
        verify(Math.abs(findChild(panel, "semanticFeatureBar1").value
                        - 1.2 / 4.6) < 0.01)
        verify(findChild(panel, "semanticFeatureBar2").value > 0.45)
        verify(findChild(panel, "semanticFeatureBar2").value <= 0.651)
        verify(Math.abs(findChild(panel, "semanticFeatureBar3").value
                        - 0.906) < 0.01)
        verify(findChild(panel, "semanticFeatureBar4").value > 0.606)
        verify(findChild(panel, "semanticFeatureBar4").value <= 0.707)
        tryVerify(function() {
            return Math.abs(findChild(panel, "semanticFeatureBar2").value
                            - 0.45) < 0.015
                    && Math.abs(findChild(panel, "semanticFeatureBar4").value
                                - 0.606) < 0.015
        }, 1000)
        mainWindow.immersiveRenderingEnabled = originalImmersiveRenderingEnabled
        verify(findChild(panel, "dynamicSlider_terrainAmplitude"))
        compare(PlayerExperienceController.responseRange, 100)
        compare(PlayerExperienceController.rhythmStrength, 30)

        var fallbackMessage = findChild(coordinator.surface,
                                        "immersiveRenderFallbackMessage")
        verify(fallbackMessage)
        tryCompare(fallbackMessage, "visible", false, 1000)

        var colorSwatch = findChild(panel, "immersiveColorSwatch0")
        var colorPicker = findChild(panel, "immersiveColorPicker")
        verify(colorSwatch && colorPicker)
        var originalCoolColor = PlayerExperienceController.coolColor
        PlayerExperienceController.songAdaptiveColorEnabled = true
        colorSwatch.clicked()
        compare(panel.editingColorProperty, "coolColor")
        verify(colorPicker.visible)
        colorPicker.selectedColor = "#123456"
        colorPicker.accept()
        compare(PlayerExperienceController.coolColor.toLowerCase(), "#123456")
        compare(PlayerExperienceController.songAdaptiveColorEnabled, false)
        colorPicker.close()
        PlayerExperienceController.coolColor = originalCoolColor

        panel.currentTab = 2
        tryVerify(function() { return panel.height > 590 }, 500)
        verify(panel.height <= panel.parent.height - 108)
        var autoRotateToggle = findChild(panel, "effectToggle_autoRotate")
        verify(autoRotateToggle)
        PlayerExperienceController.autoRotate = 0
        autoRotateToggle.checked = true
        autoRotateToggle.toggled()
        verify(PlayerExperienceController.autoRotate > 1)
    }

    function test_shared_waveform_session_is_injected_into_mini_and_immersive() {
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        tryCompare(findChild(mainWindow, "immersiveCoordinator"),
                   "attachedHostMode", PlayerExperienceController.Windowed, 2000)
        var session = findChild(mainWindow, "sharedWaveformSession")
        var miniControls = findChild(miniWindow, "miniPlayerControls")
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        var surface = coordinator ? coordinator.surface : null
        var immersiveWaveform = surface
                ? findChild(surface, "immersiveWaveformHost") : null
        var nativeWaveform = immersiveWaveform
                ? findChild(immersiveWaveform, "immersiveWaveform") : null
        var playbackGuide = immersiveWaveform
                ? findChild(immersiveWaveform,
                            "immersiveWaveformPlaybackGuide") : null
        var playbackFocus = immersiveWaveform
                ? findChild(immersiveWaveform,
                            "immersiveWaveformPlaybackFocusDot") : null
        verify(session && miniControls && immersiveWaveform && nativeWaveform)
        compare(playbackGuide, null)
        compare(playbackFocus, null)
        compare(miniWindow.waveformSession, session)
        compare(miniControls.waveformSession, session)
        compare(immersiveWaveform.waveformSession, session)
        compare(String(nativeWaveform.lowColor), "#8b3dff")
        compare(String(nativeWaveform.midColor), "#ffb000")
        compare(String(nativeWaveform.highColor), "#002fa7")
        compare(nativeWaveform.frequencyUnplayedOpacity, 0.88)
    }

    function test_three_line_spatial_lyrics_support_position_and_scale() {
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        compare(findChild(panel, "previousLyricLine").text, "上一句")
        compare(findChild(panel, "currentLyricLine").text, "当前句")
        compare(findChild(panel, "nextLyricLine").text, "下一句")
        panel.noteManualScroll()
        compare(lyricsFake.pauseCalls, 1)
        panel.spatialMode = true
        panel.fullscreen = true
        panel.placement = PlayerExperienceController.Left
        panel.lyricSize = 135
        panel.lyricOpacity = 72
        compare(panel.visible, true)
        compare(panel.placement, PlayerExperienceController.Left)
        compare(panel.sizeScale, 1.35)
        verify(Math.abs(panel.opacity - 0.72) < 0.001)
        panel.destroy()
    }

    function test_spatial_lyrics_have_bounded_cinematic_hierarchy() {
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        panel.width = 190
        panel.height = 220
        panel.spatialMode = true
        panel.depth = 100

        var stage = findChild(panel, "cinematicLyricsStage")
        var perspective = findChild(panel, "cinematicLyricsPerspective")
        var previousLine = findChild(panel, "previousLyricLine")
        var currentLine = findChild(panel, "currentLyricLine")
        var nextLine = findChild(panel, "nextLyricLine")
        verify(stage && perspective)
        verify(previousLine && currentLine && nextLine)
        verify(currentLine.scale > previousLine.scale)
        verify(currentLine.scale > nextLine.scale)
        verify(currentLine.opacity > previousLine.opacity)
        verify(currentLine.opacity > nextLine.opacity)
        compare(previousLine.color.toString().toLowerCase(),
                Theme.onBrandGradientText.toString().toLowerCase())
        compare(nextLine.color.toString().toLowerCase(),
                Theme.onBrandGradientText.toString().toLowerCase())
        panel.placement = PlayerExperienceController.Center
        compare(currentLine.color.toString().toLowerCase(),
                Theme.onBrandGradientText.toString().toLowerCase())
        panel.placement = PlayerExperienceController.Left
        compare(currentLine.color.toString().toLowerCase(),
                PlayerExperienceController.warmColor.toString().toLowerCase())

        lyricsFake.currentLine = "这是一段用于验证沉浸歌词在狭窄空间中最多显示两行并在末尾省略的很长歌词文本"
        compare(currentLine.wrapMode, Text.Wrap)
        compare(currentLine.maximumLineCount, 2)
        compare(currentLine.elide, Text.ElideRight)
        tryVerify(function() { return currentLine.lineCount <= 2 }, 500)
        panel.destroy()
    }

    function test_spatial_lyric_placement_is_mirrored_and_non_spatial_stays_flat() {
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        panel.spatialMode = true
        panel.depth = 100
        var perspective = findChild(panel, "cinematicLyricsPerspective")
        var previousLine = findChild(panel, "previousLyricLine")
        var currentLine = findChild(panel, "currentLyricLine")
        var nextLine = findChild(panel, "nextLyricLine")
        verify(perspective && previousLine && currentLine && nextLine)

        panel.placement = PlayerExperienceController.Left
        var leftAngle = perspective.angle
        compare(currentLine.horizontalAlignment, Text.AlignLeft)
        verify(leftAngle < 0)
        verify(Math.abs(leftAngle) <= 18)

        panel.placement = PlayerExperienceController.Right
        var rightAngle = perspective.angle
        compare(currentLine.horizontalAlignment, Text.AlignRight)
        verify(rightAngle > 0)
        verify(Math.abs(rightAngle) <= 18)
        verify(Math.abs(leftAngle + rightAngle) < 0.001)

        panel.placement = PlayerExperienceController.Center
        compare(currentLine.horizontalAlignment, Text.AlignHCenter)
        compare(perspective.angle, 0)

        panel.spatialMode = false
        panel.placement = PlayerExperienceController.Left
        compare(perspective.angle, 0)
        compare(currentLine.wrapMode, Text.NoWrap)
        compare(currentLine.maximumLineCount, 1)
        compare(currentLine.scale, 1)
        compare(previousLine.opacity, 1)
        compare(currentLine.opacity, 1)
        compare(nextLine.opacity, 1)
        verify(Math.abs(previousLine.scale - 0.92) < 0.001)
        verify(Math.abs(nextLine.scale - 0.86) < 0.001)
        panel.destroy()
    }

    function test_real_immersive_surface_keeps_lyrics_in_camera_safe_zone() {
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        PlayerExperienceController.lyricsVisible = true
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        var surface = coordinator.surface
        var panel = findChild(surface, "immersiveLyricsPanel")
        var waveform = findChild(surface, "immersiveWaveformHost")
        verify(surface && panel && waveform)
        panel.service = lyricsFake
        lyricsFake.status = LyricsService.Ready
        lyricsFake.previousLine = "很长的上一句歌词用于验证真实变换后的两个视觉行仍留在镜头安全区内"
        lyricsFake.currentLine = "很长的当前歌词用于验证缩放透视后的两个视觉行不会越过安全边界"
        lyricsFake.nextLine = "很长的下一句歌词用于验证真实变换后的两个视觉行仍位于波形上方"
        var settle = findChild(panel, "cinematicLyricsSettleAnimation")
        tryCompare(settle, "running", false, 400)
        verifyMappedLyricsAtExistingExtremes(panel, surface, waveform,
                                              "ready")

        lyricsFake.previousLine = ""
        lyricsFake.nextLine = ""
        lyricsFake.currentLine = ""
        lyricsFake.status = LyricsService.Loading
        compare(findChild(panel, "currentLyricLine").text,
                panel.statusText())
        verifyMappedLyricsAtExistingExtremes(panel, surface, waveform,
                                              "status")
    }

    function test_spatial_lyrics_settle_latest_line_without_residual_animation() {
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        panel.spatialMode = true
        var currentLine = findChild(panel, "currentLyricLine")
        var previousLine = findChild(panel, "previousLyricLine")
        var nextLine = findChild(panel, "nextLyricLine")
        var settle = findChild(panel, "cinematicLyricsSettleAnimation")
        verify(currentLine && previousLine && nextLine && settle)
        var stableScale = currentLine.scale

        lyricsFake.currentLine = "进入的新歌词"
        compare(currentLine.text, "进入的新歌词")
        tryCompare(settle, "running", true, 50)
        verify(currentLine.opacity < 1)
        verify(currentLine.scale < stableScale)
        tryCompare(settle, "running", false, 400)
        verify(Math.abs(currentLine.opacity - 1) < 0.001)
        verify(Math.abs(currentLine.scale - stableScale) < 0.001)

        lyricsFake.currentLine = "快速一"
        wait(20)
        lyricsFake.currentLine = "快速二"
        wait(20)
        lyricsFake.currentLine = "最终歌词"
        compare(currentLine.text, "最终歌词")
        tryCompare(settle, "running", false, 400)
        compare(currentLine.text, "最终歌词")
        verify(Math.abs(currentLine.opacity - 1) < 0.001)
        verify(Math.abs(currentLine.scale - stableScale) < 0.001)

        lyricsFake.previousLine = ""
        lyricsFake.nextLine = ""
        compare(previousLine.visible, false)
        compare(nextLine.visible, false)

        lyricsFake.currentLine = ""
        var fallbackStatuses = [LyricsService.Loading,
                                LyricsService.NotFound,
                                LyricsService.Offline,
                                LyricsService.Error]
        for (var statusIndex = 0; statusIndex < fallbackStatuses.length;
             ++statusIndex) {
            lyricsFake.status = fallbackStatuses[statusIndex]
            verify(currentLine.text.length > 0)
            compare(settle.running, false)
            verify(Math.abs(currentLine.opacity - 1) < 0.001)
        }

        lyricsFake.status = LyricsService.Ready
        panel.spatialMode = false
        lyricsFake.currentLine = "普通窗口歌词"
        compare(settle.running, false)
        compare(currentLine.opacity, 1)
        compare(currentLine.scale, 1)

        panel.spatialMode = true
        panel.enabled = false
        lyricsFake.currentLine = "禁用时歌词"
        compare(settle.running, false)
        panel.enabled = true
        lyricsFake.enabled = false
        lyricsFake.currentLine = "隐藏时歌词"
        compare(settle.running, false)
        panel.destroy()
    }

    function test_replacing_lyrics_service_resets_and_restarts_only_eligible_text() {
        transientLyricsPanel = lyricsPanelComponent.createObject(
                    mainWindow.contentItem)
        verify(transientLyricsPanel)
        transientLyricsPanel.spatialMode = true
        var currentLine = findChild(transientLyricsPanel, "currentLyricLine")
        var settle = findChild(transientLyricsPanel,
                               "cinematicLyricsSettleAnimation")
        verify(currentLine && settle)
        var stableScale = currentLine.scale

        lyricsFake.currentLine = "A 正在进入"
        tryCompare(settle, "running", true, 50)
        transientLyricsPanel.service = loadingLyricsFake
        compare(currentLine.text, transientLyricsPanel.statusText())
        compare(settle.running, false)
        compare(currentLine.opacity, 1)
        verify(Math.abs(currentLine.scale - stableScale) < 0.001)

        lyricsFake.currentLine = "A 的过期回调"
        compare(settle.running, false)
        compare(currentLine.text, transientLyricsPanel.statusText())

        readyLyricsFake.enabled = false
        transientLyricsPanel.service = readyLyricsFake
        compare(settle.running, false)
        readyLyricsFake.enabled = true
        readyLyricsFake.currentLine = ""
        transientLyricsPanel.service = loadingLyricsFake
        transientLyricsPanel.service = readyLyricsFake
        compare(settle.running, false)

        readyLyricsFake.currentLine = "B 新服务歌词"
        transientLyricsPanel.service = loadingLyricsFake
        transientLyricsPanel.service = readyLyricsFake
        compare(currentLine.text, "B 新服务歌词")
        tryCompare(settle, "running", true, 50)
        verify(currentLine.opacity < 1)
        readyLyricsFake.currentLine = "B 最终歌词"
        compare(currentLine.text, "B 最终歌词")
        tryCompare(settle, "running", false, 400)
        compare(currentLine.text, "B 最终歌词")
        compare(currentLine.opacity, 1)
        verify(Math.abs(currentLine.scale - stableScale) < 0.001)

        readyLyricsFake.currentLine = "B 再次进入"
        tryCompare(settle, "running", true, 50)
        transientLyricsPanel.service = null
        compare(settle.running, false)
        compare(currentLine.text, "")
        compare(currentLine.opacity, 1)
        transientLyricsPanel.destroy()
        transientLyricsPanel = null
    }

    function test_dynamics_controls_are_grouped_by_meaning_and_remain_wired() {
        var panel = immersiveControlPanelComponent.createObject(
                    mainWindow.contentItem)
        verify(panel)
        var terrainGroup = findChild(panel, "dynamicsTerrainGroup")
        var lightGroup = findChild(panel, "dynamicsLightGroup")
        var motionGroup = findChild(panel, "dynamicsMotionGroup")
        var impactGroup = findChild(panel, "dynamicsImpactGroup")
        verify(terrainGroup && lightGroup && motionGroup && impactGroup)

        var terrainKeys = ["terrainAmplitude", "inputCompression", "audioResponse",
                           "responseRange", "subjectClarity"]
        var lightKeys = ["centerHighlight", "depthOfField"]
        var motionKeys = ["autoRotateSpeed", "rhythmSensitivity"]
        for (var terrainIndex = 0; terrainIndex < terrainKeys.length;
             ++terrainIndex)
            verify(findChild(terrainGroup,
                             "dynamicSlider_" + terrainKeys[terrainIndex]))
        for (var lightIndex = 0; lightIndex < lightKeys.length; ++lightIndex)
            verify(findChild(lightGroup,
                             "dynamicSlider_" + lightKeys[lightIndex]))
        for (var motionIndex = 0; motionIndex < motionKeys.length;
             ++motionIndex)
            verify(findChild(motionGroup,
                             "dynamicSlider_" + motionKeys[motionIndex]))
        verify(findChild(impactGroup, "dynamicSlider_rhythmStrength"))
        verify(findChild(lightGroup, "effectToggle_streamHighlightEnabled"))
        verify(findChild(lightGroup,
                         "effectToggle_songAdaptiveColorEnabled"))
        verify(findChild(motionGroup, "effectToggle_autoRotate"))
        verify(findChild(motionGroup, "effectToggle_idleBreathingEnabled"))
        verify(findChild(motionGroup, "effectToggle_floatingCubesEnabled"))
        verify(findChild(impactGroup, "effectToggle_ripplesEnabled"))
        verify(findChild(impactGroup, "effectToggle_burstEnabled"))
        verify(findChild(impactGroup, "effectToggle_meteorsEnabled"))

        var terrainSlider = findChild(terrainGroup,
                                      "dynamicSlider_inputCompression")
        compare(terrainSlider.from, 20)
        compare(terrainSlider.to, 150)
        terrainSlider.value = 73
        terrainSlider.moved()
        compare(PlayerExperienceController.inputCompression, 73)

        var heightSlider = findChild(terrainGroup,
                                     "dynamicSlider_terrainAmplitude")
        compare(heightSlider.from, 0)
        compare(heightSlider.to, 100)
        heightSlider.value = 84
        heightSlider.moved()
        compare(PlayerExperienceController.terrainAmplitude, 84)

        var lightSlider = findChild(lightGroup,
                                    "dynamicSlider_centerHighlight")
        compare(lightSlider.from, 0)
        compare(lightSlider.to, 100)
        lightSlider.value = 41
        lightSlider.moved()
        compare(PlayerExperienceController.centerHighlight, 41)

        var motionSlider = findChild(motionGroup,
                                     "dynamicSlider_autoRotateSpeed")
        compare(motionSlider.from, 0)
        compare(motionSlider.to, 100)
        motionSlider.value = 37
        motionSlider.moved()
        compare(PlayerExperienceController.autoRotateSpeed, 37)

        var impactSlider = findChild(impactGroup,
                                     "dynamicSlider_rhythmStrength")
        compare(impactSlider.from, 0)
        compare(impactSlider.to, 140)
        impactSlider.value = 63
        impactSlider.moved()
        compare(PlayerExperienceController.rhythmStrength, 63)

        var lightToggle = findChild(lightGroup,
                                    "effectToggle_streamHighlightEnabled")
        var lightToggleValue = !PlayerExperienceController.streamHighlightEnabled
        lightToggle.checked = lightToggleValue
        lightToggle.toggled()
        compare(PlayerExperienceController.streamHighlightEnabled,
                lightToggleValue)

        var motionToggle = findChild(motionGroup,
                                     "effectToggle_idleBreathingEnabled")
        var motionToggleValue = !PlayerExperienceController.idleBreathingEnabled
        motionToggle.checked = motionToggleValue
        motionToggle.toggled()
        compare(PlayerExperienceController.idleBreathingEnabled,
                motionToggleValue)

        var impactToggle = findChild(impactGroup,
                                     "effectToggle_burstEnabled")
        var impactToggleValue = !PlayerExperienceController.burstEnabled
        impactToggle.checked = impactToggleValue
        impactToggle.toggled()
        compare(PlayerExperienceController.burstEnabled, impactToggleValue)
        panel.destroy()
    }

    function test_lyrics_switch_drives_service_and_below_list_panel() {
        SettingsController.playerShellMode = 2
        wait(20)
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.Off
        PlayerExperienceController.lyricsVisible = false
        var normalPanel = findChild(mainWindow, "normalLyricsPanel")
        var rollingPanel = findChild(mainWindow, "rollingLyricsPanel")
        verify(normalPanel && rollingPanel)
        compare(normalPanel.visible, false)
        compare(rollingPanel.visible, false)
        compare(LyricsService.enabled, false)

        var rollingLyricsTab = findChild(mainWindow,
                                         "rollingLyricsTabButton")
        var rollingToggle = findChild(mainWindow,
                                      "rollingSidePanelToggleButton")
        verify(rollingLyricsTab && rollingToggle)
        mouseClick(rollingLyricsTab)
        tryCompare(LyricsService, "enabled", true, 500)
        tryCompare(rollingPanel, "visible", true, 500)

        mouseClick(rollingToggle)
        tryCompare(LyricsService, "enabled", false, 500)
        mouseClick(rollingToggle)
        tryCompare(LyricsService, "enabled", true, 500)

        SettingsController.playerShellMode = 1
        wait(20)
        SettingsController.playerShellMode = 2
        wait(20)
        rollingPanel = findChild(mainWindow, "rollingLyricsPanel")
        verify(rollingPanel)
        tryCompare(rollingPanel, "visible", true, 500)
        tryCompare(LyricsService, "enabled", true, 500)

        var rollingTagTab = findChild(mainWindow, "rollingTagTabButton")
        rollingToggle = findChild(mainWindow,
                                  "rollingSidePanelToggleButton")
        verify(rollingTagTab && rollingToggle)
        mouseClick(rollingTagTab)
        mouseClick(rollingToggle)
        tryCompare(rollingPanel, "visible", false, 500)

        PlayerExperienceController.lyricsVisible = true
        tryCompare(LyricsService, "enabled", true, 500)
        tryCompare(rollingPanel, "visible", true, 500)
        compare(normalPanel.visible, false)
    }

    function test_integrated_lyrics_button_recovers_manually_changed_panel() {
        SettingsController.playerShellMode = 1
        wait(20)
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.Off
        PlayerExperienceController.lyricsVisible = false

        var panel = findChild(mainWindow, "integratedLyricsPanel")
        var lyricsTab = findChild(mainWindow, "integratedLyricsTabButton")
        var tagTab = findChild(mainWindow, "integratedTagTabButton")
        var toggle = findChild(mainWindow,
                               "integratedSidePanelToggleButton")
        verify(panel && lyricsTab && tagTab && toggle)

        mouseClick(lyricsTab)
        mouseClick(tagTab)
        if (mainWindow.integratedSidePanelExpanded)
            mouseClick(toggle)
        tryCompare(panel, "visible", false, 500)

        PlayerExperienceController.lyricsVisible = true
        tryCompare(panel, "visible", true, 500)

        SettingsController.playerShellMode = 0
        wait(20)
        SettingsController.playerShellMode = 1
        wait(20)
        panel = findChild(mainWindow, "integratedLyricsPanel")
        verify(panel)
        tryCompare(panel, "visible", true, 500)
    }
}
