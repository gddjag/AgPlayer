import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

ApplicationWindow {
    id: mainWindow
    objectName: "mainWindow"
    visible: true
    width: 960
    height: 298
    minimumWidth: SettingsController.playerShellMode === 1 ? 1180
                  : SettingsController.playerShellMode === 2 ? 1000 : 612
    minimumHeight: SettingsController.playerShellMode === 1 ? 720
                   : SettingsController.playerShellMode === 2 ? 420 : 228
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
    property int positionMs: playback ? playback.positionMs : 0
    property bool playFirstDroppedTrack: false
    property string tagSearchText: ""
    property int integratedSidePanelPage: 0
    property bool integratedSidePanelExpanded: true
    property bool immersiveRenderingEnabled: true
    property int previousShellMode: SettingsController.playerShellMode
    property alias waveformSession: sharedWaveformSession
    readonly property bool integratedShell:
        SettingsController.playerShellMode === 1
    readonly property bool rollingShell:
        SettingsController.playerShellMode === 2
    readonly property bool immersiveActuallyRendering:
        immersiveRenderingEnabled && PlayerExperienceController.panelVisible
    readonly property bool integratedLyricsRequested:
        integratedShell && integratedSidePanelExpanded
        && integratedSidePanelPage === 1
    readonly property bool qaImmersive:
        Qt.application.arguments.indexOf("--qa-immersive") >= 0
    readonly property bool qaImmersiveSynthetic:
        Qt.application.arguments.indexOf("--qa-immersive-synthetic") >= 0
    readonly property bool qaImmersiveFullscreen:
        Qt.application.arguments.indexOf("--qa-immersive-fullscreen") >= 0
    readonly property int qaImmersiveWidth: qaArgumentNumber("--qa-width", 0)
    readonly property int qaImmersiveHeight: qaArgumentNumber("--qa-height", 0)

    function qaArgumentNumber(name, fallback) {
        var index = Qt.application.arguments.indexOf(name)
        if (index < 0 || index + 1 >= Qt.application.arguments.length)
            return fallback
        var value = Number(Qt.application.arguments[index + 1])
        return isFinite(value) && value > 0 ? Math.round(value) : fallback
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

    // `active` is intentionally invokable-only. Keep one writer here so the
    // rolling shell and immersive surface cannot race each other's teardown.
    function synchronizeAudioVisualConsumer() {
        AudioVisualFeatureController.setActive(
                    rollingShell || immersiveActuallyRendering)
    }

    Binding {
        target: LyricsService
        property: "enabled"
        value: PlayerExperienceController.lyricsVisible
               || mainWindow.integratedLyricsRequested
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
    }

    Component.onCompleted: {
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
            nameFilters: [LibraryManagerController.audioFileNameFilter]
            onAccepted: mainWindow.importFiles(selectedFiles)
        }
    }

    Component {
        id: folderDialogComponent
        FolderDialog {
            onAccepted: ImportController.importFolder(selectedFolder)
        }
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
                    Layout.preferredHeight: 36
                    window: mainWindow
                    showBrand: true
                    onOpenSettings: mainWindow.openSettingsPage()
                }

                EmptyStartup {
                    id: emptyStartup
                    objectName: "emptyStartup"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: LibraryModel.count === 0 && !importStatus.active
                    onOpenFileRequested: mainWindow.openImportDialog()
                    onImportFolderRequested: mainWindow.openFolderDialog()
                }

                PlayerPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 128
                    visible: LibraryModel.count > 0
                    waveformSession: sharedWaveformSession
                }

                PlayerControls {
                    objectName: "playerControls"
                    Layout.fillWidth: true
                    Layout.preferredHeight: LibraryModel.count === 0 ? 72 : 64
                    emptyMode: LibraryModel.count === 0
                    shellMode: 0
                    onOpenEqualizerRequested: mainWindow.openEqualizer()
                }
            }
        }
    }

    Component {
        id: integratedBottomBarComponent
        IntegratedPlayerControls {
            emptyMode: LibraryModel.count === 0
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
            onOpenSettingsRequested: mainWindow.openSettingsPage()
            onOpenEqualizerRequested: mainWindow.openEqualizer()
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
        spatialMode: false
        z: 70
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
        onUrlsDropped: function(urls) {
            mainWindow.importDroppedFiles(urls)
        }
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
