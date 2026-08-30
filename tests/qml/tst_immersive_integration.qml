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
        property int offsetMs: 0
        property int retryCalls: 0
        property int pauseCalls: 0
        function retry() { retryCalls += 1 }
        function pauseFollow(milliseconds) { pauseCalls += 1 }
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
        var miniActions = findChild(miniWindow, "miniExperienceActions")
        verify(mainActions && miniActions)
        compare(findChild(mainActions, "themeActionButton"), null)
        compare(findChild(miniActions, "themeActionButton"), null)
        compare(findChild(miniActions, "immersiveActionButton").visible, false)

        PlayerExperienceController.lyricsVisible = false
        findChild(mainActions, "lyricsActionButton").clicked()
        compare(PlayerExperienceController.lyricsVisible, true)
        compare(findChild(miniActions, "lyricsActionButton").checked, true)

        PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
        findChild(mainActions, "immersiveActionButton").clicked()
        compare(PlayerExperienceController.immersiveMode,
                PlayerExperienceController.TerrainReactor)
        compare(findChild(mainActions, "immersiveActionButton").checked, true)
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
        colorPicker.applied("#123456")
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
