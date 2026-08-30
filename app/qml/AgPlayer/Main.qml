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
    minimumWidth: SettingsController.playerShellMode === 1 ? 1180 : 612
    minimumHeight: SettingsController.playerShellMode === 1 ? 720 : 228
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
    property alias waveformSession: sharedWaveformSession
    readonly property bool integratedShell:
        SettingsController.playerShellMode === 1
    readonly property bool integratedLyricsRequested:
        integratedShell && integratedSidePanelExpanded
        && integratedSidePanelPage === 1
    readonly property bool qaImmersive:
        Qt.application.arguments.indexOf("--qa-immersive") >= 0
    readonly property bool qaImmersiveSynthetic:
        Qt.application.arguments.indexOf("--qa-immersive-synthetic") >= 0

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

    Binding {
        target: LyricsService
        property: "enabled"
        value: PlayerExperienceController.lyricsVisible
               || mainWindow.integratedLyricsRequested
    }

    Component.onCompleted: {
        if (qaImmersive) {
            PlayerExperienceController.hostMode =
                    PlayerExperienceController.Windowed
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
        visible: PlayerExperienceController.immersiveMode
                 === PlayerExperienceController.Off
        sourceComponent: mainWindow.integratedShell
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
                    onOpenEqualizerRequested: mainWindow.openEqualizer()
                }
            }
        }
    }

    Component {
        id: integratedBottomBarComponent
        PlayerControls {
            objectName: "playerControls"
            emptyMode: LibraryModel.count === 0
            showListWindowButton: false
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
        visible: PlayerExperienceController.immersiveMode
                 !== PlayerExperienceController.Off
                 && immersiveCoordinator.attachedHostMode
                    === PlayerExperienceController.Windowed
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
        Window {
            objectName: "immersiveFullscreenWindow"
            visible: false
            flags: Qt.Window | Qt.FramelessWindowHint
            color: "black" // theme-color-allow: immersive compositor clear color
            title: qsTr("AgPlayer 全屏沉浸")
            onClosing: function(close) {
                close.accepted = false
                PlayerExperienceController.hostMode =
                        PlayerExperienceController.Windowed
            }
            Shortcut {
                sequence: "Escape"
                onActivated: PlayerExperienceController.hostMode =
                             PlayerExperienceController.Windowed
            }
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
            if (!surface)
                surface = immersiveSurfaceComponent.createObject(
                            windowedImmersiveHost)
            return surface
        }

        function ensureWindow(mode) {
            if (mode === PlayerExperienceController.Fullscreen
                    && !fullscreenWindow) {
                fullscreenWindow = fullscreenWindowComponent.createObject(mainWindow)
                fullscreenWindow.visible = false
            }
            if (mode === PlayerExperienceController.Desktop
                    && !desktopWindow) {
                desktopWindow = desktopWindowComponent.createObject(mainWindow)
                desktopWindow.visible = false
            }
        }

        function targetItem(mode) {
            if (mode === PlayerExperienceController.Windowed)
                return windowedImmersiveHost
            ensureWindow(mode)
            if (mode === PlayerExperienceController.Fullscreen)
                return fullscreenWindow ? fullscreenWindow.contentItem : null
            return desktopWindow ? desktopWindow.contentItem : null
        }

        function currentWindow(mode) {
            if (mode === PlayerExperienceController.Windowed)
                return mainWindow
            if (mode === PlayerExperienceController.Fullscreen)
                return fullscreenWindow
            return desktopWindow
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
            if (attachedHostMode !== PlayerExperienceController.Fullscreen
                    && fullscreenWindow)
                fullscreenWindow.visible = false
            if (attachedHostMode !== PlayerExperienceController.Desktop
                    && desktopWindow)
                desktopWindow.visible = false
        }

        function detach() {
            if (!surface) {
                attachedHostMode = -1
                return
            }
            var oldWindow = currentWindow(attachedHostMode)
            surface.attached = false
            surface.hostExposed = false
            surface.visible = false
            surface.parent = null
            surface.x = 0
            surface.y = 0
            surface.width = 0
            surface.height = 0
            attachedHostMode = -1
            if (oldWindow && oldWindow !== mainWindow)
                oldWindow.visible = false
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
            var target = targetItem(requestedHostMode)
            if (!target)
                return false
            handoffPhase = 2
            surface.parent = target
            surface.x = 0
            surface.y = 0
            surface.width = Qt.binding(function() {
                return surface.parent ? surface.parent.width : 0
            })
            surface.height = Qt.binding(function() {
                return surface.parent ? surface.parent.height : 0
            })
            surface.hostMode = requestedHostMode
            var targetWindow = currentWindow(requestedHostMode)
            if (targetWindow && targetWindow !== mainWindow) {
                targetWindow.visible = true
                if (requestedHostMode === PlayerExperienceController.Fullscreen)
                    targetWindow.showFullScreen()
            }
            surface.visible = true
            surface.attached = true
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
            if (attachedHostMode !== requestedHostMode)
                beginHandoff()
            else
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
                    if (immersiveCoordinator.surface) {
                        var doomedSurface = immersiveCoordinator.surface
                        immersiveCoordinator.surface = null
                        doomedSurface.destroy()
                    }
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
