import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

ApplicationWindow {
    id: mainWindow
    objectName: "mainWindow"
    visible: true
    width: 1448
    height: 1086
    minimumWidth: 1000
    minimumHeight: 700
    flags: Qt.FramelessWindowHint
    color: Theme.background
    title: "AgPlayer"

    // Shared-state surface so the main window and the mini player can bind to
    // the same playback source. Defaults to the production singleton; tests
    // override this with a fake QtObject to verify shared state without audio.
    property var playback: PlaybackController
    property int positionMs: playback.positionMs

    function openImportDialog() {
        var dialog = Qt.createQmlObject(
            'import QtQuick.Dialogs\nimport AgPlayer\n' +
            'FileDialog {\n' +
            '    fileMode: FileDialog.OpenFiles\n' +
            '    nameFilters: ["Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)"]\n' +
            '    onAccepted: ImportController.importUrls(files)\n' +
            '}',
            mainWindow,
            "importDialog"
        )
        if (dialog)
            dialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            id: titleBar
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            window: mainWindow
        }

        PlayerPane {
            id: playerPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 360
        }

        PlayerControls {
            id: playerControls
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            onImportRequested: mainWindow.openImportDialog()
        }

        Rectangle {
            color: Theme.panel
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            Layout.minimumHeight: 160

            StackLayout {
                id: listStack
                anchors.fill: parent
                currentIndex: LibraryModel.rowCount > 0 ? 0 : 1

                TrackList {
                    id: trackList
                    objectName: "trackList"
                }

                EmptyLibrary {
                    id: emptyLibrary
                    objectName: "emptyLibrary"
                    onImportRequested: mainWindow.openImportDialog()
                }
            }
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
}
