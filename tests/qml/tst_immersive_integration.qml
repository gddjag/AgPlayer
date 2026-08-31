import QtQuick
import QtQuick.Controls
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
        PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        wait(50)
    }

    function cleanup() {
        SettingsController.playerShellMode = originalShellMode
        PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        PlayerExperienceController.lyricsVisible = false
        PlayerExperienceController.panelVisible = true
        queuePlayback.lastQueue = []
        queuePlayback.lastTrackId = ""
        var coordinator = findChild(mainWindow, "immersiveCoordinator")
        if (coordinator) {
            tryCompare(coordinator, "handoffPhase", 0, 6000)
            if (coordinator.surface && coordinator.surface.terrainItem)
                tryCompare(coordinator.surface.terrainItem,
                           "renderingRequested", false, 6000)
        }
        wait(50)
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
        compare(findChild(mainActions, "lyricsActionButton").icon.width, 16)
        compare(findChild(mainActions, "lyricsActionButton").icon.height, 16)
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
        verify(fullscreenButton)
        compare(fullscreenButton.display, AbstractButton.IconOnly)
        tryCompare(AudioVisualFeatureController, "active",
                   surface.terrainItem.renderingRequested, 1000)
        compare(findChild(surface, "immersivePanelIdleTimer").interval, 3000)
        compare(findChild(surface, "immersivePanelAutoHideTimer").interval, 5000)
        compare(findChild(surface, "immersiveCameraResumeTimer").interval, 4000)
        surface.notePointerActivity()
        compare(surface.panelIdle, false)
        surface.noteManualCameraActivity()
        compare(surface.manualCameraActive, true)

        fullscreenButton.clicked()
        tryCompare(PlayerExperienceController, "hostMode",
                   PlayerExperienceController.Fullscreen, 1000)
        var escapeShortcut = findChild(mainWindow, "immersiveEscapeShortcut")
        verify(escapeShortcut)
        escapeShortcut.activated()
        compare(PlayerExperienceController.hostMode,
                PlayerExperienceController.Windowed)
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

    function test_v46_panel_exposes_six_presets_lyrics_and_real_dynamics() {
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
        var presetCards = []
        for (var preset = 0; preset < 6; ++preset) {
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
        compare(findChild(panel, "dynamicSlider_terrainAmplitude"), null)
        compare(PlayerExperienceController.responseRange, 100)
        compare(PlayerExperienceController.rhythmStrength, 30)

        var fallbackMessage = findChild(coordinator.surface,
                                        "immersiveRenderFallbackMessage")
        verify(fallbackMessage)
        compare(fallbackMessage.visible, false)

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
        verify(session && miniControls && immersiveWaveform)
        compare(miniWindow.waveformSession, session)
        compare(miniControls.waveformSession, session)
        compare(immersiveWaveform.waveformSession, session)
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

    function test_lyrics_switch_drives_service_and_normal_window_overlay() {
        PlayerExperienceController.immersiveMode =
                PlayerExperienceController.Off
        PlayerExperienceController.lyricsVisible = false
        var normalPanel = findChild(mainWindow, "normalLyricsPanel")
        verify(normalPanel)
        compare(normalPanel.visible, false)
        compare(LyricsService.enabled, false)

        PlayerExperienceController.lyricsVisible = true
        tryCompare(LyricsService, "enabled", true, 500)
        tryCompare(normalPanel, "visible", true, 500)
    }
}
