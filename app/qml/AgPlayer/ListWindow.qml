import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

// Stand-alone playlist window. It is always shown alongside the main player
// window and reuses the LibraryFilterModel owned by Main.qml so filtering and
// the track list stay in sync between the two windows.
Window {
    id: listWindow
    objectName: "listWindow"
    visible: false
    width: 1040
    height: 560
    minimumWidth: 720
    minimumHeight: 320
    flags: Qt.FramelessWindowHint
    color: Theme.background
    title: qsTr("AgPlayer Track List")

    // Injected dependencies — defaults keep production wiring implicit.
    property var windows: WindowController
    property var filterModel: null

    // Internal drag state for custom frameless window movement with
    // continuous magnetic snapping through WindowController.
    property point dragStartMouse
    property point dragStartWindow
    property bool dragActive: false

    // Keep the controller informed of the current geometry so it can
    // persist / expose position and size to other UI surfaces.
    onXChanged: if (windows) windows.listWindowX = x
    onYChanged: if (windows) windows.listWindowY = y
    onWidthChanged: if (windows) windows.listWindowWidth = width
    onHeightChanged: if (windows) windows.listWindowHeight = height

    Connections {
        target: windows
        function onSearchRequested() {
            if (searchFilter)
                searchFilter.focusSearch()
        }
    }

    function openImportDialog() {
        var dialog = importDialogComponent.createObject(listWindow)
        if (dialog)
            dialog.open()
    }

    Component {
        id: importDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: ["Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)"]
            onAccepted: ImportController.importUrls(selectedFiles)
        }
    }

    Rectangle {
        id: surface
        anchors.fill: parent
        color: Theme.background
        border.color: Theme.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0

            // --- Title bar: drag area + close button ---
            Rectangle {
                id: titleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                color: Theme.panel

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMd
                    anchors.rightMargin: Theme.spacingMd
                    spacing: Theme.spacingSm

                    Image {
                        source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        sourceSize.width: 16
                        sourceSize.height: 16
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        fillMode: Image.PreserveAspectFit
                    }

                    Text {
                        text: qsTr("音乐列表")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        id: closeButton
                        objectName: "listWindowCloseButton"
                        icon.source: Theme.icon("close-fill")
                        icon.color: Theme.secondaryText
                        icon.width: 14
                        icon.height: 14
                        Accessible.name: qsTr("Hide playlist window")
                        focusPolicy: Qt.StrongFocus
                        onClicked: windows.hideListWindow()
                        ToolTip.text: Accessible.name
                        ToolTip.visible: hovered

                        background: Rectangle {
                            color: !parent.enabled ? "transparent"
                                  : parent.pressed ? Theme.favoriteRed
                                  : parent.visualFocus ? Theme.border
                                  : parent.hovered ? Theme.favoriteRed
                                  : "transparent"
                            border.color: parent.visualFocus ? Theme.cyan : "transparent"
                            border.width: parent.visualFocus ? 2 : 0
                            radius: Theme.radiusSm
                        }
                    }
                }

                // Drag the frameless window from the title bar. The controller
                // applies magnetic snapping while the drag is in progress.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    z: -1
                    onPressed: function(mouse) {
                        dragStartMouse = Qt.point(mouse.x, mouse.y)
                        dragStartWindow = Qt.point(listWindow.x, listWindow.y)
                        dragActive = true
                    }
                    onPositionChanged: function(mouse) {
                        if (!dragActive)
                            return

                        var currentMouse = Qt.point(mouse.x, mouse.y)
                        var screenMouse = Qt.point(
                            dragStartWindow.x + currentMouse.x,
                            dragStartWindow.y + currentMouse.y)
                        var newX = dragStartWindow.x + (currentMouse.x - dragStartMouse.x)
                        var newY = dragStartWindow.y + (currentMouse.y - dragStartMouse.y)

                        windows.moveListWindow(newX, newY)

                        // Re-anchor the drag origin to the actual (possibly
                        // snapped) window position so the next move event stays
                        // relative to the mouse cursor.
                        dragStartWindow = Qt.point(listWindow.x, listWindow.y)
                        dragStartMouse = Qt.point(
                            screenMouse.x - listWindow.x,
                            screenMouse.y - listWindow.y)
                    }
                    onReleased: dragActive = false
                }
            }

            // --- Playlist body: side navigation + track list ---
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.background

                RowLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Left sidebar: categories and filters.
                    Rectangle {
                        Layout.preferredWidth: 220
                        Layout.fillHeight: true
                        color: Theme.background
                        border.color: Theme.border
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMd
                            spacing: Theme.spacingMd

                            SideNavigation {
                                id: sideNav
                                Layout.fillWidth: true
                                Layout.preferredHeight: 260
                                selectedCategory: filterModel ? filterModel.category : "all"
                                allCount: LibraryModel.rowCount
                                favoriteCount: LibraryModel.favoriteCount
                                historyCount: LibraryModel.rowCount
                                workoutCount: LibraryModel.rowCount
                                carCount: LibraryModel.rowCount
                                networkCount: LibraryModel.rowCount
                                onCategorySelected: function(category) {
                                    if (filterModel)
                                        filterModel.category = category
                                }
                                onImportRequested: listWindow.openImportDialog()
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.border
                            }

                            SearchFilter {
                                id: searchFilter
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                searchText: filterModel ? filterModel.searchText : ""
                                minRating: filterModel ? filterModel.minRating : 0
                                minBpm: filterModel ? filterModel.minBpm : 0.0
                                maxBpm: filterModel ? filterModel.maxBpm : 300.0
                                onSearchTextChanged: if (filterModel) filterModel.searchText = searchText
                                onMinRatingChanged: if (filterModel) filterModel.minRating = minRating
                                onMinBpmChanged: if (filterModel) filterModel.minBpm = minBpm
                                onMaxBpmChanged: if (filterModel) filterModel.maxBpm = maxBpm
                            }
                        }
                    }

                    // Right area: track list.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.background

                        StackLayout {
                            id: listStack
                            anchors.fill: parent
                            currentIndex: filterModel && filterModel.count > 0 ? 0 : 1

                            TrackList {
                                id: trackList
                                objectName: "detachedTrackList"
                                anchors.fill: parent
                                trackModel: filterModel
                            }

                            EmptyLibrary {
                                id: emptyLibrary
                                objectName: "emptyLibrary"
                                onImportRequested: listWindow.openImportDialog()
                            }
                        }
                    }
                }
            }
        }
    }
}
