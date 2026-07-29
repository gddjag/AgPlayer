import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

ApplicationWindow {
    id: mainWindow
    objectName: "mainWindow"
    visible: true
    width: 1228
    height: 399
    minimumWidth: 800
    minimumHeight: 360
    onClosing: function(close) {
        close.accepted = false
        WindowController.requestClose()
    }
    flags: Qt.FramelessWindowHint
    color: Theme.background
    title: "AgPlayer"

    // Shared-state surface so the main window and the mini player can bind to
    // the same playback source. Defaults to the production singleton; tests
    // override this with a fake QtObject to verify shared state without audio.
    property var playback: PlaybackController
    property int positionMs: playback.positionMs

    Component.onCompleted: {
        Theme.mode = SettingsController.themeMode
    }

    Connections {
        target: SettingsController
        function onThemeModeChanged() {
            Theme.mode = SettingsController.themeMode
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

    function openFolderDialog() {
        var dialog = folderDialogComponent.createObject(mainWindow)
        if (dialog)
            dialog.open()
    }

    function openSettingsPage() {
        settingsPageLoader.active = true
        if (settingsPageLoader.item)
            settingsPageLoader.item.open()
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
            Layout.preferredHeight: 40
            window: mainWindow
            showBrand: LibraryModel.count === 0
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
            Layout.minimumHeight: 260
            visible: LibraryModel.count > 0
        }

        PlayerControls {
            id: playerControls
            objectName: "playerControls"
            Layout.fillWidth: true
            Layout.preferredHeight: LibraryModel.count === 0 ? 128 : 72
            emptyMode: LibraryModel.count === 0
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            var urls = []
            for (var i = 0; i < drop.urls.length; i++) {
                urls.push(drop.urls[i])
            }
            ImportController.importUrls(urls)
            drop.acceptProposedAction()
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
        id: settingsPageLoader
        active: false
        sourceComponent: Component {
            SettingsPage {
                objectName: "settingsPage"
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.border
        border.width: 1
        radius: Theme.windowRadius
        z: 100
    }

    Shortcut {
        sequence: SettingsController.hkSearch
        context: Qt.ApplicationShortcut
        onActivated: WindowController.activateSearch()
    }

    Shortcut {
        sequence: SettingsController.hkWaveformMode
        context: Qt.ApplicationShortcut
        onActivated: SettingsController.setWaveformMode(
                         (SettingsController.waveformMode + 1) % 3)
    }

    Shortcut {
        sequence: SettingsController.hkAudioTools
        context: Qt.ApplicationShortcut
        onActivated: WindowController.showAudioTools()
    }
}
