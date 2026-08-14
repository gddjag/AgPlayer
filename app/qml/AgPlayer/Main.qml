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
    minimumWidth: 612
    minimumHeight: 228
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
    palette.base: Theme.elevated
    palette.alternateBase: Theme.panel
    palette.text: Theme.primaryText
    palette.button: Theme.elevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.mid: Theme.border

    // Shared-state surface so the main window and the mini player can bind to
    // the same playback source. Defaults to the production singleton; tests
    // override this with a fake QtObject to verify shared state without audio.
    property var playback: PlaybackController
    property int positionMs: playback ? playback.positionMs : 0
    property bool playFirstDroppedTrack: false

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

    Component.onCompleted: {
        Theme.mode = SettingsController.themeMode
    }

    Connections {
        target: SettingsController
        function onThemeModeChanged() {
            Theme.mode = SettingsController.themeMode
        }
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
        id: filterModel
        objectName: "filterModel"
        sourceModel: LibraryModel
        playlistModel: PlaylistModel
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
            nameFilters: ["Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)"]
            onAccepted: mainWindow.importFiles(selectedFiles)
        }
    }

    Component {
        id: folderDialogComponent
        FolderDialog {
            onAccepted: ImportController.importFolder(selectedFolder)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            id: titleBar
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
            id: playerPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 128
            visible: LibraryModel.count > 0
        }

        PlayerControls {
            id: playerControls
            objectName: "playerControls"
            Layout.fillWidth: true
            // Empty startup reserves enough room for the responsive action
            // area.  The controls stay anchored at the bottom instead of
            // cutting through the format hint on compact windows.
            Layout.preferredHeight: LibraryModel.count === 0 ? 72 : 64
            emptyMode: LibraryModel.count === 0
            onOpenEqualizerRequested: mainWindow.openEqualizer()
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
