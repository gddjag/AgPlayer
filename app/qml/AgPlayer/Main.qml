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
    readonly property bool integratedShell:
        SettingsController.playerShellMode === 1

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
        onActivated: SettingsController.waveformMode =
                         (SettingsController.waveformMode + 1) % 3
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
