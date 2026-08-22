import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Window {
    id: listWindow
    objectName: "listWindow"
    visible: false
    width: 1447
    height: 570
    minimumWidth: 1284
    minimumHeight: 320
    flags: Qt.FramelessWindowHint
    color: "transparent"
    title: qsTr("AgPlayer 音乐列表")
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

    property var windows: WindowController
    property var filterModel: null
    property var playlistModel: PlaylistModel
    property string importTargetPlaylistId: ""
    property string exportPlaylistId: ""

    function routeNavigationNode(nodeType, nodeId, resourceFolder) {
        if (!filterModel)
            return
        if (nodeType === "library" || nodeType === "favorites"
                || nodeType === "playlist") {
            filterModel.tagKey = ""
            TagModel.selectedKey = ""
            filterModel.resourceFolder = ""
            filterModel.category = nodeType === "library" ? "all"
                                 : nodeType === "favorites" ? "favorites"
                                 : nodeId.substring("playlist:".length)
        } else if (nodeType === "tags") {
            filterModel.resourceFolder = ""
            filterModel.category = "all"
        } else if (nodeType === "resourceRoot"
                   || nodeType === "resourceFolder") {
            filterModel.tagKey = ""
            TagModel.selectedKey = ""
            filterModel.category = "all"
            filterModel.resourceFolder = resourceFolder
        }
    }

    onClosing: function(close) {
        close.accepted = false
        windows.hideListWindow()
    }

    Connections {
        target: windows
        function onSearchRequested() { searchFilter.focusSearch() }
    }
    Connections {
        target: filterModel
        function onCategoryChanged() {
            listWindow.ensureLibraryManagerHeight()
        }
    }

    function ensureLibraryManagerHeight() {
        if (!filterModel || filterModel.category !== "library") {
            listWindow.minimumHeight = 420
            return
        }
        var geometry = listWindow.screen
                       ? listWindow.screen.availableGeometry : null
        var available = geometry && geometry.height > 0
                        ? geometry.height : 1080
        var targetHeight = Math.min(available,
                                    libraryManagerPage.preferredWindowHeight)
        listWindow.minimumHeight = targetHeight
        if (listWindow.height < targetHeight)
            listWindow.height = targetHeight
    }
    Connections {
        target: ImportController
        function onFinished() {
            if (listWindow.importTargetPlaylistId
                    && ImportController.importedTrackIds.length > 0) {
                listWindow.playlistModel.addTracks(
                    listWindow.importTargetPlaylistId,
                    ImportController.importedTrackIds)
            }
            listWindow.importTargetPlaylistId = ""
        }
    }

    function customCategory(): string {
        var category = filterModel ? filterModel.category : "all"
        return category !== "all" && category !== "favorites"
                && category !== "history" && category !== "library" ? category : ""
    }
    function customPlaylistEmpty(): bool {
        var playlistId = customCategory()
        return playlistId.length > 0
                && playlistModel.trackIdsForPlaylist(playlistId).length === 0
    }
    function beginImport(urls) {
        importTargetPlaylistId = customCategory()
        ImportController.importUrls(urls)
    }
    function handleResourceDropUrls(urls) {
        var audioUrls = []
        for (var index = 0; index < urls.length; ++index) {
            if (!LibraryNavigationModel.addResourceFolder(urls[index]))
                audioUrls.push(urls[index])
        }
        if (audioUrls.length > 0)
            beginImport(audioUrls)
    }
    function openImportDialog() {
        importTargetPlaylistId = customCategory()
        var dialog = importDialogComponent.createObject(listWindow)
        if (dialog) dialog.open()
    }
    function openRenameDialog(playlistId) {
        renamePlaylistDialog.playlistId = playlistId
        renamePlaylistField.text = playlistModel.nameForId(playlistId)
        renamePlaylistDialog.open()
        renamePlaylistField.forceActiveFocus()
        renamePlaylistField.selectAll()
    }
    function trackPathsForPlaylist(playlistId) {
        var result = ({})
        var ids = playlistModel.trackIdsForPlaylist(playlistId)
        for (var index = 0; index < ids.length; ++index) {
            var details = LibraryModel.trackForId(ids[index])
            if (details && details.path)
                result[ids[index]] = details.path
        }
        return result
    }

    Component {
        id: importDialogComponent
        FileDialog {
            fileMode: FileDialog.OpenFiles
            nameFilters: [
                "Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma)"
            ]
            onAccepted: listWindow.beginImport(selectedFiles)
        }
    }

    FileDialog {
        id: importPlaylistDialog
        title: qsTr("导入歌单")
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "Playlist files (*.m3u *.m3u8 *.pls *.json)"
        ]
        onAccepted: {
            var paths = playlistModel.pathsFromPlaylist(selectedFile.toString())
            var playlistId = playlistModel.importPlaylist(selectedFile.toString())
            if (playlistId) {
                if (filterModel) filterModel.category = playlistId
                ImportController.importPaths(paths)
            }
        }
    }

    Dialog {
        id: exportOptionsDialog
        title: qsTr("导出歌单")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: exportPlaylistDialog.open()
        contentItem: CheckBox {
            id: copyPlaylistFiles
            text: qsTr("同时复制歌曲文件")
            checked: false
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }

    FileDialog {
        id: exportPlaylistDialog
        title: qsTr("导出歌单")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "m3u8"
        nameFilters: [
            "M3U8 playlist (*.m3u8)",
            "AgPlayer playlist (*.json)",
            "PLS playlist (*.pls)"
        ]
        onAccepted: playlistModel.exportPlaylist(
                        listWindow.exportPlaylistId,
                        selectedFile.toString(),
                        listWindow.trackPathsForPlaylist(
                            listWindow.exportPlaylistId),
                        copyPlaylistFiles.checked)
    }

    Dialog {
        id: createPlaylistDialog
        title: qsTr("新建歌单")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            createPlaylistField.clear()
            createPlaylistField.forceActiveFocus()
        }
        onAccepted: {
            var id = playlistModel.createPlaylist(createPlaylistField.text)
            if (id && filterModel) filterModel.category = id
        }
        contentItem: TextField {
            id: createPlaylistField
            placeholderText: qsTr("歌单名称")
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }
    Dialog {
        id: renamePlaylistDialog
        property string playlistId
        title: qsTr("重命名歌单")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: playlistModel.renamePlaylist(
                        playlistId, renamePlaylistField.text)
        contentItem: TextField { id: renamePlaylistField }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }
    Dialog {
        id: removePlaylistDialog
        property string playlistId
        title: qsTr("删除歌单")
        modal: true
        width: 420
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            if (playlistModel.removePlaylist(playlistId) && filterModel)
                filterModel.category = "all"
        }
        contentItem: Label {
            width: 380
            text: qsTr("确定删除这个歌单？音乐文件不会被删除。")
            color: Theme.primaryText
            wrapMode: Text.Wrap
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }

    DockedWindowFrame {
        id: surface
        anchors.fill: parent
        dockEdge: windows.mainVisible && !windows.listWindowDetached
                  ? windows.listDockEdge : "none"
        windowRole: "list"
        maximized: listWindow.visibility === Window.Maximized

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: titleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        objectName: "listWindowMinimizeButton"
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.secondaryText
                        icon.width: 16
                        icon.height: 16
                        onClicked: listWindow.showMinimized()
                        background: null
                    }
                    ToolButton {
                        objectName: "listWindowCloseButton"
                        icon.source: Theme.icon("close-fill")
                        icon.color: Theme.secondaryText
                        icon.width: 16
                        icon.height: 16
                        onClicked: windows.hideListWindow()
                        background: Rectangle {
                            color: parent.hovered ? Theme.favoriteRed
                                                  : "transparent"
                            radius: Theme.radiusSm
                        }
                    }
                }
                MouseArea {
                    objectName: "listWindowMoveArea"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.rightMargin: 84
                    acceptedButtons: Qt.LeftButton
                    onPressed: listWindow.startSystemMove()
                }
            }

            Rectangle {
                id: listWorkspace
                objectName: "listWorkspace"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.bottomMargin: 8
                readonly property int leftColumnWidth: 256
                readonly property int rightColumnWidth: 328
                readonly property int centerMinimumWidth: 680
                readonly property int dividerWidth: 1
                readonly property real centerWidth: centerColumn.width
                color: Theme.listWorkspaceSurface
                border.color: Theme.listWorkspaceBorder
                border.width: 1
                radius: Theme.radiusMd
                clip: true

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0

                    SideNavigation {
                        id: sideNavigation
                        objectName: "referenceSideNavigation"
                        Layout.preferredWidth: listWorkspace.leftColumnWidth
                        Layout.minimumWidth: listWorkspace.leftColumnWidth
                        Layout.maximumWidth: listWorkspace.leftColumnWidth
                        Layout.fillHeight: true
                        navigationModel: LibraryNavigationModel
                        selectedCategory: filterModel ? filterModel.category : "all"
                        selectedTagKey: filterModel ? filterModel.tagKey : ""
                        selectedResourceFolder: filterModel
                                                ? filterModel.resourceFolder : ""
                        playlistModel: listWindow.playlistModel
                        onNavigationSelected: function(nodeType, nodeId,
                                                       resourceFolder) {
                            listWindow.routeNavigationNode(nodeType, nodeId,
                                                           resourceFolder)
                        }
                        onCreatePlaylistRequested: createPlaylistDialog.open()
                        onRenamePlaylistRequested: function(playlistId) {
                            listWindow.openRenameDialog(playlistId)
                        }
                        onRemovePlaylistRequested: function(playlistId) {
                            removePlaylistDialog.playlistId = playlistId
                            removePlaylistDialog.open()
                        }
                        onImportRequested: listWindow.openImportDialog()
                        onImportPlaylistRequested: importPlaylistDialog.open()
                        onExportPlaylistRequested: function(playlistId) {
                            listWindow.exportPlaylistId = playlistId
                            copyPlaylistFiles.checked = false
                            exportOptionsDialog.open()
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: listWorkspace.dividerWidth
                        Layout.fillHeight: true
                        color: Theme.listDivider
                    }

                    ColumnLayout {
                        id: centerColumn
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: listWorkspace.centerMinimumWidth
                        spacing: 0

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: filterModel && filterModel.category === "library" ? 3
                                          : LibraryModel.count === 0
                                            || listWindow.customPlaylistEmpty() ? 1
                                          : filterModel && filterModel.count > 0
                                            ? 0 : 2
                            TrackList {
                                objectName: "sharedTrackList"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                trackModel: filterModel
                                playlistModel: listWindow.playlistModel
                                selectedCategory: filterModel
                                                  ? filterModel.category : "all"
                                searchText: filterModel
                                            ? filterModel.searchText : ""
                            }
                            EmptyLibrary {
                                objectName: "emptyLibrary"
                                playlistMode: listWindow.customPlaylistEmpty()
                                onImportRequested: listWindow.openImportDialog()
                            }
                            Item {
                                objectName: "emptyFilteredResult"
                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: 10
                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: qsTr("未找到符合条件的歌曲")
                                        color: Theme.secondaryText
                                        font.pixelSize: 16
                                    }
                                    Button {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: qsTr("一键清空筛选")
                                        onClicked: searchFilter.clearFilters()
                                    }
                                }
                            }
                            LibraryManagerPage {
                                id: libraryManagerPage
                                objectName: "libraryManagerPageInList"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                onPreferredWindowHeightChanged:
                                    listWindow.ensureLibraryManagerHeight()
                            }
                        }

                        SearchFilter {
                            id: searchFilter
                            objectName: "librarySearchFilter"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 66
                            visible: !filterModel || filterModel.category !== "library"
                            searchText: filterModel ? filterModel.searchText : ""
                            exactRating: filterModel ? filterModel.exactRating : 0
                            minBpm: filterModel ? filterModel.minBpm : 60
                            maxBpm: filterModel ? filterModel.maxBpm : 160
                            onSearchTextChanged: if (filterModel)
                                                     filterModel.searchText = searchText
                            onExactRatingChanged: if (filterModel)
                                                      filterModel.exactRating = exactRating
                            onMinBpmChanged: if (filterModel)
                                                 filterModel.minBpm = minBpm
                            onMaxBpmChanged: if (filterModel)
                                                 filterModel.maxBpm = maxBpm
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: listWorkspace.dividerWidth
                        Layout.fillHeight: true
                        color: Theme.listDivider
                    }

                    TagManagementPanel {
                        objectName: "tagManagementPanel"
                        Layout.preferredWidth: listWorkspace.rightColumnWidth
                        Layout.minimumWidth: listWorkspace.rightColumnWidth
                        Layout.maximumWidth: listWorkspace.rightColumnWidth
                        Layout.fillHeight: true
                        tagModel: TagModel
                        filterModel: listWindow.filterModel
                    }
                }
            }
        }

        Rectangle {
            objectName: "listSnapPreview"
            anchors.fill: parent
            z: 100
            visible: windows.snapPreviewEdge !== "none"
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
            radius: Theme.windowRadius
        }
    }

    // Fallback for Explorer/OLE drops that bypass the native window message
    // filter.  It deliberately lives above the visual surface but only reacts
    // to drops, so normal list selection and dragging remain untouched.
    FileDropArea {
        objectName: "listFileDropFallback"
        anchors.fill: parent
        z: -5
        onUrlsDropped: function(urls) {
            listWindow.handleResourceDropUrls(urls)
        }
    }

    WindowResizeHandles {
        objectName: "listResizeHandles"
        targetWindow: listWindow
    }

}
