import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

ApplicationWindow {
    id: mainWindow
    objectName: "mainWindow"
    visible: true
    width: 1040
    height: 520
    minimumWidth: 800
    minimumHeight: 420
    flags: Qt.FramelessWindowHint
    color: Theme.background
    title: "AgPlayer"

    // Shared-state surface so the main window and the mini player can bind to
    // the same playback source. Defaults to the production singleton; tests
    // override this with a fake QtObject to verify shared state without audio.
    property var playback: PlaybackController
    property int positionMs: playback.positionMs
    property bool lyricsVisible: false

    // The filter model is owned by the main window but consumed by the
    // separate ListWindow so filtering state stays in sync.
    LibraryFilterModel {
        id: filterModel
        objectName: "filterModel"
        sourceModel: LibraryModel
    }

    function openImportDialog() {
        var dialog = importDialogComponent.createObject(mainWindow)
        if (dialog)
            dialog.open()
    }

    Component {
        id: importDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: ["Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)"]
            onAccepted: ImportController.importUrls(files)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            id: titleBar
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            window: mainWindow
            onOpenSettings: settingsPage.open()
        }

        SettingsPage {
            id: settingsPage
            objectName: "settingsPage"
        }

        PlayerPane {
            id: playerPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 280
        }

        Rectangle {
            id: lyricsPanel
            Layout.fillWidth: true
            Layout.preferredHeight: lyricsVisible ? 160 : 0
            visible: lyricsVisible
            color: Theme.panel
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
            }

            Flickable {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                contentWidth: width
                contentHeight: lyricsText.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                ScrollIndicator.vertical: ScrollIndicator {}

                Text {
                    id: lyricsText
                    width: parent.width
                    text: PlaybackController.lyrics.length > 0
                          ? PlaybackController.lyrics
                          : qsTr("No lyrics available")
                    color: Theme.primaryText
                    font.pixelSize: 14
                    lineHeight: 1.6
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        PlayerControls {
            id: playerControls
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            onToggleLyrics: mainWindow.lyricsVisible = !mainWindow.lyricsVisible
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

    Shortcut {
        sequence: StandardKey.Find
        context: Qt.ApplicationShortcut
        onActivated: WindowController.activateSearch()
    }
}
