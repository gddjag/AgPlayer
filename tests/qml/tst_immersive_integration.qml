import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
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
    property string originalThemeId: ""
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
        id: immersiveShortcutSpyComponent
        SignalSpy { signalName: "activated" }
    }

    Component {
        id: immersiveShortcutEditingComponent
        Item {
            width: 240
            height: 100
            z: 100
            TextField {
                objectName: "shortcutEditingText"
                width: 220
                text: "ab"
            }
            ThemedSlider {
                objectName: "shortcutEditingSlider"
                y: 50
                width: 220
                from: 0
                to: 10
                stepSize: 1
                value: 5
            }
        }
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
                            var state = label + " placement=" + placement
                                    + " x=" + endpoints[xIndex]
                                    + " y=" + endpoints[yIndex]
                                    + " size=" + sizes[sizeIndex]
                                    + " depth=" + endpoints[depthIndex]
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
                            }, 200, state + " column layout")
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
        originalThemeId = PlayerExperienceController.themeId
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
            "colorMode": PlayerExperienceController.colorMode,
            "coolColor": PlayerExperienceController.coolColor,
            "warmColor": PlayerExperienceController.warmColor,
            "accentColor": PlayerExperienceController.accentColor,
            "peakColor": PlayerExperienceController.peakColor,
            "baseColor": PlayerExperienceController.baseColor,
            "terrainAmplitude": PlayerExperienceController.terrainAmplitude,
            "motionResponse": PlayerExperienceController.motionResponse,
            "gradientLayers": PlayerExperienceController.gradientLayers,
            "glowIntensity": PlayerExperienceController.glowIntensity,
            "cinemaShake": PlayerExperienceController.cinemaShake,
            "autoRotate": PlayerExperienceController.autoRotate,
            "peakBoost": PlayerExperienceController.peakBoost,
            "visualEqGains": PlayerExperienceController.visualEqGains,
            "inputCompression": PlayerExperienceController.inputCompression,
            "audioResponse": PlayerExperienceController.audioResponse,
            "responseRange": PlayerExperienceController.responseRange,
            "subjectClarity": PlayerExperienceController.subjectClarity,
            "centerHighlight": PlayerExperienceController.centerHighlight,
            "depthOfField": PlayerExperienceController.depthOfField,
            "autoRotateSpeed": PlayerExperienceController.autoRotateSpeed,
            "rhythmSensitivity": PlayerExperienceController.rhythmSensitivity,
            "materialMode": PlayerExperienceController.materialMode,
            "materialSoftness": PlayerExperienceController.materialSoftness,
            "jellyElasticity": PlayerExperienceController.jellyElasticity,
            "inkDensity": PlayerExperienceController.inkDensity,
            "rippleStrength": PlayerExperienceController.rippleStrength,
            "rippleWidth": PlayerExperienceController.rippleWidth,
            "rippleDecay": PlayerExperienceController.rippleDecay,
            "columnSize": PlayerExperienceController.columnSize,
            "columnOpacity": PlayerExperienceController.columnOpacity,
            "columnInnerLight": PlayerExperienceController.columnInnerLight,
            "columnLightSpill": PlayerExperienceController.columnLightSpill,
            "columnLightRadius": PlayerExperienceController.columnLightRadius,
            "reactorBrightness": PlayerExperienceController.reactorBrightness,
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

    function windowedPresetPanel() {
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
        PlayerExperienceController.panelVisible = true
        coordinator.surface.revealPanelFromHotCorner()
        panel.collapsed = false
        panel.currentTab = 0
        tryCompare(panel, "currentTab", 0)
        tryCompare(panel, "opacity", 1)
        tryCompare(panel, "visible", true)
        // The page is lazily laid out, then opens with a 180 ms height animation.
        wait(220)
        tryVerify(function() {
            return Math.abs(panel.height - Math.min(panel.expandedHeight,
                                      panel.parent.height - 108)) < 0.5
        })
        return panel
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
        if (originalThemeId.length > 0)
            PlayerExperienceController.applyTheme(originalThemeId)
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

    function test_immersive_icon_uses_uploaded_static_svg_and_follows_theme() {
        var action = findChild(mainWindow, "immersiveActionButton")
        verify(action)
        var icon = findChild(action, "animatedImmersiveIcon")
        verify(icon, "The three player themes must reuse the uploaded immersive icon")
        compare(icon.width, 20)
        compare(icon.height, 20)
        var uploadedPaths = [
            findChild(icon, "uploadedImmersivePath1"),
            findChild(icon, "uploadedImmersivePath2"),
            findChild(icon, "uploadedImmersivePath3"),
            findChild(icon, "uploadedImmersivePath4")
        ]
        for (var pathIndex = 0; pathIndex < uploadedPaths.length; ++pathIndex) {
            verify(uploadedPaths[pathIndex])
            compare(uploadedPaths[pathIndex].preferredRendererType,
                    Shape.CurveRenderer,
                    "Small immersive paths must use analytic curve antialiasing")
        }
        var previousTheme = SettingsController.themeMode
        try {
            for (var mode = 0; mode < 2; ++mode) {
                SettingsController.themeMode = mode
                tryCompare(Theme, "isLight", mode === 1)
                compare(icon.color.toString(), action.checked
                        ? Theme.iconAccent.toString() : Theme.iconPrimary.toString())
            }
            compare(icon.animating, false)
            var before = icon.phase
            wait(100)
            compare(icon.phase, before)
            action.visible = false
            compare(icon.animating, false)
            var stopped = icon.phase
            wait(100)
            compare(icon.phase, stopped)
            action.visible = true
            compare(icon.animating, false)
        } finally {
            action.visible = true
            SettingsController.themeMode = previousTheme
        }
    }

    function test_shared_actions_reuse_one_state_source() {
        var mainActions = findChild(mainWindow, "experienceActions")
        var miniControls = findChild(miniWindow, "miniPlayerControls")
        verify(mainActions && miniControls)
        compare(findChild(mainActions, "themeActionButton"), null)
        compare(findChild(miniControls, "lyricsActionButton"), null)
        compare(findChild(miniControls, "immersiveActionButton"), null)
        compare(findChild(mainActions, "lyricsActionButton").icon.width, 22)
        compare(findChild(mainActions, "lyricsActionButton").icon.height, 22)

        mainActions.compact = true
        compare(findChild(mainActions, "lyricsActionButton").icon.width, 22)
        compare(findChild(mainActions, "lyricsActionButton").icon.height, 22)
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
        compare(timingNotice, null)
        verify(source)
        verify(flickable)
        verify(plainText.text.indexOf("long complete plain lyric line 23") >= 0)
        compare(plainText.wrapMode, Text.Wrap)
        verify(flickable.contentHeight > flickable.height)
        compare(plainText.y, flickable.height / 2)
        compare(flickable.contentHeight,
                plainText.height + flickable.height)
        var originalContentY = flickable.contentY
        flickable.flick(0, -1200)
        tryVerify(function() { return flickable.contentY > originalContentY }, 1500)
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
        compare(findChild(panel, "lyricsSourceText").visible, false)
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
        var timeline = findChild(panel, "lyricsTimelineList")
        verify(surface && currentLine && source && timeline)
        compare(timeline.highlightMoveDuration, 320)
        verify(currentLine.font.pixelSize
               > findChild(panel, "previousLyricLine").font.pixelSize)
        try {
            SettingsController.themeMode = 0
            tryCompare(Theme, "isLight", false)
            compare(surface.color.toString(), Theme.glassSurface.toString())
            compare(surface.border.color.toString(), Theme.glassBorder.toString())
            compare(currentLine.color.toString(), Theme.accent.toString())
            compare(currentLine.font.weight, Font.Bold)
            compare(source.color.toString(), Theme.textSecondary.toString())

            SettingsController.themeMode = 1
            tryCompare(Theme, "isLight", true)
            compare(surface.color.toString(), Theme.glassSurface.toString())
            compare(surface.border.color.toString(), Theme.glassBorder.toString())
            compare(currentLine.color.toString(), Theme.accent.toString())
            compare(currentLine.font.weight, Font.Bold)
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

    function test_reopening_before_renderer_release_does_not_close_window_later() {
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
        tryCompare(coordinator, "attachedHostMode", PlayerExperienceController.Windowed)
        var window = coordinator.fullscreenWindow
        var terrain = coordinator.surface.terrainItem
        try {
            PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
            tryCompare(coordinator.surface, "terrainItem", null)
            // Resume near the real release deadline without a wall-clock race.
            coordinator.releasePolls = coordinator.releasePollLimit - 1
            PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
            tryCompare(window, "visible", true)
            wait(1000)
            compare(PlayerExperienceController.immersiveMode,
                    PlayerExperienceController.TerrainReactor)
            compare(window.visible, true)
            verify(coordinator.surface.terrainItem)
            verify(coordinator.surface.terrainItem !== terrain)
        } finally {
            PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
        }
    }

    function test_gpu_exit_releases_terrain_and_reopen_keeps_playback() {
        var savedRendering = mainWindow.immersiveRenderingEnabled
        var savedMode = PlaybackController.mode
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        try {
            // Keep end-of-track queue transitions outside this lifecycle test.
            var ids = transportTestSetup.prepareTransportQueue(30)
            compare(ids.length, 3)
            tryCompare(PlaybackController, "currentTrackId", ids[1])
            PlaybackController.setMode(PlaybackController.RepeatOne)
            tryCompare(PlaybackController, "mode", PlaybackController.RepeatOne)
            PlaybackController.play()
            tryCompare(PlaybackController, "state", PlaybackController.Playing)
            var trackId = PlaybackController.currentTrackId
            mainWindow.immersiveRenderingEnabled = true
            PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
            for (var cycle = 0; cycle < 3; ++cycle) {
                PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
                tryVerify(function() { return coordinator.surface
                        && coordinator.surface.terrainItem }, 3000)
                var surface = coordinator.surface
                var terrain = surface.terrainItem
                tryVerify(function() {
                    return terrain.renderStatus === TerrainReactorItem.Ready
                            || terrain.renderStatus === TerrainReactorItem.SoftwareBackend
                }, 10000)
                if (terrain.renderStatus === TerrainReactorItem.SoftwareBackend) {
                    skip("Requires a native GPU backend; ordinary window tests remain enabled")
                    return
                }
                tryCompare(terrain, "liveRendererCount", 1, 3000)
                var firstFrame = terrain.frameCount
                tryVerify(function() { return terrain.frameCount > firstFrame }, 3000)
                var window = coordinator.fullscreenWindow
                var invalidated = createTemporaryObject(immersiveShortcutSpyComponent,
                    testCase, { target: window, signalName: "sceneGraphInvalidated" })
                verify(invalidated && invalidated.valid)
                window.showMinimized()
                tryCompare(surface, "hostExposed", false, 1500)
                tryCompare(terrain, "renderingRequested", false, 1500)
                // Permit one already submitted frame to finish before checking quiescence.
                wait(100)
                var stoppedFrame = terrain.frameCount
                wait(200)
                compare(terrain.frameCount, stoppedFrame)
                compare(surface.terrainItem, terrain)
                window.showNormal()
                tryCompare(surface, "hostExposed", true, 1500)
                tryVerify(function() { return terrain.frameCount > stoppedFrame }, 3000)
                invalidated.clear()
                PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
                tryCompare(surface, "terrainItem", null, 3000)
                tryVerify(function() { return invalidated.count > 0 }, 3000,
                          "Exiting must invalidate the dedicated immersive scene graph")
                tryCompare(AudioVisualFeatureController, "active", false, 1500)
                tryCompare(coordinator, "handoffPhase", 0, 3000)
                tryCompare(PlaybackController, "state", PlaybackController.Playing)
                compare(PlaybackController.currentTrackId, trackId)
                var exitPosition = PlaybackController.positionMs
                tryVerify(function() {
                    return PlaybackController.positionMs !== exitPosition
                }, 1500, "Shared playback must keep advancing after immersive exit")
            }
        } finally {
            PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
            mainWindow.immersiveRenderingEnabled = savedRendering
            PlaybackController.stop()
            PlaybackController.setMode(savedMode)
        }
    }

    QtObject {
        id: presetCyclePlayback
        property string currentTrackId: "cycle-track-a"
        property int positionMs: 0
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
        var playerGeometry = Qt.rect(mainWindow.x, mainWindow.y,
                                     mainWindow.width, mainWindow.height)
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        tryCompare(findChild(mainWindow, "immersiveCoordinator"),
                   "attachedHostMode", PlayerExperienceController.Windowed, 2000)
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        var surface = coordinator ? coordinator.surface : null
        verify(surface)
        var controlPanel = findChild(surface, "immersiveControlPanelHost")
        var queueTrigger = findChild(surface, "queueTriggerZone")
        verify(controlPanel && queueTrigger)
        tryVerify(function() { return surface.width > 0
                && controlPanel.x < surface.width / 2 }, 1500)
        tryVerify(function() {
            return queueTrigger.x >= surface.width - queueTrigger.width
        }, 1500)
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
        surface.panelAutoHidden = true
        surface.notePointerActivity()
        compare(surface.panelAutoHidden, true)
        var revealZone = findChild(surface, "immersivePanelRevealZone")
        verify(revealZone)
        surface.revealPanelFromHotCorner()
        compare(surface.panelAutoHidden, false)
        verify(findChild(surface, "immersivePanelAutoHideTimer").running)
        compare(surface.panelIdle, false)
        surface.panelIdle = true
        tryCompare(controlPanel, "opacity", 1)
        surface.panelIdle = false
        surface.noteManualCameraActivity()
        compare(surface.manualCameraActive, true)

        var immersiveWindow = findChild(mainWindow, "immersiveVisualWindow")
        verify(immersiveWindow)
        compare(WindowController.mainVisible, false)
        compare(immersiveWindow.transientParent, null)
        var restoredSurface = coordinator.surface
        var restoredTerrain = surface.terrainItem
        var restoredGeometry = Qt.rect(immersiveWindow.x, immersiveWindow.y,
                                       immersiveWindow.width, immersiveWindow.height)
        minimizeButton.clicked()
        tryCompare(immersiveWindow, "visibility", Window.Minimized, 1500)
        tryCompare(AudioVisualFeatureController, "active", false, 1500)
        compare(WindowController.mainVisible, false)
        compare(PlayerExperienceController.immersiveMode,
                PlayerExperienceController.TerrainReactor)
        immersiveWindow.showNormal()
        immersiveWindow.requestActivate()
        tryCompare(immersiveWindow, "visibility", Window.Windowed, 1500)
        tryCompare(immersiveWindow, "active", true, 1500)
        compare(coordinator.surface, restoredSurface)
        compare(surface.terrainItem, restoredTerrain)
        compare(immersiveWindow.width, restoredGeometry.width)
        compare(immersiveWindow.height, restoredGeometry.height)
        compare(WindowController.mainVisible, false)
        tryCompare(AudioVisualFeatureController, "active",
                   surface.terrainItem.renderingRequested, 1500)

        var normalGeometry = Qt.rect(immersiveWindow.x, immersiveWindow.y,
                                     immersiveWindow.width, immersiveWindow.height)
        fullscreenButton.clicked()
        tryCompare(PlayerExperienceController, "hostMode",
                   PlayerExperienceController.Fullscreen, 1000)
        var escapeShortcut = findChild(mainWindow, "immersiveEscapeShortcut")
        verify(escapeShortcut)
        immersiveWindow.requestActivate()
        fullscreenButton.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        tryCompare(PlayerExperienceController, "hostMode",
                   PlayerExperienceController.Windowed)
        // A transient window owned by the hidden player has no independent
        // taskbar restore target on Windows.
        compare(immersiveWindow.transientParent, null)
        tryCompare(immersiveWindow, "visibility", Window.Windowed)
        compare(immersiveWindow.width, normalGeometry.width)
        compare(immersiveWindow.height, normalGeometry.height)
        compare(immersiveWindow.x, normalGeometry.x)
        compare(immersiveWindow.y, normalGeometry.y)
        surface.noteManualCameraActivity()
        verify(findChild(surface, "immersiveCameraResumeTimer").running)
        returnButton.clicked()
        tryCompare(WindowController, "mainVisible", true, 1500)
        tryCompare(AudioVisualFeatureController, "active", false, 1500)
        tryCompare(surface, "terrainItem", null, 1500)
        compare(findChild(surface, "immersiveOrbitArea").enabled, false)
        compare(findChild(surface, "immersivePanelIdleTimer").running, false)
        compare(findChild(surface, "immersivePanelAutoHideTimer").running, false)
        compare(findChild(surface, "immersiveCameraResumeTimer").running, false)
        compare(findChild(surface, "immersiveColumnGlowLoader").item, null)
        compare(mainWindow.width, playerGeometry.width)
        compare(mainWindow.height, playerGeometry.height)
        compare(mainWindow.x, playerGeometry.x)
        compare(mainWindow.y, playerGeometry.y)
    }

    function test_local_glow_lifecycle_stays_unloaded_on_software_backend() {
        mainWindow.immersiveRenderingEnabled = false
        PlayerExperienceController.glowIntensity = 38
        PlayerExperienceController.columnInnerLight = 100
        PlayerExperienceController.columnLightSpill = 60
        PlayerExperienceController.columnLightRadius = 100
        PlayerExperienceController.materialMode = 0
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed

        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        verify(coordinator)
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        var surface = coordinator.surface
        verify(surface && surface.terrainItem)
        compare(surface.terrainItem.renderStatus, TerrainReactorItem.Inactive)
        compare(surface.localGlowLifecycleEligible, false)
        compare(surface.localGlowRequested, false)
        tryCompare(findChild(surface, "immersiveColumnGlowLoader"),
                   "item", null, 1000)

        mainWindow.immersiveRenderingEnabled = true
        tryVerify(function() {
            return surface.terrainItem
                    && surface.terrainItem.renderStatus
                       === TerrainReactorItem.SoftwareBackend
        }, 6000)
        tryCompare(surface, "localGlowLifecycleEligible", true, 1000)
        compare(surface.localGlowRequested, false)
        tryCompare(findChild(surface, "immersiveColumnGlowLoader"),
                   "item", null, 1000)

        PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
        tryCompare(surface, "active", false, 1000)
        tryCompare(surface, "localGlowLifecycleEligible", false, 1000)
        compare(findChild(surface, "immersiveColumnGlowLoader").item, null)

        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        tryCompare(surface, "localGlowLifecycleEligible", true, 1000)

        var immersiveWindow = findChild(mainWindow, "immersiveVisualWindow")
        verify(immersiveWindow)
        immersiveWindow.showMinimized()
        tryCompare(surface, "hostExposed", false, 1500)
        tryCompare(surface, "localGlowLifecycleEligible", false, 1000)
        tryCompare(surface, "localGlowRequested", false, 1000)
        tryCompare(findChild(surface, "immersiveColumnGlowLoader"),
                   "item", null, 1000)

        immersiveWindow.showNormal()
        tryCompare(surface, "hostExposed", true, 1500)
        tryCompare(surface, "localGlowLifecycleEligible", true, 1000)
        compare(surface.localGlowRequested, false)
        compare(findChild(surface, "immersiveColumnGlowLoader").item, null)

        PlayerExperienceController.materialMode = 2
        tryCompare(surface, "localGlowLifecycleEligible", false, 1000)
        tryCompare(surface, "localGlowRequested", false, 1000)
        tryCompare(findChild(surface, "immersiveColumnGlowLoader"),
                   "item", null, 1000)

        PlayerExperienceController.materialMode = 0
        tryCompare(surface, "localGlowLifecycleEligible", true, 1000)
        PlayerExperienceController.columnLightSpill = 0
        tryCompare(surface, "localGlowLifecycleEligible", false, 1000)
        compare(surface.localGlowRequested, false)
        compare(findChild(surface, "immersiveColumnGlowLoader").item, null)

        PlayerExperienceController.columnLightSpill = 60
        tryCompare(surface, "localGlowLifecycleEligible", true, 1000)
        PlayerExperienceController.glowIntensity = 0
        tryCompare(surface, "localGlowLifecycleEligible", false, 1000)
        tryCompare(surface, "localGlowRequested", false, 1000)
        tryCompare(findChild(surface, "immersiveColumnGlowLoader"),
                   "item", null, 1000)
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

    function test_desktop_and_fullscreen_roundtrip_preserves_windowed_geometry() {
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        var immersiveWindow = coordinator.fullscreenWindow
        verify(immersiveWindow)

        immersiveWindow.x = 47
        immersiveWindow.y = 53
        immersiveWindow.width = 1120
        immersiveWindow.height = 720
        var windowed = Qt.rect(immersiveWindow.x, immersiveWindow.y,
                               immersiveWindow.width, immersiveWindow.height)

        PlayerExperienceController.hostMode = PlayerExperienceController.Desktop
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Desktop, 2500)
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2500)
        compare(immersiveWindow.x, windowed.x)
        compare(immersiveWindow.y, windowed.y)
        compare(immersiveWindow.width, windowed.width)
        compare(immersiveWindow.height, windowed.height)

        PlayerExperienceController.hostMode = PlayerExperienceController.Desktop
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Desktop, 2500)
        PlayerExperienceController.hostMode = PlayerExperienceController.Fullscreen
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Fullscreen, 2500)
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2500)
        compare(immersiveWindow.x, windowed.x)
        compare(immersiveWindow.y, windowed.y)
        compare(immersiveWindow.width, windowed.width)
        compare(immersiveWindow.height, windowed.height)
    }

    function test_queue_drawer_timers_scope_and_transform_only_magnification() {
        var drawer = queueDrawerComponent.createObject(mainWindow.contentItem)
        verify(drawer)
        compare(findChild(drawer, "queueOpenTimer").interval, 140)
        compare(findChild(drawer, "queueHideTimer").interval, 2000)
        compare(findChild(drawer, "queueTriggerZone").width, 20)
        drawer.requestOpen()
        // The configured delay is asserted above.  A full software-rendered
        // suite can defer an animation-timer delivery beyond one exact wait;
        // wait for the real signal instead of sampling it once at 170 ms.
        tryCompare(drawer, "opened", true, 1000)
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
        var coverImage = findChild(delegate, "queueCoverImage")
        verify(coverImage)
        compare(coverImage.asynchronous, true)
        verify(coverImage.sourceSize.width > 0 && coverImage.sourceSize.width <= 128)
        delegate.hoveredForQa = true
        tryCompare(delegate, "scale", 1.12)
        compare(delegate.height, delegateHeight)
        delegate.activateForQa()
        compare(queuePlayback.lastTrackId, "two")
        compare(queuePlayback.lastQueue.length, 3)
        compare(queuePlayback.lastQueue[0], "one")
        drawer.destroy()
    }

    function test_theme_cards_fit_readable_text_in_five_compact_rows() {
        var panel = windowedPresetPanel()
        var cards = []
        compare(PlayerExperienceController.builtInThemeChoices.length, 13)
        for (var preset = 0; preset < 13; ++preset) {
            var card = findChild(panel, "immersivePresetCard" + preset)
            verify(card)
            cards.push(card)
            var title = findChild(card, "immersivePresetTitle" + preset)
            verify(title)
            verify(card.height >= title.implicitHeight + 8
                   && card.height <= title.implicitHeight + 20,
                   "preset must fit one title with compact padding: " + card.height)
            compare(card.height, cards[0].height)
            verify(Math.abs(card.width - cards[0].width) <= 1)
            compare(title.lineCount, 1)
            verify(title.contentWidth <= title.width + 0.5)
            compare(card.ToolTip.text,
                    PlayerExperienceController.builtInThemeChoices[preset].title)
            var titleBounds = mappedBounds(title, card)
            verify(titleBounds.left >= -0.5
                   && titleBounds.right <= card.width + 0.5)
            verify(titleBounds.top >= 4
                   && titleBounds.bottom <= card.height - 4,
                   "preset " + preset + " text=" + titleBounds.top
                   + ".." + titleBounds.bottom
                   + " cardHeight=" + card.height)
            var textCenter = (titleBounds.top + titleBounds.bottom) / 2
            verify(Math.abs(textCenter - card.height / 2) <= 1.5,
                   "preset " + preset + " textCenter=" + textCenter
                   + " cardCenter=" + card.height / 2)
        }

        compare(cards[0].y, cards[1].y)
        compare(cards[1].y, cards[2].y)
        compare(cards[3].y, cards[4].y)
        compare(cards[4].y, cards[5].y)
        compare(cards[6].y, cards[7].y)
        compare(cards[7].y, cards[8].y)
        verify(cards[3].y > cards[0].y)
        verify(cards[6].y > cards[3].y)
        verify(cards[2].x > cards[1].x && cards[1].x > cards[0].x)
        verify(cards[5].x > cards[4].x && cards[4].x > cards[3].x)
        verify(cards[8].x > cards[7].x && cards[7].x > cards[6].x)
        compare(findChild(panel, "immersivePresetCard13"), null)
        compare(cards[9].y, cards[10].y)
        compare(cards[10].y, cards[11].y)
        verify(cards[9].y > cards[6].y && cards[12].y > cards[9].y)
        compare(cards[12].x, cards[0].x)
        verify(cards[12].y + cards[12].height - cards[0].y <= 250,
               "preset grid height="
               + (cards[12].y + cards[12].height - cards[0].y))
    }

    function test_readable_preset_page_keeps_quality_control_visible() {
        var panel = windowedPresetPanel()
        verify(panel.expandedHeight < 600,
               "preset page must fit its content: " + panel.expandedHeight)
        var panelScroll = findChild(panel, "immersivePanelScroll")
        var qualityCombo = findChild(panel, "immersiveQualityCombo")
        verify(panelScroll && qualityCombo)
        compare(panelScroll.contentItem.contentY, 0)
        var qualityBounds = mappedBounds(qualityCombo, panelScroll)
        verify(qualityBounds.top >= -0.5
               && qualityBounds.bottom <= panelScroll.height + 0.5,
               "quality bounds=" + qualityBounds.top + ".."
               + qualityBounds.bottom
               + " viewportHeight=" + panelScroll.height)
        verify(panelScroll.height - qualityBounds.bottom <= Theme.spacingLg,
               "preset page must not retain a large empty lower half")
    }

    function test_last_theme_card_applies_id_without_overwriting_dynamics() {
        var panel = windowedPresetPanel()
        var amberCinema = findChild(panel, "immersivePresetCard12")
        verify(amberCinema)
        PlayerExperienceController.terrainAmplitude = 37
        PlayerExperienceController.audioResponse = 123
        PlayerExperienceController.responseRange = 117
        mouseClick(amberCinema, amberCinema.width / 2,
                   amberCinema.height / 2, Qt.LeftButton)
        tryCompare(PlayerExperienceController, "themeId", PlayerExperienceController.builtInThemeChoices[12].id)
        compare(PlayerExperienceController.terrainAmplitude, 37)
        compare(PlayerExperienceController.audioResponse, 123)
        compare(PlayerExperienceController.responseRange, 117)
        compare(amberCinema.checkable, false)
        var host = amberCinema.Window.window
        verify(host)
        compare(host.objectName, "immersiveVisualWindow")
        host.requestActivate()
        tryCompare(host, "active", true)
        // The click above already focused the command with MouseFocusReason.
        // An actual focus transition is required to exercise keyboard styling.
        var precedingCard = findChild(panel, "immersivePresetCard11")
        verify(precedingCard)
        precedingCard.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(precedingCard, "activeFocus", true)
        tryCompare(amberCinema, "activeFocus", false)
        amberCinema.forceActiveFocus(Qt.TabFocusReason)
        function focusDiagnostic() {
            return "host.active=" + host.active
                    + " activeFocus=" + amberCinema.activeFocus
                    + " focusReason=" + amberCinema.focusReason
                    + " activeFocusItem="
                    + (host.activeFocusItem ? host.activeFocusItem.objectName : "null")
        }
        tryVerify(function() { return amberCinema.activeFocus }, 1000,
                  focusDiagnostic())
        compare(amberCinema.focusReason, Qt.TabFocusReason, focusDiagnostic())
        tryCompare(amberCinema, "visualFocus", true)
        compare(amberCinema.background.border.width, 2)
    }

    function test_000_first_immersive_open_accepts_shortcuts() {
        var savedLyrics = PlayerExperienceController.lyricsVisible
        try {
            PlayerExperienceController.lyricsVisible = false
            // Open through the real controller path. No click, requestActivate
            // or forceActiveFocus in this test may repair initial focus.
            PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
            var coordinator = findChild(mainWindow, "immersiveCoordinator")
            tryCompare(coordinator, "attachedHostMode", PlayerExperienceController.Windowed)
            var host = coordinator.surface.Window.window
            tryCompare(host, "visible", true)
            tryCompare(host, "activeFocusItem", coordinator.surface)
            tryCompare(host, "transportShortcutsEnabled", true)
            var shortcut = findChild(host, "immersiveLyricsShortcut")
            var spy = createTemporaryObject(immersiveShortcutSpyComponent,
                                            testCase, { target: shortcut })
            verify(spy && spy.valid)
            keyClick(Qt.Key_Up)
            compare(spy.count, 1)
            compare(PlayerExperienceController.lyricsVisible, true)
        } finally {
            PlayerExperienceController.lyricsVisible = savedLyrics
        }
    }

    function test_immersive_arrows_switch_real_queue_after_button_focus() {
        var savedMode = PlaybackController.mode
        var savedLyrics = PlayerExperienceController.lyricsVisible
        try {
            PlaybackController.setMode(PlaybackController.Sequential)
            tryCompare(PlaybackController, "mode", PlaybackController.Sequential)
            var ids = transportTestSetup.prepareTransportQueue()
            compare(ids.length, 3, "Real PCM queue must load successfully")
            tryCompare(PlaybackController, "currentTrackId", ids[1])
            var panel = windowedPresetPanel()
            var host = panel.Window.window
            host.requestActivate()
            tryCompare(host, "active", true)
            host.surfaceItem.forceActiveFocus(Qt.OtherFocusReason)
            keyClick(Qt.Key_Right)
            tryCompare(PlaybackController, "currentTrackId", ids[2])
            keyClick(Qt.Key_Left)
            tryCompare(PlaybackController, "currentTrackId", ids[1])

            var button = findChild(panel, "immersivePanelCollapseButton")
            verify(button)
            button.forceActiveFocus(Qt.TabFocusReason)
            tryCompare(button, "activeFocus", true)
            keyClick(Qt.Key_Left)
            tryCompare(PlaybackController, "currentTrackId", ids[0])
            keyClick(Qt.Key_Right)
            tryCompare(PlaybackController, "currentTrackId", ids[1])
            // Playback remains a window command even after a panel button
            // receives focus; otherwise the first click disables Space.
            var collapsed = panel.collapsed
            var playbackShortcut = findChild(host, "immersivePlaybackShortcut")
            var playbackSpy = createTemporaryObject(immersiveShortcutSpyComponent,
                                                     testCase,
                                                     { target: playbackShortcut })
            verify(playbackSpy && playbackSpy.valid)
            keyClick(Qt.Key_Space)
            compare(playbackSpy.count, 1)
            compare(panel.collapsed, collapsed)
            keyClick(Qt.Key_Right)
            tryCompare(PlaybackController, "currentTrackId", ids[2])

            PlayerExperienceController.lyricsVisible = false
            keyClick(Qt.Key_Up)
            compare(PlayerExperienceController.lyricsVisible, true)
            keyClick(Qt.Key_Up)
            compare(PlayerExperienceController.lyricsVisible, false)
            keyClick(Qt.Key_Down)
            tryCompare(PlaybackController, "mode", PlaybackController.Shuffle)
            keyClick(Qt.Key_Down)
            tryCompare(PlaybackController, "mode", PlaybackController.Shuffle)
            PlaybackController.setMode(PlaybackController.Sequential)
            tryCompare(PlaybackController, "mode", PlaybackController.Sequential)

            var editing = createTemporaryObject(immersiveShortcutEditingComponent,
                                                host.contentItem)
            verify(editing)
            var slider = findChild(editing, "shortcutEditingSlider")
            slider.forceActiveFocus(Qt.TabFocusReason)
            tryCompare(slider, "activeFocus", true)
            keyClick(Qt.Key_Left)
            compare(slider.value, 4)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Down)
            compare(PlayerExperienceController.lyricsVisible, false)
            compare(PlaybackController.mode, PlaybackController.Sequential)
            wait(150)
            compare(PlaybackController.currentTrackId, ids[2])
            var text = findChild(editing, "shortcutEditingText")
            text.forceActiveFocus(Qt.TabFocusReason)
            tryCompare(text, "activeFocus", true)
            text.cursorPosition = 2
            keyClick(Qt.Key_Left)
            compare(text.cursorPosition, 1)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Down)
            compare(PlayerExperienceController.lyricsVisible, false)
            compare(PlaybackController.mode, PlaybackController.Sequential)
            wait(150)
            compare(PlaybackController.currentTrackId, ids[2])

            // Real transport/settings must also remain unchanged when the
            // owner has focus, even if Qt reports its transient as active.
            mainWindow.visible = true
            mainWindow.requestActivate()
            tryCompare(host, "activeFocusItem", null)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Down)
            wait(150)
            compare(PlaybackController.currentTrackId, ids[2])
            compare(PlayerExperienceController.lyricsVisible, false)
            compare(PlaybackController.mode, PlaybackController.Sequential)

            PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
            tryCompare(WindowController, "immersivePresentationActive", false)
            mainWindow.requestActivate()
            tryCompare(mainWindow, "active", true)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Down)
            wait(150)
            compare(PlaybackController.currentTrackId, ids[2])
            compare(PlayerExperienceController.lyricsVisible, false)
            compare(PlaybackController.mode, PlaybackController.Sequential)
        } finally {
            PlaybackController.stop()
            PlaybackController.setMode(savedMode)
            tryCompare(PlaybackController, "mode", savedMode)
            PlayerExperienceController.lyricsVisible = savedLyrics
        }
    }

    function test_immersive_transport_shortcuts_respect_focus_and_editing() {
        var savedMode = PlaybackController.mode
        var savedLyrics = PlayerExperienceController.lyricsVisible
        try {
            PlaybackController.setMode(PlaybackController.Sequential)
            tryCompare(PlaybackController, "mode", PlaybackController.Sequential)
            PlayerExperienceController.lyricsVisible = false
            var panel = windowedPresetPanel()
            var host = panel.Window.window
            host.requestActivate()
            tryCompare(host, "active", true)
            host.surfaceItem.forceActiveFocus(Qt.OtherFocusReason)
            var names = ["immersivePreviousShortcut", "immersiveNextShortcut",
                         "immersivePlaybackShortcut", "immersiveShuffleShortcut",
                         "immersiveLyricsShortcut"]
            var keys = [Qt.Key_Left, Qt.Key_Right, Qt.Key_Space,
                        Qt.Key_Down, Qt.Key_Up]
            var shortcuts = []
            var spies = []
            for (var index = 0; index < names.length; ++index) {
                var shortcut = findChild(host, names[index])
                verify(shortcut, "Missing transport shortcut: " + names[index])
                compare(shortcut.context, Qt.WindowShortcut)
                compare(shortcut.autoRepeat, false)
                tryCompare(shortcut, "enabled", true)
                shortcuts.push(shortcut)
                var spy = createTemporaryObject(immersiveShortcutSpyComponent,
                                                testCase, { target: shortcut })
                verify(spy && spy.valid)
                spies.push(spy)
            }
            for (var key = 0; key < keys.length; ++key) {
                keyClick(keys[key])
                compare(spies[key].count, 1)
            }
            tryCompare(PlaybackController, "mode", PlaybackController.Shuffle)
            compare(PlayerExperienceController.lyricsVisible, true)
            keyClick(Qt.Key_Down)
            tryCompare(PlaybackController, "mode", PlaybackController.Shuffle)
            keyClick(Qt.Key_Up)
            compare(PlayerExperienceController.lyricsVisible, false)
            var baselineCounts = spies.map(function(spy) { return spy.count })

            var editing = createTemporaryObject(immersiveShortcutEditingComponent,
                                                host.contentItem)
            verify(editing)
            var text = findChild(editing, "shortcutEditingText")
            text.forceActiveFocus(Qt.TabFocusReason)
            tryCompare(text, "activeFocus", true)
            text.cursorPosition = 1
            for (var disabled = 0; disabled < shortcuts.length; ++disabled)
                tryCompare(shortcuts[disabled], "enabled", false)
            keyClick(Qt.Key_Space)
            compare(text.text, "a b")
            keyClick(Qt.Key_Left)
            compare(text.cursorPosition, 1)
            keyClick(Qt.Key_Right)
            compare(text.cursorPosition, 2)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Up)

            var slider = findChild(editing, "shortcutEditingSlider")
            slider.forceActiveFocus(Qt.TabFocusReason)
            tryCompare(slider, "activeFocus", true)
            tryCompare(shortcuts[2], "enabled", true)
            for (var blocked of [0, 1, 3, 4])
                tryCompare(shortcuts[blocked], "enabled", false)
            keyClick(Qt.Key_Right)
            compare(slider.value, 6)
            keyClick(Qt.Key_Left)
            compare(slider.value, 5)
            keyClick(Qt.Key_Space)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Up)
            compare(slider.value, 5)
            for (var unchanged = 0; unchanged < spies.length; ++unchanged)
                compare(spies[unchanged].count,
                        baselineCounts[unchanged] + (unchanged === 2 ? 1 : 0))

            host.surfaceItem.forceActiveFocus(Qt.OtherFocusReason)
            tryCompare(shortcuts[0], "enabled", true)
            // Immersive presentation hides Main; a hidden window cannot take
            // focus merely through requestActivate(). Expose it first.
            mainWindow.visible = true
            mainWindow.requestActivate()
            // Qt keeps transient siblings/owners "active" together. The
            // actual Quick focus item distinguishes which window has input.
            tryCompare(host, "activeFocusItem", null)
            for (var inactive = 0; inactive < shortcuts.length; ++inactive)
                tryCompare(shortcuts[inactive], "enabled", false)
            keyClick(Qt.Key_Right)
            compare(spies[1].count, 1)
            host.visible = false
            for (var hidden = 0; hidden < shortcuts.length; ++hidden)
                compare(shortcuts[hidden].enabled, false)
        } finally {
            PlaybackController.setMode(savedMode)
            tryCompare(PlaybackController, "mode", savedMode)
            PlayerExperienceController.lyricsVisible = savedLyrics
        }
    }

    function test_compact_panel_keeps_dynamic_eq_reachable_by_scrolling() {
        var panel = windowedPresetPanel()
        panel.currentTab = 2
        compare(panel.expandedHeight, 760)
        var scroll = findChild(panel, "immersivePanelScroll")
        var lastEq = findChild(panel, "visualEqSlider_7")
        verify(scroll && lastEq)
        tryVerify(function() { return scroll.contentHeight > scroll.height }, 1000)
        var targetY = lastEq.mapToItem(scroll.contentItem, 0, 0).y
        scroll.contentItem.contentY = Math.min(targetY,
                                              scroll.contentHeight - scroll.height)
        wait(0)
        var bounds = mappedBounds(lastEq, scroll)
        verify(bounds.top >= -0.5 && bounds.bottom <= scroll.height + 0.5,
               "last visual EQ band remains reachable in the dynamic tab")
    }

    function test_panel_exposes_thirteen_themes_lyrics_and_real_dynamics() {
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
        // The shared window intentionally preserves the user's last tab.
        // This test validates the preset page itself, so select it explicitly
        // instead of depending on the alphabetical order of earlier cases.
        panel.currentTab = 0
        compare(panel.currentTab, 0)
        tryVerify(function() {
            return panel.expandedHeight < 600
                    && Math.abs(panel.height - Math.min(panel.expandedHeight,
                                                       panel.parent.height - 108)) < 0.5
        }, 1000)
        verify(panel.height <= panel.parent.height - 108)
        var presetCards = []
        for (var preset = 0; preset < 13; ++preset) {
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
        verify(findChild(panel, "themeTimedCycleToggle"))
        verify(findChild(panel, "themeSongCycleToggle"))
        verify(findChild(panel, "themeCycleIntervalSlider"))
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
        // 音域回响 follows the approved preset capture: response range 1.29.
        compare(PlayerExperienceController.responseRange, 129)
        compare(PlayerExperienceController.rhythmStrength, 30)

        var fallbackMessage = findChild(coordinator.surface,
                                        "immersiveRenderFallbackMessage")
        verify(fallbackMessage)
        tryCompare(fallbackMessage, "visible", false, 1000)

        var colorField = findChild(panel, "immersiveColorField0")
        verify(colorField)
        compare(findChild(panel, "immersiveColorPicker"), null)
        var originalCoolColor = PlayerExperienceController.coolColor
        PlayerExperienceController.songAdaptiveColorEnabled = true
        colorField.colorEdited("#123456")
        compare(PlayerExperienceController.coolColor.toLowerCase(), "#123456")
        compare(PlayerExperienceController.songAdaptiveColorEnabled, false)
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

    function test_optional_preset_cycle_advances_once_per_distinct_immersive_track() {
        var priorPlayback = mainWindow.playback
        var priorTimedEnabled = PlayerExperienceController.themeCycleEnabled
        var priorSongEnabled = PlayerExperienceController.themeSongCycleEnabled
        var priorInterval = PlayerExperienceController.themeCycleIntervalSeconds
        var priorTheme = PlayerExperienceController.themeId
        try {
            mainWindow.playback = presetCyclePlayback
            presetCyclePlayback.currentTrackId = "cycle-track-a"
            PlayerExperienceController.applyTheme("ink-wash")
            PlayerExperienceController.themeCycleEnabled = false
            PlayerExperienceController.themeSongCycleEnabled = false
            PlayerExperienceController.immersiveMode =
                    PlayerExperienceController.TerrainReactor
            wait(0)

            presetCyclePlayback.currentTrackId = "cycle-track-b"
            wait(0)
            compare(PlayerExperienceController.themeId, "ink-wash")

            PlayerExperienceController.themeSongCycleEnabled = true
            presetCyclePlayback.currentTrackId = "cycle-track-c"
            tryCompare(PlayerExperienceController, "themeId", "nocturnal")

            // Property reassignment to the same identity is not a new song.
            presetCyclePlayback.currentTrackId = "cycle-track-c"
            wait(0)
            compare(PlayerExperienceController.themeId, "nocturnal")

            PlayerExperienceController.immersiveMode =
                    PlayerExperienceController.Off
            presetCyclePlayback.currentTrackId = "cycle-track-d"
            wait(0)
            compare(PlayerExperienceController.themeId, "nocturnal")

            PlayerExperienceController.themeSongCycleEnabled = false
            PlayerExperienceController.themeCycleIntervalSeconds = 3
            PlayerExperienceController.themeCycleEnabled = true
            PlayerExperienceController.applyTheme("ink-wash")
            PlayerExperienceController.immersiveMode =
                    PlayerExperienceController.TerrainReactor
            var timer = findChild(mainWindow, "themeRotationTimer")
            verify(timer)
            compare(timer.interval, 3000)
            tryCompare(PlayerExperienceController, "themeId", "nocturnal", 4000)
        } finally {
            mainWindow.playback = priorPlayback
            PlayerExperienceController.themeCycleEnabled = priorTimedEnabled
            PlayerExperienceController.themeSongCycleEnabled = priorSongEnabled
            PlayerExperienceController.themeCycleIntervalSeconds = priorInterval
            if (priorTheme.length > 0)
                PlayerExperienceController.applyTheme(priorTheme)
        }
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
        compare(String(nativeWaveform.lowColor), "#ff0000")
        compare(String(nativeWaveform.midColor), "#00ff00")
        compare(String(nativeWaveform.highColor), "#0000ff")
        var settings = SettingsController.frequencyColorWaveform
        var previousDimness = settings.unplayedDimness
        try {
            settings.unplayedDimness = 0.30
            compare(nativeWaveform.frequencyUnplayedOpacity, 0.70)
            settings.unplayedDimness = 0.72
            fuzzyCompare(nativeWaveform.frequencyUnplayedOpacity, 0.28, 0.000001)
        } finally {
            settings.unplayedDimness = previousDimness
        }
    }

    function test_reference_theme_density_and_light_background_follow_selection() {
        PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        verify(coordinator)
        tryCompare(coordinator, "attachedHostMode", PlayerExperienceController.Windowed, 2000)
        var surface = coordinator.surface
        var panel = windowedPresetPanel()
        verify(panel)
        try {
            verify(PlayerExperienceController.applyTheme("ink-wash"))
            compare(surface.inkMode, false)
            compare(surface.lightEnvironment, true)
            panel.currentTab = 2
            var density = findChild(panel, "dynamicSlider_topographyDensity")
            verify(density)
            compare(density.from, 0)
            compare(density.to, 100)
            density.value = 61
            density.moved()
            compare(PlayerExperienceController.topographyDensity, 61)
            verify(PlayerExperienceController.applyTheme("nocturnal"))
            compare(surface.lightEnvironment, false)
            compare(PlayerExperienceController.topographyDensity, 61)
        } finally {
            PlayerExperienceController.topographyDensity = 46
            PlayerExperienceController.applyTheme("ink-wash")
            panel.currentTab = 0
        }
    }

    function test_environment_palette_exposes_valid_live_color_channels() {
        PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        verify(coordinator)
        tryCompare(coordinator, "attachedHostMode", PlayerExperienceController.Windowed, 2000)
        var surface = coordinator.surface
        verify(surface)
        PlayerExperienceController.coolColor = "#204080"
        PlayerExperienceController.warmColor = "#804020"
        var colors = [surface.environmentCoolColor, surface.environmentWarmColor]
        var expected = [[32, 64, 128], [128, 64, 32]]
        var channels = ["r", "g", "b"]
        for (var i = 0; i < colors.length; ++i) {
            verify(colors[i] !== undefined, "Environment color must expose numeric channels")
            for (var j = 0; j < channels.length; ++j) {
                var channel = colors[i][channels[j]]
                verify(isFinite(channel))
                verify(Math.abs(channel - expected[i][j] / 255) < 0.001)
            }
        }
    }

    function test_theme_contrast_does_not_replace_frequency_waveform() {
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.TerrainReactor
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        PlayerExperienceController.lyricsVisible = true
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        verify(coordinator)
        tryCompare(coordinator, "attachedHostMode",
                   PlayerExperienceController.Windowed, 2000)
        var surface = coordinator.surface
        var waveform = findChild(surface, "immersiveWaveformHost")
        var lyrics = findChild(surface, "immersiveLyricsPanel")
        var session = findChild(mainWindow, "sharedWaveformSession")
        verify(surface && waveform && lyrics && session)
        var frequencyWaveform = findChild(waveform, "immersiveWaveform")
        verify(frequencyWaveform)
        verify(PlayerExperienceController.applyTheme("ink-wash"))
        compare(waveform.lightBackground, true)
        compare(lyrics.lightBackground, true)
        compare(frequencyWaveform.visualMode, 3)
        compare(waveform.waveformSession, session)
        verify(PlayerExperienceController.applyTheme("nocturnal"))
        compare(surface.inkMode, false)
        compare(waveform.lightBackground, false)
        compare(lyrics.lightBackground, false)
        compare(frequencyWaveform.visualMode, 3)
        compare(waveform.waveformSession, session)
        PlayerExperienceController.materialMode = 2
        compare(surface.inkMode, true)
        compare(waveform.lightBackground, true)
        compare(lyrics.lightBackground, true)
        var paper = surface.inkPaper
        var paperLuminance = paper.r * 0.2126 + paper.g * 0.7152 + paper.b * 0.0722
        verify(paperLuminance > 0.9,
               "Selecting ink material on a dark preset must produce light paper; got "
               + paperLuminance)
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
        var previousDepth = findChild(panel, "previousLyricDepthTransform")
        var nextDepth = findChild(panel, "nextLyricDepthTransform")
        verify(stage && perspective)
        verify(previousLine && currentLine && nextLine)
        verify(previousDepth && nextDepth)
        verify(currentLine.scale > previousLine.scale)
        verify(currentLine.scale > nextLine.scale)
        verify(currentLine.opacity > previousLine.opacity)
        verify(currentLine.opacity > nextLine.opacity)
        verify(previousDepth.y < 0)
        verify(nextDepth.y > 0)
        compare(currentLine.font.pixelSize, Theme.fontSizePageTitle)
        compare(previousLine.font.pixelSize, Theme.fontSizeBody)
        compare(nextLine.font.pixelSize, Theme.fontSizeBody)
        PlayerExperienceController.warmColor = "#6f3516"
        panel.placement = PlayerExperienceController.Left
        var previousEffectiveLuma = previousLine.opacity
                * (0.2126 * previousLine.color.r
                   + 0.7152 * previousLine.color.g
                   + 0.0722 * previousLine.color.b)
        var nextEffectiveLuma = nextLine.opacity
                * (0.2126 * nextLine.color.r
                   + 0.7152 * nextLine.color.g
                   + 0.0722 * nextLine.color.b)
        verify(currentLine.color.a > 0.99)
        verify(previousLine.color.a > 0.99)
        verify(nextLine.color.a > 0.99)
        compare(currentLine.color.toString(), Theme.accent.toString())
        compare(currentLine.font.weight, Font.Bold)
        verify(previousEffectiveLuma >= 0.25)
        verify(nextEffectiveLuma >= 0.22)
        verify(panel.implicitHeight >= stage.implicitHeight)
        tryVerify(function() {
            return previousLine.y + previousLine.height <= currentLine.y
                    && currentLine.y + currentLine.height <= nextLine.y
        }, 500)

        lyricsFake.previousLine = "这是一段用于验证上一行沉浸歌词在狭窄空间中保持两行以内的很长歌词文本"
        lyricsFake.currentLine = "这是一段用于验证沉浸歌词在狭窄空间中最多显示两行并在末尾省略的很长歌词文本"
        lyricsFake.nextLine = "这是一段用于验证下一行沉浸歌词在狭窄空间中保持两行以内的很长歌词文本"
        compare(currentLine.wrapMode, Text.Wrap)
        compare(currentLine.maximumLineCount, 2)
        compare(currentLine.elide, Text.ElideRight)
        tryVerify(function() {
            return previousLine.lineCount <= 2
                    && currentLine.lineCount <= 2
                    && nextLine.lineCount <= 2
        }, 500)
        tryVerify(function() {
            return previousLine.y + previousLine.height <= currentLine.y
                    && currentLine.y + currentLine.height <= nextLine.y
        }, 500)
        panel.destroy()
    }

    function test_spatial_lyric_placement_is_mirrored_and_non_spatial_stays_flat() {
        var panel = lyricsPanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        panel.spatialMode = true
        panel.depth = 100
        var previousLine = findChild(panel, "previousLyricLine")
        var currentLine = findChild(panel, "currentLyricLine")
        var nextLine = findChild(panel, "nextLyricLine")
        verify(previousLine && currentLine && nextLine)

        function edgeHeight(line, right) {
            var x = right ? line.width : 0
            var top = line.mapToItem(panel, x, 0)
            var bottom = line.mapToItem(panel, x, line.height)
            return Math.hypot(bottom.x - top.x, bottom.y - top.y)
        }

        panel.depth = 0
        panel.placement = PlayerExperienceController.Left
        wait(0)
        var previousFlatHeight = edgeHeight(previousLine, false)
        var nextFlatHeight = edgeHeight(nextLine, false)
        var currentFlatHeight = edgeHeight(currentLine, false)
        verify(Math.abs(edgeHeight(currentLine, true) - currentFlatHeight) < 0.01)
        compare(findChild(panel, "previousLyricDepthTransform").y, 0)
        compare(findChild(panel, "nextLyricDepthTransform").y, 0)
        panel.depth = 100

        panel.placement = PlayerExperienceController.Left
        compare(currentLine.horizontalAlignment, Text.AlignLeft)
        var leftNear = edgeHeight(currentLine, false)
        var leftFar = edgeHeight(currentLine, true)
        verify(leftNear / leftFar > 1.3,
               "The side lyric plane must visibly recede, not merely scale: "
               + leftNear / leftFar)
        verify(leftNear / leftFar < 2, "Keep the far end readable")
        verify(leftNear >= currentFlatHeight * 0.95,
               "The current line must remain clear at the reading edge")
        verify(edgeHeight(previousLine, false) < previousFlatHeight * 0.92,
               "The previous line must retreat behind the current plane")
        verify(edgeHeight(nextLine, false) < nextFlatHeight * 0.88,
               "The upcoming line must occupy the deeper plane")

        panel.placement = PlayerExperienceController.Right
        compare(currentLine.horizontalAlignment, Text.AlignRight)
        verify(Math.abs(edgeHeight(currentLine, true) - leftNear) < 0.01)
        verify(Math.abs(edgeHeight(currentLine, false) - leftFar) < 0.01)

        panel.placement = PlayerExperienceController.Center
        compare(currentLine.horizontalAlignment, Text.AlignHCenter)
        verify(Math.abs(edgeHeight(currentLine, true)
                        - edgeHeight(currentLine, false)) < 0.01)

        panel.spatialMode = false
        panel.placement = PlayerExperienceController.Left
        // Probe the plane with exact integer distances; do not compare mapped
        // text bounds with a fractional implicit text height (e.g. 22.4px).
        var normalOrigin = currentLine.mapToItem(panel, 0, 0)
        var normalVertical = currentLine.mapToItem(panel, 0, 16)
        var normalHorizontal = currentLine.mapToItem(panel, 16, 0)
        verify(Math.abs(normalVertical.y - normalOrigin.y - 16) < 0.01)
        verify(Math.abs(normalVertical.x - normalOrigin.x) < 0.01)
        verify(Math.abs(normalHorizontal.x - normalOrigin.x - 16) < 0.01)
        verify(Math.abs(normalHorizontal.y - normalOrigin.y) < 0.01)
        compare(currentLine.wrapMode, Text.Wrap)
        compare(currentLine.maximumLineCount, 3)
        compare(currentLine.scale, 1)
        compare(currentLine.font.pixelSize, Theme.fontSizePageTitle)
        compare(previousLine.font.pixelSize, Theme.fontSizeBody)
        compare(nextLine.font.pixelSize, Theme.fontSizeBody)
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
        var entrance = findChild(panel, "currentLyricEntranceTransform")
        verify(currentLine && previousLine && nextLine && settle)
        verify(entrance)
        var stableScale = currentLine.scale

        lyricsFake.currentLine = "进入的新歌词"
        compare(currentLine.text, "进入的新歌词")
        tryCompare(settle, "running", true, 50)
        compare(currentLine.opacity, 1)
        verify(Math.abs(currentLine.scale - stableScale) < 0.001)
        verify(entrance.y > 0)
        tryCompare(settle, "running", false, 400)
        verify(Math.abs(currentLine.opacity - 1) < 0.001)
        verify(Math.abs(currentLine.scale - stableScale) < 0.001)
        verify(Math.abs(entrance.y) < 0.001)

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
        compare(previousLine.visible, true)
        compare(nextLine.visible, true)

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
        tryCompare(settle, "running", true, 50)
        tryCompare(settle, "running", false, 400)
        compare(currentLine.opacity, 1)
        compare(currentLine.scale, 1)

        panel.spatialMode = true
        panel.enabled = false
        lyricsFake.currentLine = "禁用时歌词"
        compare(settle.running, false)
        verify(Math.abs(entrance.y) < 0.001)
        panel.enabled = true
        lyricsFake.enabled = false
        lyricsFake.currentLine = "隐藏时歌词"
        compare(settle.running, false)
        verify(Math.abs(entrance.y) < 0.001)
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
        compare(settle.running, false)
        compare(currentLine.opacity, 1)
        readyLyricsFake.currentLine = "B 最终歌词"
        compare(currentLine.text, "B 最终歌词")
        tryCompare(settle, "running", true, 50)
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

    function test_panel_tabs_keep_contrast_and_readable_presets_in_both_themes() {
        var previousTheme = SettingsController.themeMode
        var panel = createTemporaryObject(immersiveControlPanelComponent,
                                         mainWindow.contentItem,
                                         { currentTab: 0 })
        verify(panel)
        try {
            for (var mode = 0; mode <= 1; ++mode) {
                SettingsController.themeMode = mode
                wait(0)
                var tab = findChild(panel, "immersivePresetTab")
                verify(tab && tab.checked)
                compare(tab.contentItem.color.toString(), Theme.textPrimary.toString())
                compare(tab.font.family, Theme.fontPrimary)
                var title = findChild(panel, "immersivePresetTitle0")
                compare(title.font.family, Theme.fontPrimary)
                verify(title.font.pixelSize >= Theme.fontSizeCaption)
                var card = findChild(panel, "immersivePresetCard8")
                verify(card && card.visible)
            }
        } finally {
            SettingsController.themeMode = previousTheme
        }
    }

    function test_lyric_position_labels_remain_readable_when_unselected() {
        var savedTheme = SettingsController.themeMode
        var savedPosition = PlayerExperienceController.lyricPosition
        // Use the exposed host: a temporary z=0 panel under Main's shell is
        // sufficient for property checks, but can be covered during input.
        var panel = windowedPresetPanel()
        panel.currentTab = 1
        wait(220)
        verify(panel)
        function textItem(item, text) {
            if (item.text === text && item.color !== undefined)
                return item
            for (var child = 0; child < item.children.length; ++child) {
                var found = textItem(item.children[child], text)
                if (found)
                    return found
            }
            return null
        }
        function luminance(color) {
            function linear(value) {
                return value <= 0.04045 ? value / 12.92
                                       : Math.pow((value + 0.055) / 1.055, 2.4)
            }
            return 0.2126 * linear(color.r) + 0.7152 * linear(color.g)
                    + 0.0722 * linear(color.b)
        }
        try {
            for (var mode = 0; mode <= 1; ++mode) {
                SettingsController.themeMode = mode
                for (var selected = 0; selected < 3; ++selected) {
                    PlayerExperienceController.lyricPosition = selected
                    wait(0)
                    for (var index = 0; index < 3; ++index) {
                        var button = findChild(panel, "lyricPositionButton" + index)
                        verify(button && button.visible && button.enabled)
                        compare(button.checked, selected === index)
                        var label = textItem(button.contentItem, button.text)
                        verify(label && label.visible && label.opacity > 0)
                        verify(label.width > 0 && label.height > 0)
                        var background = button.background.color
                        var composed = Qt.rgba(
                                background.r * background.a + panel.color.r * (1 - background.a),
                                background.g * background.a + panel.color.g * (1 - background.a),
                                background.b * background.a + panel.color.b * (1 - background.a), 1)
                        var foregroundLuma = luminance(label.color)
                        var backgroundLuma = luminance(composed)
                        var contrast = (Math.max(foregroundLuma, backgroundLuma) + 0.05)
                                / (Math.min(foregroundLuma, backgroundLuma) + 0.05)
                        verify(contrast >= 4.5, "theme=" + mode + " position=" + index
                               + " selected=" + selected + " contrast=" + contrast)
                    }
                }
            }
            for (var target = 0; target < 3; ++target) {
                var targetButton = findChild(panel, "lyricPositionButton" + target)
                var viewport = findChild(panel, "immersivePanelScroll")
                var bounds = mappedBounds(targetButton, viewport)
                verify(bounds.left >= 0 && bounds.right <= viewport.width
                       && bounds.top >= 0 && bounds.bottom <= viewport.height,
                       "Position button must be inside the exposed scroll viewport")
                mouseClick(targetButton, targetButton.width / 2,
                           targetButton.height / 2, Qt.LeftButton)
                compare(PlayerExperienceController.lyricPosition, target)
                compare(targetButton.checked, true)
                targetButton.Window.window.requestActivate()
                targetButton.forceActiveFocus(Qt.TabFocusReason)
                verify(targetButton.activeFocus)
                compare(targetButton.background.border.color.toString(), Theme.focus.toString())
                keyClick(Qt.Key_Space)
                compare(PlayerExperienceController.lyricPosition, target)
                compare(targetButton.checked, true)
            }
        } finally {
            SettingsController.themeMode = savedTheme
            PlayerExperienceController.lyricPosition = savedPosition
        }
    }

    function test_reference_theme_exposes_only_live_dynamics_and_drop_entry() {
        verify(PlayerExperienceController.applyTheme("nocturnal"))
        var panel = windowedPresetPanel()
        panel.currentTab = 2
        wait(0)
        for (var key of ["terrainAmplitude", "motionResponse", "glowIntensity",
                         "reactorBrightness", "rhythmSensitivity", "topographyDensity"])
            verify(findChild(panel, "dynamicSlider_" + key), key)
        for (var deadKey of ["columnInnerLight", "columnLightSpill", "columnLightRadius",
                             "centerHighlight", "depthOfField", "inputCompression", "rhythmStrength"])
            compare(findChild(panel, "dynamicSlider_" + deadKey), null, deadKey)
        compare(findChild(panel, "effectToggle_burstEnabled"), null)
        var host = findChild(mainWindow, "immersiveCoordinator").fullscreenWindow
        var drop = findChild(host, "immersiveFileDropArea")
        verify(drop && drop.enabled)
        verify(typeof drop.urlsSubmitter === "function")
        compare(drop.submitUrls([]), false)
        PlayerExperienceController.colorMode = PlayerExperienceController.Custom
    }

    function test_effect_sliders_require_their_effect_and_have_unique_keys() {
        verify(PlayerExperienceController.applyTheme("nocturnal"))
        var panel = windowedPresetPanel()
        panel.currentTab = 2
        var seen = {}
        for (var group of panel.dynamicsGroups) {
            for (var slider of group.sliders) {
                verify(!seen[slider.key], "duplicate setting: " + slider.key)
                seen[slider.key] = true
            }
        }
        for (var control of [
                 { toggle: "ripplesEnabled", value: false, key: "rippleWidth", restore: true },
                 { toggle: "floatingCubesEnabled", value: false, key: "floatingBlockSpeed", restore: true },
                 { toggle: "autoRotate", value: 0, key: "autoRotateSpeed", restore: 54 }]) {
            PlayerExperienceController[control.toggle] = control.value
            var item = findChild(panel, "dynamicSlider_" + control.key)
            verify(item)
            tryCompare(item, "enabled", false)
            PlayerExperienceController[control.toggle] = control.restore
            tryCompare(item, "enabled", true)
        }
    }

    function test_visual_eq_sliders_change_only_the_selected_frequency_band() {
        var saved = PlayerExperienceController.visualEqGains.slice()
        var panel = createTemporaryObject(immersiveControlPanelComponent,
                                          mainWindow.contentItem, { currentTab: 2 })
        verify(panel)
        try {
            for (var band = 0; band < 8; ++band) {
                var gains = [40, 41, 42, 43, 44, 45, 46, 47]
                PlayerExperienceController.visualEqGains = gains
                var slider = findChild(panel, "visualEqSlider_" + band)
                verify(slider, "Missing visual EQ control for band " + band)
                compare(slider.from, 0)
                compare(slider.to, 100)
                compare(slider.value, gains[band])
                slider.value = 73
                slider.moved()
                for (var i = 0; i < 8; ++i)
                    compare(PlayerExperienceController.visualEqGains[i],
                            i === band ? 73 : gains[i])
                compare(findChild(panel, "visualEqValue_" + band).text, "73%")
            }
        } finally {
            PlayerExperienceController.visualEqGains = saved
        }
    }

    function test_column_density_buttons_change_and_bound_quantity() {
        var saved = PlayerExperienceController.columnDensity
        var panel = windowedPresetPanel()
        panel.currentTab = 2
        wait(220)
        try {
            var slider = findChild(panel, "dynamicSlider_columnDensity")
            verify(slider)
            var row = slider.parent
            var decrease = findChild(row, "densityDecrease")
            var increase = findChild(row, "densityIncrease")
            verify(decrease && increase)
            PlayerExperienceController.columnDensity = 125
            mouseClick(increase)
            compare(PlayerExperienceController.columnDensity, 130)
            mouseClick(decrease)
            compare(PlayerExperienceController.columnDensity, 125)
            compare(findChild(row, "dynamicValue_columnDensity").text, "125%")
            PlayerExperienceController.columnDensity = 50
            compare(decrease.enabled, false)
            PlayerExperienceController.columnDensity = 200
            compare(increase.enabled, false)
            slider.value = 100
            slider.moved()
            compare(PlayerExperienceController.columnDensity, 100)
        } finally {
            PlayerExperienceController.columnDensity = saved
        }
    }

    function test_fixed_column_geometry_is_not_exposed_as_a_dynamic_control() {
        var panel = createTemporaryObject(immersiveControlPanelComponent,
                                          mainWindow.contentItem, { currentTab: 2 })
        verify(panel)
        var keys = ["terrainAmplitude", "subjectClarity", "reactorBrightness",
                    "rhythmStrength"]
        for (var i = 0; i < keys.length; ++i) {
            var slider = findChild(panel, "dynamicSlider_" + keys[i])
            verify(slider, keys[i])
            var others = {}
            for (var j = 0; j < keys.length; ++j)
                others[keys[j]] = PlayerExperienceController[keys[j]]
            PlayerExperienceController[keys[i]] = 66
            compare(slider.value, 66)
            slider.value = 74
            slider.moved()
            compare(PlayerExperienceController[keys[i]], 74)
            for (var k = 0; k < keys.length; ++k)
                if (k !== i)
                    compare(PlayerExperienceController[keys[k]], others[keys[k]])
        }
        compare(findChild(panel, "dynamicValue_reactorBrightness").text, "0.74")
        compare(findChild(panel, "dynamicSlider_columnSize"), null)
        compare(findChild(panel, "dynamicSlider_columnOpacity"), null)
    }

    function test_material_selection_is_not_exposed_in_dynamic_controls() {
        var panel = createTemporaryObject(immersiveControlPanelComponent,
                                          mainWindow.contentItem, { currentTab: 2 })
        verify(panel)
        var material = findChild(panel, "dynamicsMaterialGroup")
        var ripple = findChild(panel, "dynamicsRippleGroup")
        verify(!material)
        verify(!findChild(panel, "immersiveMaterialCombo"))
        verify(ripple)
    }

    function test_dynamics_controls_are_grouped_by_meaning_and_remain_wired() {
        var panel = windowedPresetPanel()
        verify(panel)
        compare(panel.width, 320)
        tryVerify(function() { return panel.expandedHeight < 600 }, 1000)
        var terrainGroup = findChild(panel, "dynamicsTerrainGroup")
        var lightGroup = findChild(panel, "dynamicsLightGroup")
        var motionGroup = findChild(panel, "dynamicsMotionGroup")
        var impactGroup = findChild(panel, "dynamicsImpactGroup")
        verify(terrainGroup && lightGroup && motionGroup && impactGroup)

        panel.currentTab = 0
        wait(0)
        var presetScroll = findChild(panel, "immersivePanelScroll")
        var lastPreset = findChild(panel, "immersivePresetCard12")
        verify(presetScroll && lastPreset)
        var presetRight = lastPreset.mapToItem(presetScroll, lastPreset.width, 0).x
        verify(presetRight <= presetScroll.width + 0.5,
               "preset and color controls must stay inside the narrow panel")
        for (var cardIndex = 0; cardIndex < 13; ++cardIndex) {
            var card = findChild(panel, "immersivePresetCard" + cardIndex)
            var title = findChild(card, "immersivePresetTitle" + cardIndex)
            verify(title, "each preset must expose a visible name")
            compare(title.text, panel.presetCards[cardIndex].title)
            verify(title.width >= title.implicitWidth,
                   "preset names must fit without truncation: " + title.text
                   + " width=" + title.width + " implicit=" + title.implicitWidth
                   + " card=" + card.width)
            var titleOrigin = title.mapToItem(card, 0, 0)
            verify(titleOrigin.y >= 0 && titleOrigin.y + title.height <= card.height)
            verify(titleOrigin.x >= 0 && titleOrigin.x + title.width <= card.width)
        }
        panel.currentTab = 2
        wait(0)

        var terrainKeys = ["terrainAmplitude", "inputCompression", "audioResponse",
                           "responseRange", "subjectClarity"]
        var lightKeys = ["columnInnerLight", "columnLightSpill",
                         "columnLightRadius", "centerHighlight",
                         "depthOfField"]
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
        compare(findChild(lightGroup,
                          "effectToggle_songAdaptiveColorEnabled"), null)
        verify(findChild(panel, "songColorToggle"))
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

        var liveControlCases = [
            { key: "columnInnerLight", value: 77 },
            { key: "columnLightSpill", value: 88 },
            { key: "columnLightRadius", value: 99 },
            { key: "centerHighlight", value: 52 },
            { key: "depthOfField", value: 61 },
            { key: "rhythmSensitivity", value: 72 }
        ]
        for (var liveIndex = 0; liveIndex < liveControlCases.length;
             ++liveIndex) {
            var liveCase = liveControlCases[liveIndex]
            var liveSlider = findChild(panel,
                    "dynamicSlider_" + liveCase.key)
            verify(liveSlider, "missing interactive control " + liveCase.key)
            liveSlider.value = liveCase.value
            liveSlider.moved()
            compare(Number(PlayerExperienceController[liveCase.key]),
                    liveCase.value)
        }

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

        var restoreDefaults = findChild(panel,
                                        "restoreImmersiveDynamicsDefaults")
        verify(restoreDefaults)
        verify(restoreDefaults.text.length > 0)
        restoreDefaults.clicked()
        compare(PlayerExperienceController.inputCompression, 99)
        compare(PlayerExperienceController.rippleStrength, 100)
        compare(PlayerExperienceController.rippleWidth, 100)
        compare(PlayerExperienceController.reactorBrightness, 100)
        compare(PlayerExperienceController.terrainAmplitude, 50)
        compare(PlayerExperienceController.centerHighlight, 40)
        compare(PlayerExperienceController.autoRotateSpeed, 15)
        compare(PlayerExperienceController.rhythmStrength, 30)
        compare(PlayerExperienceController.streamHighlightEnabled, true)
        compare(PlayerExperienceController.idleBreathingEnabled, true)
        compare(PlayerExperienceController.burstEnabled, true)
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
