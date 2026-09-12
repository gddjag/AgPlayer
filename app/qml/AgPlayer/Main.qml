import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

ApplicationWindow {
    id: mainWindow
    objectName: "mainWindow"
    font.family: Theme.fontPrimary
    font.pixelSize: Theme.fontSizeBody
    visible: true
    width: 863
    height: 266
    minimumWidth: WindowController.mainWindowShellMode === 1
                  || WindowController.mainWindowShellMode === 2 ? 1180 : 612
    minimumHeight: WindowController.mainWindowShellMode === 1 ? 720
                   : WindowController.mainWindowShellMode === 2 ? 720 : 232
    onClosing: function(close) {
        close.accepted = false
        WindowController.requestClose()
    }
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    background: null
    title: "AgPlayer"
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.surfaceElevated
    palette.alternateBase: Theme.surface
    palette.text: Theme.primaryText
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightText
    palette.mid: Theme.opaqueBorder

    // Shared-state surface so the main window and the mini player can bind to
    // the same playback source. Defaults to the production singleton; tests
    // override this with a fake QtObject to verify shared state without audio.
    property var playback: PlaybackController
    property var videoPlayback: VideoPlaybackController
    property int positionMs: playback ? playback.positionMs : 0
    property bool playFirstDroppedTrack: false
    property string tagSearchText: ""
    property int integratedSidePanelPage: 0
    property bool integratedSidePanelExpanded: true
    property int rollingSidePanelPage: 0
    property bool rollingSidePanelExpanded: true
    property bool immersiveRenderingEnabled: true
    property string themeRotationTrackId: ""
    property int previousShellMode: SettingsController.playerShellMode
    property int videoVisibilityBeforeFullscreen: Window.Windowed
    property alias waveformSession: sharedWaveformSession
    readonly property bool integratedShell:
        SettingsController.playerShellMode === 1
    readonly property bool rollingShell:
        SettingsController.playerShellMode === 2
    readonly property bool immersiveActuallyRendering:
        immersiveRenderingEnabled && immersiveCoordinator.surface !== null
        && immersiveCoordinator.surface.terrainItem !== null
        && immersiveCoordinator.surface.terrainItem.renderingRequested
    onImmersiveActuallyRenderingChanged: synchronizeAudioVisualConsumer()
    onPlaybackChanged: resetThemeRotationTrackBaseline()
    readonly property bool integratedLyricsRequested:
        integratedShell && integratedSidePanelExpanded
        && integratedSidePanelPage === 1
    readonly property bool rollingLyricsRequested:
        rollingShell && rollingSidePanelExpanded
        && rollingSidePanelPage === 1
    readonly property bool qaImmersive:
        Qt.application.arguments.indexOf("--qa-immersive") >= 0
    readonly property bool qaImmersiveSynthetic:
        Qt.application.arguments.indexOf("--qa-immersive-synthetic") >= 0
    readonly property bool qaImmersiveFullscreen:
        Qt.application.arguments.indexOf("--qa-immersive-fullscreen") >= 0
    readonly property bool videoFullscreen:
        mainWindow.visibility === Window.FullScreen
    readonly property int qaImmersiveWidth: qaArgumentNumber("--qa-width", 0)
    readonly property int qaImmersiveHeight: qaArgumentNumber("--qa-height", 0)

    function qaArgumentNumber(name, fallback) {
        var index = Qt.application.arguments.indexOf(name)
        if (index < 0 || index + 1 >= Qt.application.arguments.length)
            return fallback
        var value = Number(Qt.application.arguments[index + 1])
        return isFinite(value) && value > 0 ? Math.round(value) : fallback
    }

    function enterVideoFullscreen() {
        if (videoFullscreen)
            return
        videoVisibilityBeforeFullscreen = visibility
        showFullScreen()
    }

    function exitVideoFullscreen() {
        if (!videoFullscreen)
            return
        if (videoVisibilityBeforeFullscreen === Window.Maximized)
            showMaximized()
        else
            showNormal()
    }

    function leaveVideoPlayback() {
        if (videoFullscreen)
            exitVideoFullscreen()
        if (playback && playback.stop !== undefined)
            playback.stop()
        if (videoPlayback && videoPlayback.dismiss !== undefined)
            videoPlayback.dismiss()
    }

    DockedWindowFrame {
        anchors.fill: parent
        dockEdge: WindowController.listWindowVisible
                  && !WindowController.listWindowDetached
                  ? WindowController.listDockEdge : "none"
        windowRole: "main"
        maximized: mainWindow.visibility === Window.Maximized
        showBorders: false
        z: -10
    }

    Connections {
        target: ImportController
        function onFinished() {
            if (!mainWindow.playFirstDroppedTrack)
                return
            mainWindow.playFirstDroppedTrack = false
            if (ImportController.importedTrackIds.length < 1)
                return
            var row = LibraryModel.indexForTrackId(
                        ImportController.importedTrackIds[0])
            if (row >= 0)
                LibraryModel.playRow(row)
        }
    }

    // The filter model is owned by the main window but consumed by the
    // separate ListWindow so filtering state stays in sync.
    LibraryFilterModel {
        id: sharedFilterModel
        objectName: "filterModel"
        sourceModel: LibraryModel
        playlistModel: PlaylistModel
    }

    WaveformSession {
        id: sharedWaveformSession
    }

    // `active` is intentionally invokable-only. The independent immersive
    // surface preserves the same rolling demand during host teardown.
    function synchronizeAudioVisualConsumer() {
        AudioVisualFeatureController.setActive(
                    rollingShell || immersiveActuallyRendering)
    }

    function currentPlaybackTrackId() {
        return playback && playback.currentTrackId !== undefined
                ? String(playback.currentTrackId || "") : ""
    }

    function resetThemeRotationTrackBaseline() {
        themeRotationTrackId = currentPlaybackTrackId()
    }

    function advanceImmersivePreset() {
        var themes = PlayerExperienceController.builtInThemeChoices
        if (!themes || themes.length < 2)
            return false
        var currentId = String(PlayerExperienceController.themeId || "")
        var currentIndex = -1
        for (var index = 0; index < themes.length; ++index) {
            if (String(themes[index].id) === currentId) {
                currentIndex = index
                break
            }
        }
        var nextIndex = currentIndex >= 0
                ? (currentIndex + 1) % themes.length : 0
        return PlayerExperienceController.applyTheme(
                    String(themes[nextIndex].id))
    }

    function handleThemeRotationTrackChanged() {
        var currentId = currentPlaybackTrackId()
        if (currentId.length === 0)
            return
        var previousId = themeRotationTrackId
        themeRotationTrackId = currentId
        if (previousId.length === 0 || previousId === currentId)
            return
        if (PlayerExperienceController.immersiveMode
                !== PlayerExperienceController.Off
                && PlayerExperienceController.themeSongCycleEnabled)
            advanceImmersivePreset()
    }

    function showRollingLyricsPanel() {
        rollingSidePanelPage = 1
        rollingSidePanelExpanded = true
        if (rollingShell && shellLoader.item) {
            shellLoader.item.sidePanelPage = 1
            shellLoader.item.sidePanelExpanded = true
        }
    }

    function showIntegratedLyricsPanel() {
        integratedSidePanelPage = 1
        integratedSidePanelExpanded = true
        if (integratedShell && shellLoader.item) {
            shellLoader.item.sidePanelPage = 1
            shellLoader.item.sidePanelExpanded = true
        }
    }

    Binding {
        target: LyricsService
        property: "enabled"
        value: PlayerExperienceController.lyricsVisible
               || mainWindow.integratedLyricsRequested
               || mainWindow.rollingLyricsRequested
    }

    Connections {
        target: SettingsController
        function onPlayerShellModeChanged() {
            var nextMode = SettingsController.playerShellMode
            if (mainWindow.previousShellMode === 2 && nextMode !== 2
                    && mainWindow.playback) {
                if (mainWindow.playback.cancelScratch !== undefined)
                    mainWindow.playback.cancelScratch()
                if (mainWindow.playback.resetTempo !== undefined)
                    mainWindow.playback.resetTempo()
            }
            mainWindow.previousShellMode = nextMode
            mainWindow.synchronizeAudioVisualConsumer()
        }
    }

    Connections {
        target: PlayerExperienceController
        function onPanelVisibleChanged() {
            mainWindow.synchronizeAudioVisualConsumer()
        }
        function onLyricsVisibleChanged() {
            if (!PlayerExperienceController.lyricsVisible)
                return
            if (PlayerExperienceController.immersiveMode
                    !== PlayerExperienceController.Off)
                return
            if (mainWindow.integratedShell) {
                mainWindow.showIntegratedLyricsPanel()
            } else if (mainWindow.rollingShell) {
                mainWindow.showRollingLyricsPanel()
            } else if (!mainWindow.rollingShell) {
                WindowController.showListWindow()
            }
        }
        function onImmersiveModeChanged() {
            mainWindow.resetThemeRotationTrackBaseline()
        }
        function onThemeChanged() {
            if (themeRotationTimer.running)
                themeRotationTimer.restart()
        }
    }

    Connections {
        target: mainWindow.playback
        ignoreUnknownSignals: true
        function onCurrentTrackIdChanged() {
            mainWindow.handleThemeRotationTrackChanged()
        }
    }

    Timer {
        id: themeRotationTimer
        objectName: "themeRotationTimer"
        interval: Math.max(3, Math.min(120,
                    PlayerExperienceController.themeCycleIntervalSeconds)) * 1000
        repeat: true
        running: PlayerExperienceController.immersiveMode
                 !== PlayerExperienceController.Off
                 && PlayerExperienceController.themeCycleEnabled
        onTriggered: mainWindow.advanceImmersivePreset()
    }

    Component.onCompleted: {
        resetThemeRotationTrackBaseline()
        synchronizeAudioVisualConsumer()
        if (qaImmersive) {
            PlayerExperienceController.hostMode =
                    qaImmersiveFullscreen
                    ? PlayerExperienceController.Fullscreen
                    : PlayerExperienceController.Windowed
            PlayerExperienceController.panelVisible = true
            PlayerExperienceController.immersiveMode =
                    PlayerExperienceController.TerrainReactor
        }
    }

    function openImportDialog() {
        var dialog = importDialogComponent.createObject(mainWindow)
        if (dialog)
            dialog.open()
    }

    function importFiles(urls) {
        ImportController.importUrls(urls)
    }

    // NativeDropRouter handles WM_DROPFILES and Qt-delivered drops first.  A
    // QML-level fallback is still required for Explorer/OLE routes that never
    // reach the native message filter (notably when a child surface owns the
    // drop target).  Keeping it separate avoids making ordinary file-dialog
    // imports unexpectedly start playback.
    function importDroppedFiles(urls) {
        playFirstDroppedTrack = true
        ImportController.importUrls(urls)
    }

    function handleShellDropUrls(urls): bool {
        if (!urls || urls.length === 0)
            return false
        var accepted = []
        for (var index = 0; index < urls.length; ++index) {
            var classified = ResourceFolderController.classifyDropUrl(urls[index])
            if (classified.kind === ResourceFolderController.Directory
                    || classified.kind === ResourceFolderController.AudioFile
                    || classified.kind === ResourceFolderController.VideoFile)
                accepted.push(classified.url)
        }
        if (accepted.length === 0)
            return false
        importDroppedFiles(accepted)
        return true
    }

    function resourceDropContainsPoint(x, y): bool {
        const navigation = (integratedShell || rollingShell) && shellLoader.item
                         ? (shellLoader.item.libraryNavigation || null) : null
        if (!navigation)
            return false
        const localPoint = navigation.mapFromItem(null, x, y)
        return navigation.resourceDropContainsPoint(localPoint.x, localPoint.y)
    }

    function handleResourceDropUrls(urls): bool {
        const navigation = (integratedShell || rollingShell) && shellLoader.item
                         ? (shellLoader.item.libraryNavigation || null) : null
        return navigation ? navigation.submitResourceUrls(urls) : false
    }

    function openFolderDialog() {
        var dialog = folderDialogComponent.createObject(mainWindow)
        if (dialog)
            dialog.open()
    }

    function openSettingsPage() {
        settingsWindowLoader.active = true
        Qt.callLater(function() {
            if (settingsWindowLoader.item)
                settingsWindowLoader.item.openSettings()
        })
    }

    function openEqualizer() {
        equalizerWindowLoader.active = true
        Qt.callLater(function() {
            if (equalizerWindowLoader.item)
                equalizerWindowLoader.item.openEqualizer()
        })
    }

    function editingText() {
        var item = mainWindow.activeFocusItem
        while (item) {
            if (item instanceof TextInput || item instanceof TextEdit)
                return true
            item = item.parent
        }
        return false
    }

    Component {
        id: importDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [ResourceFolderController.audioFileNameFilter]
            onAccepted: mainWindow.importFiles(selectedFiles)
        }
    }

    Component {
        id: folderDialogComponent
        FolderDialog {
            onAccepted: ImportController.importFolder(selectedFolder)
        }
    }

    LibraryNavigationActions {
        objectName: "embeddedLibraryActions"
        hostWindow: mainWindow
        filterModel: sharedFilterModel
        navigation: (mainWindow.integratedShell || mainWindow.rollingShell)
                    && shellLoader.item ? (shellLoader.item.libraryNavigation || null) : null
    }

    Loader {
        id: shellLoader
        objectName: "playerShellLoader"
        anchors.fill: parent
        visible: true
        sourceComponent: mainWindow.rollingShell ? rollingShellComponent
                         : mainWindow.integratedShell
                           ? integratedShellComponent : classicShellComponent
        onLoaded: {
            if (item && item.tagSearchText !== undefined)
                item.tagSearchText = mainWindow.tagSearchText
            if (PlayerExperienceController.lyricsVisible) {
                if (mainWindow.rollingShell)
                    mainWindow.showRollingLyricsPanel()
                else if (mainWindow.integratedShell)
                    mainWindow.showIntegratedLyricsPanel()
            }
        }
    }

    Connections {
        target: shellLoader.item
        ignoreUnknownSignals: true
        function onTagSearchTextChanged() {
            mainWindow.tagSearchText = shellLoader.item.tagSearchText
        }
    }

    Component {
        id: classicShellComponent
        Item {
            objectName: "classicPlayerShell"
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                TitleBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.titleBarHeight
                    window: mainWindow
                    showBrand: true
                    surfaceColor: Theme.background
                    onOpenSettings: mainWindow.openSettingsPage()
                }

                PlayerPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 128
                    waveformSession: sharedWaveformSession
                }

                PlayerControls {
                    objectName: "playerControls"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    emptyMode: false
                    shellMode: 0
                    onOpenEqualizerRequested: mainWindow.openEqualizer()
                }
            }
        }
    }

    Component {
        id: integratedBottomBarComponent
        IntegratedPlayerControls {
            emptyMode: false
            onTogglePlaylistRequested: {
                if (shellLoader.item
                        && shellLoader.item.listPanelExpanded !== undefined)
                    shellLoader.item.listPanelExpanded =
                            !shellLoader.item.listPanelExpanded
            }
            onOpenEqualizerRequested: mainWindow.openEqualizer()
        }
    }

    Component {
        id: integratedShellComponent
        IntegratedPlayerShell {
            filterModel: sharedFilterModel
            hostWindow: mainWindow
            lyricsService: LyricsService
            waveformLayers: sharedWaveformSession.layers
            waveformDurationMs: sharedWaveformSession.durationMs
            sidePanelPage: mainWindow.integratedSidePanelPage
            sidePanelExpanded: mainWindow.integratedSidePanelExpanded
            bottomBarComponent: integratedBottomBarComponent
            onOpenSettingsRequested: mainWindow.openSettingsPage()
            onSidePanelPageChanged: {
                mainWindow.integratedSidePanelPage = sidePanelPage
            }
            onSidePanelExpandedChanged: {
                mainWindow.integratedSidePanelExpanded = sidePanelExpanded
            }
        }
    }

    Component {
        id: rollingShellComponent
        RollingPlayerShell {
            hostWindow: mainWindow
            playback: mainWindow.playback
            waveformSession: sharedWaveformSession
            filterModel: sharedFilterModel
            sidePanelPage: mainWindow.rollingSidePanelPage
            sidePanelExpanded: mainWindow.rollingSidePanelExpanded
            onOpenSettingsRequested: mainWindow.openSettingsPage()
            onOpenEqualizerRequested: mainWindow.openEqualizer()
            onSidePanelPageChanged: {
                mainWindow.rollingSidePanelPage = sidePanelPage
            }
            onSidePanelExpandedChanged: {
                mainWindow.rollingSidePanelExpanded = sidePanelExpanded
            }
        }
    }

    LyricsPanel {
        id: normalLyricsPanel
        objectName: "normalLyricsPanel"
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(42, parent.height - height - 66)
        width: Math.min(640, parent.width - 40)
        height: 104
        visible: PlayerExperienceController.lyricsVisible
                 && PlayerExperienceController.immersiveMode
                    === PlayerExperienceController.Off
                 && !mainWindow.rollingShell
                 && !mainWindow.integratedShell
                 && !WindowController.listWindowVisible
        spatialMode: false
        z: 70
        onCloseRequested: PlayerExperienceController.lyricsVisible = false
    }

    Item {
        id: windowedImmersiveHost
        objectName: "windowedImmersiveHost"
        anchors.fill: parent
        visible: false
        z: 80
    }

    Component {
        id: immersiveSurfaceComponent
        ImmersiveSurface {
            visible: false
            waveformSession: sharedWaveformSession
            renderingEnabled: mainWindow.immersiveRenderingEnabled
            qaSyntheticFeatures: mainWindow.qaImmersiveSynthetic
            z: 80
        }
    }

    Component {
        id: fullscreenWindowComponent
        ImmersiveWindow {
            dropUrlsSubmitter: mainWindow.handleShellDropUrls
            waveformSession: sharedWaveformSession
            renderingEnabled: mainWindow.immersiveRenderingEnabled
            qaSyntheticFeatures: mainWindow.qaImmersiveSynthetic
            qaViewportWidth: mainWindow.qaImmersiveWidth
            qaViewportHeight: mainWindow.qaImmersiveHeight
        }
    }

    Component {
        id: desktopWindowComponent
        Window {
            objectName: "immersiveDesktopWindow"
            visible: false
            x: Screen.virtualX
            y: Screen.virtualY
            width: Screen.width
            height: Screen.height
            flags: Qt.FramelessWindowHint
                   | Qt.WindowStaysOnBottomHint
                   | (PlayerExperienceController.desktopMousePassthrough
                      ? Qt.WindowTransparentForInput : 0)
            color: "transparent"
            title: qsTr("AgPlayer 桌面沉浸")
            onClosing: function(close) {
                close.accepted = false
                PlayerExperienceController.immersiveMode =
                        PlayerExperienceController.Off
            }
        }
    }

    Item {
        id: immersiveCoordinator
        objectName: "immersiveCoordinator"
        visible: false
        width: 0
        height: 0
        property int handoffPhase: 0 // 0 stable, 1 detaching, 2 attaching
        property int attachedHostMode: -1
        property int requestedHostMode: PlayerExperienceController.hostMode
        property var fullscreenWindow: null
        property var desktopWindow: null
        property var surface: null
        property int releasePolls: 0
        readonly property int releasePollLimit: 300
        property bool handoffTimedOut: false

        function ensureSurface() {
            ensureWindow(PlayerExperienceController.Windowed)
            if (!surface && fullscreenWindow)
                surface = fullscreenWindow.surfaceItem
            return surface
        }

        function ensureWindow(mode) {
            if (!fullscreenWindow) {
                fullscreenWindow = fullscreenWindowComponent.createObject(mainWindow)
                fullscreenWindow.visible = false
            }
        }

        function targetItem(mode) {
            ensureWindow(mode)
            return fullscreenWindow ? fullscreenWindow.contentItem : null
        }

        function currentWindow(mode) {
            return fullscreenWindow
        }

        function refreshExposure() {
            if (!surface)
                return
            var window = currentWindow(attachedHostMode)
            surface.hostExposed = !!window && window.visible
                    && window.visibility !== Window.Minimized
                    && window.visibility !== Window.Hidden
        }

        function hideDetachedWindows() {
            if (PlayerExperienceController.immersiveMode
                    === PlayerExperienceController.Off && fullscreenWindow)
                fullscreenWindow.visible = false
        }

        function detach() {
            if (!surface) {
                attachedHostMode = -1
                return
            }
            surface.hostExposed = false
            surface.visible = false
            attachedHostMode = -1
            if (fullscreenWindow)
                fullscreenWindow.visible = false
        }

        function beginHandoff() {
            requestedHostMode = PlayerExperienceController.hostMode
            if (handoffPhase !== 0)
                return
            handoffPhase = 1
            releasePolls = 0
            handoffTimedOut = false
            detach()
            releaseTimer.interval = 16
            releaseTimer.start()
        }

        function attachRequestedHost() {
            if (!ensureSurface())
                return false
            // A pending detach timeout belongs to the previous presentation.
            // It must not turn a rapidly reopened immersive window off.
            releaseTimer.stop()
            releaseTimer.interval = 16
            releasePolls = 0
            handoffTimedOut = false
            handoffPhase = 2
            surface.hostMode = requestedHostMode
            surface.visible = true
            if (fullscreenWindow)
                fullscreenWindow.synchronizeHost()
            attachedHostMode = requestedHostMode
            refreshExposure()
            handoffPhase = 0
            hideDetachedWindows()
            return true
        }

        function synchronize() {
            requestedHostMode = PlayerExperienceController.hostMode
            if (PlayerExperienceController.immersiveMode
                    === PlayerExperienceController.Off) {
                if (!surface) {
                    handoffPhase = 0
                    attachedHostMode = -1
                    hideDetachedWindows()
                    return
                }
                if (handoffPhase === 0)
                    beginHandoff()
                return
            }
            if (!ensureSurface())
                return
            if (attachedHostMode < 0) {
                if (!attachRequestedHost())
                    handoffPhase = 0
                return
            }
            if (attachedHostMode !== requestedHostMode) {
                surface.hostMode = requestedHostMode
                attachedHostMode = requestedHostMode
                if (fullscreenWindow)
                    fullscreenWindow.synchronizeHost()
            }
            refreshExposure()
        }

        Timer {
            id: releaseTimer
            interval: 16
            repeat: true
            onTriggered: {
                ++immersiveCoordinator.releasePolls
                var terrain = null
                if (immersiveCoordinator.surface)
                    terrain = immersiveCoordinator.surface.terrainItem
                if (terrain && terrain.liveRendererCount > 0) {
                    if (immersiveCoordinator.releasePolls
                            >= immersiveCoordinator.releasePollLimit) {
                        immersiveCoordinator.handoffTimedOut = true
                        interval = 250
                        if (PlayerExperienceController.immersiveMode
                                !== PlayerExperienceController.Off)
                            PlayerExperienceController.immersiveMode =
                                    PlayerExperienceController.Off
                    }
                    return
                }
                stop()
                interval = 16
                if (PlayerExperienceController.immersiveMode
                        === PlayerExperienceController.Off) {
                    immersiveCoordinator.handoffPhase = 0
                    immersiveCoordinator.hideDetachedWindows()
                    return
                }
                immersiveCoordinator.requestedHostMode =
                        PlayerExperienceController.hostMode
                if (!immersiveCoordinator.attachRequestedHost()) {
                    immersiveCoordinator.handoffPhase = 0
                    return
                }
                if (immersiveCoordinator.attachedHostMode
                        !== PlayerExperienceController.hostMode)
                    immersiveCoordinator.beginHandoff()
            }
        }

        Connections {
            target: PlayerExperienceController
            function onImmersiveModeChanged() { immersiveCoordinator.synchronize() }
            function onHostModeChanged() { immersiveCoordinator.synchronize() }
        }
        Connections {
            target: mainWindow
            function onVisibilityChanged() { immersiveCoordinator.refreshExposure() }
        }
        Connections {
            target: immersiveCoordinator.fullscreenWindow
            ignoreUnknownSignals: true
            function onVisibilityChanged() { immersiveCoordinator.refreshExposure() }
        }
        Connections {
            target: immersiveCoordinator.desktopWindow
            ignoreUnknownSignals: true
            function onVisibilityChanged() { immersiveCoordinator.refreshExposure() }
        }
        // Reparenting a QQuickRhiItem while Main is still constructing can
        // invalidate its scene-graph bindings. Queue the first handoff after
        // QQmlComponent::create() has returned to the event loop.
        Component.onCompleted: Qt.callLater(synchronize)
    }

    FileDropArea {
        objectName: "mainFileDropFallback"
        anchors.fill: parent
        // Keep the fallback beneath interactive player controls.  It only
        // participates in drag-and-drop hit testing; putting it above the
        // controls steals click and seek input on some Qt/Windows builds.
        z: -5
        urlsSubmitter: mainWindow.handleShellDropUrls
    }

    ImportStatusPanel {
        id: importStatus
        objectName: "importStatusPanel"
        anchors.fill: parent
        anchors.topMargin: 40
        z: 90
        onRetryRequested: mainWindow.openImportDialog()
    }

    Loader {
        id: videoPlaybackLoader
        objectName: "videoPlaybackLoader"
        anchors.fill: parent
        z: 1000
        active: mainWindow.videoPlayback
                && Boolean(mainWindow.videoPlayback.visible)
        sourceComponent: Component {
            VideoPlaybackView {
                playback: mainWindow.playback
                videoPlayback: mainWindow.videoPlayback
                frameController: VideoPlaybackController
                fullscreen: mainWindow.videoFullscreen
                onFullscreenRequested: {
                    if (mainWindow.videoFullscreen)
                        mainWindow.exitVideoFullscreen()
                    else
                        mainWindow.enterVideoFullscreen()
                }
                onReturnRequested: mainWindow.leaveVideoPlayback()
            }
        }
    }

    Loader {
        id: settingsWindowLoader
        active: false
        sourceComponent: Component {
            SettingsWindow {}
        }
    }

    Loader {
        id: equalizerWindowLoader
        objectName: "equalizerWindowLoader"
        active: false
        sourceComponent: Component {
            EqualizerWindow {}
        }
    }

    DockedWindowFrame {
        anchors.fill: parent
        dockEdge: WindowController.listWindowVisible
                  && !WindowController.listWindowDetached
                  ? WindowController.listDockEdge : "none"
        windowRole: "main"
        maximized: mainWindow.visibility === Window.Maximized
        showFill: false
        z: 100
    }

    Shortcut {
        sequence: "Space"
        context: Qt.WindowShortcut
        enabled: !mainWindow.editingText() && !WindowController.audioToolsVisible
            && !AudioEditorController.editorPlaybackOwnsPlayer
        onActivated: PlaybackController.togglePlayback()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: mainWindow.videoFullscreen && videoPlaybackLoader.active
        onActivated: mainWindow.exitVideoFullscreen()
    }

    Shortcut {
        sequence: SettingsController.hkSearch
        context: Qt.ApplicationShortcut
        onActivated: WindowController.activateSearch()
    }

    Shortcut {
        sequence: SettingsController.hkWaveformMode
        context: Qt.ApplicationShortcut
        onActivated: SettingsController.cycleWaveformMode()
    }

    Shortcut {
        sequence: SettingsController.hkAudioTools
        context: Qt.ApplicationShortcut
        onActivated: WindowController.showAudioTools()
    }

    WindowResizeHandles {
        objectName: "mainResizeHandles"
        targetWindow: mainWindow
    }
}
