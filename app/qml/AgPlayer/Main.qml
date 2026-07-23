import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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

    LibraryFilterModel {
        id: filterModel
        objectName: "filterModel"
        sourceModel: LibraryModel
        searchText: searchFilter.searchText
        minRating: searchFilter.minRating
        minBpm: searchFilter.minBpm
        maxBpm: searchFilter.maxBpm
        category: sideNav.selectedCategory
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        SideNavigation {
            id: sideNav
            Layout.preferredWidth: 200
            Layout.minimumWidth: 180
            Layout.maximumWidth: 240
            Layout.fillHeight: true
            selectedCategory: filterModel.category
            allCount: LibraryModel.rowCount
            favoriteCount: LibraryModel.favoriteCount
            historyCount: LibraryModel.rowCount
            workoutCount: LibraryModel.rowCount
            carCount: LibraryModel.rowCount
            networkCount: LibraryModel.rowCount
            onCategorySelected: function(category) { filterModel.category = category }
            onImportRequested: mainWindow.openImportDialog()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                Layout.minimumHeight: 280
            }

            PlayerControls {
                id: playerControls
                Layout.fillWidth: true
                Layout.preferredHeight: 80
            }

            Rectangle {
                id: listContainer
                color: Theme.panel
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 160
                visible: !WindowController.listWindowDetached

                StackLayout {
                    id: listStack
                    anchors.fill: parent
                    currentIndex: filterModel.count > 0 ? 0 : 1

                    TrackList {
                        id: trackList
                        objectName: "trackList"
                        trackModel: filterModel
                    }

                    EmptyLibrary {
                        id: emptyLibrary
                        objectName: "emptyLibrary"
                        onImportRequested: mainWindow.openImportDialog()
                    }
                }
            }
        }
    }

    SearchFilter {
        id: searchFilter
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingMd
        z: 100
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
