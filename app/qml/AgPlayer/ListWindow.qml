import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Window {
    id: listWindow
    objectName: "listWindow"
    visible: false
    readonly property int titleBarHeight: 38
    readonly property int trackHeaderHeight: 56
    readonly property int defaultVisibleTrackCount: 10
    readonly property int filterBarHeight: 54
    readonly property int defaultTrackRowHeight:
        SettingsController.listWaveformThumbnailEnabled ? 62 : 42
    readonly property int defaultListHeight:
        titleBarHeight + trackHeaderHeight
        + defaultVisibleTrackCount * defaultTrackRowHeight + filterBarHeight
    width: 960
    height: defaultListHeight
    readonly property int pageMinimumWidth: 956
    minimumWidth: pageMinimumWidth
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
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightText
    palette.mid: Theme.border

    property var windows: WindowController
    property var filterModel: null
    property var playlistModel: PlaylistModel
    property string importTargetPlaylistId: ""
    property var activeImportDialog: null
    property bool importBatchActive: false
    property string exportPlaylistId: ""
    readonly property bool tagManagementMode:
        sideNavigation.activeNodeType === "tags"

    function enterNavigationState(nodeType, category, tagKey,
                                  resourceFolder) {
        if (!filterModel)
            return false
        filterModel.category = category || "all"
        filterModel.tagKey = tagKey || ""
        TagModel.selectedKey = tagKey || ""
        filterModel.resourceFolder = resourceFolder || ""
        sideNavigation.activeNodeType = nodeType || "library"
        return true
    }
    function enterCategory(category, nodeType) {
        var target = category || "all"
        var type = nodeType || (target === "all" ? "library"
                                : target === "favorites" ? "favorites"
                                : "playlist")
        return enterNavigationState(type, target, "", "")
    }
    function enterResource(nodeType, resourceFolder) {
        return enterNavigationState(nodeType, "all", "", resourceFolder)
    }
    function handleResourceFolderRemoved(folder) {
        if (!filterModel || !filterModel.resourceFolder)
            return false
        if (!LibraryManagerController.pathIsWithin(
                    filterModel.resourceFolder, folder))
            return false
        return enterCategory("all", "library")
    }
    function routeNavigationNode(nodeType, nodeId, resourceFolder) {
        if (nodeType === "library")
            return enterCategory("all", "library")
        if (nodeType === "favorites")
            return enterCategory("favorites", "favorites")
        if (nodeType === "playlist")
            return enterCategory(nodeId.substring("playlist:".length),
                                 "playlist")
        if (nodeType === "tags")
            return enterNavigationState("tags", "all",
                                        filterModel ? filterModel.tagKey : "",
                                        "")
        if (nodeType === "resourceRoot" || nodeType === "resourceFolder")
            return enterResource(nodeType, resourceFolder)
        return false
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
            if (!listWindow.importBatchActive)
                return
            if (listWindow.importTargetPlaylistId
                    && ImportController.importedTrackIds.length > 0) {
                listWindow.playlistModel.addTracks(
                    listWindow.importTargetPlaylistId,
                    ImportController.importedTrackIds)
            }
            listWindow.importTargetPlaylistId = ""
            listWindow.importBatchActive = false
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
        if (!urls || urls.length === 0 || importBatchActive
                || ImportController.busy || activeImportDialog)
            return false
        importTargetPlaylistId = customCategory()
        importBatchActive = true
        ImportController.importUrls(urls)
        return true
    }
    function importSelectedUrls(urls) {
        if (!urls || urls.length === 0 || importBatchActive
                || ImportController.busy)
            return false
        importBatchActive = true
        ImportController.importUrls(urls)
        return true
    }
    function handleListDropUrls(urls) {
        if (!urls || urls.length === 0)
            return false
        var accepted = []
        for (var index = 0; index < urls.length; ++index) {
            var classified = LibraryManagerController.classifyDropUrl(urls[index])
            if (classified.kind === LibraryManagerController.Directory
                    || classified.kind === LibraryManagerController.AudioFile)
                accepted.push(classified.url)
        }
        return accepted.length > 0 && beginImport(accepted)
    }
    function handleResourceDropUrls(urls) {
        if (!urls || urls.length === 0)
            return false
        var seenPaths = ({})
        var directoryPaths = []
        for (var index = 0; index < urls.length; ++index) {
            var classified = LibraryManagerController.classifyDropUrl(urls[index])
            var path = String(classified.path || "")
            if (!path)
                continue
            var identity = Qt.platform.os === "windows"
                         ? path.toLocaleLowerCase() : path
            if (seenPaths[identity])
                continue
            seenPaths[identity] = true
            if (classified.kind === LibraryManagerController.Directory)
                directoryPaths.push(path)
        }
        for (var pathIndex = 0; pathIndex < directoryPaths.length;
             ++pathIndex) {
            LibraryManagerController.addMonitoredFolder(
                        directoryPaths[pathIndex])
        }
        return directoryPaths.length > 0
    }
    function resourceDropContainsPoint(x, y) {
        var local = sideNavigation.mapFromItem(null, x, y)
        return sideNavigation.resourceDropContainsPoint(local.x, local.y)
    }
    function openImportDialog(playlistId) {
        if (importBatchActive || ImportController.busy || activeImportDialog)
            return false
        importTargetPlaylistId = arguments.length > 0
                ? (playlistId || "") : customCategory()
        if (importTargetPlaylistId)
            enterCategory(importTargetPlaylistId, "playlist")
        var dialog = importDialogComponent.createObject(listWindow)
        activeImportDialog = dialog
        if (!dialog) {
            importTargetPlaylistId = ""
            return false
        }
        dialog.open()
        return true
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
            id: dialog
            objectName: "importAudioDialog"
            fileMode: FileDialog.OpenFiles
            nameFilters: [LibraryManagerController.audioFileNameFilter]
            onAccepted: {
                var started = listWindow.importSelectedUrls(selectedFiles)
                if (!started && !listWindow.importBatchActive)
                    listWindow.importTargetPlaylistId = ""
                if (listWindow.activeImportDialog === dialog)
                    listWindow.activeImportDialog = null
                Qt.callLater(destroy)
            }
            onRejected: {
                if (listWindow.activeImportDialog === dialog) {
                    if (!listWindow.importBatchActive)
                        listWindow.importTargetPlaylistId = ""
                    listWindow.activeImportDialog = null
                }
                Qt.callLater(destroy)
            }
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
                listWindow.enterCategory(playlistId, "playlist")
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
        objectName: "createPlaylistDialog"
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
            if (id) listWindow.enterCategory(id, "playlist")
        }
        contentItem: TextField {
            id: createPlaylistField
            objectName: "createPlaylistField"
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
        objectName: "renamePlaylistDialog"
        property string playlistId
        title: qsTr("重命名歌单")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: playlistModel.renamePlaylist(
                        playlistId, renamePlaylistField.text)
        contentItem: TextField {
            id: renamePlaylistField
            objectName: "renamePlaylistField"
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }
    Dialog {
        id: removePlaylistDialog
        objectName: "removePlaylistDialog"
        property string playlistId
        title: qsTr("删除歌单")
        modal: true
        width: 420
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            if (playlistModel.removePlaylist(playlistId) && filterModel
                    && filterModel.category === playlistId)
                listWindow.enterCategory("all", "library")
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
                Layout.preferredHeight: listWindow.titleBarHeight
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
                            color: parent.hovered ? Theme.danger
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
                readonly property int leftColumnWidth: 208
                readonly property int rightColumnWidth: 248
                readonly property int dividerWidth: 1
                readonly property real centerWidth: centerColumn.width
                color: Theme.listWorkspaceSurface
                border.width: 0
                radius: 0
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
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
                        onResourceUrlsDropped: function(urls) {
                            resourceDropAccepted =
                                    listWindow.handleResourceDropUrls(urls)
                        }
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
                        onImportRequested: function(playlistId) {
                            listWindow.openImportDialog(playlistId)
                        }
                        onResourceFolderRemoved: function(folder) {
                            listWindow.handleResourceFolderRemoved(folder)
                        }
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
                        spacing: 0

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: filterModel && filterModel.category === "library" ? 3
                                          : LibraryModel.count === 0
                                            || listWindow.customPlaylistEmpty() ? 1
                                          : filterModel && filterModel.count > 0
                                            ? 0 : 2
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                TrackList {
                                    id: sharedTrackList
                                    objectName: "sharedTrackList"
                                    anchors.fill: parent
                                    trackModel: filterModel
                                    playlistModel: listWindow.playlistModel
                                    selectedCategory: filterModel
                                                      ? filterModel.category : "all"
                                    tagFilterActive: filterModel
                                                     && filterModel.tagKey !== ""
                                    searchText: filterModel
                                                ? filterModel.searchText : ""
                                }
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

                        Item {
                            id: centerTrackFooter
                            objectName: "centerTrackFooter"
                            Layout.fillWidth: true
                            Layout.minimumWidth: centerColumn.width
                            Layout.maximumWidth: centerColumn.width
                            Layout.preferredHeight: listWindow.filterBarHeight
                            visible: !filterModel || filterModel.category !== "library"

                            SearchFilter {
                                id: searchFilter
                                objectName: "librarySearchFilter"
                                anchors.fill: parent
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
                    }

                    Rectangle {
                        objectName: "tagPanelDivider"
                        Layout.preferredWidth: listWorkspace.dividerWidth
                        Layout.fillHeight: true
                        visible: listWindow.tagManagementMode
                        color: Theme.listDivider
                    }

                    TagManagementPanel {
                        objectName: "tagManagementPanel"
                        Layout.preferredWidth: listWorkspace.rightColumnWidth
                        Layout.minimumWidth: listWorkspace.rightColumnWidth
                        Layout.maximumWidth: listWorkspace.rightColumnWidth
                        Layout.fillHeight: true
                        visible: listWindow.tagManagementMode
                        tagModel: TagModel
                        filterModel: listWindow.filterModel
                    }
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
        urlsSubmitter: function(urls) {
            return listWindow.handleListDropUrls(urls)
        }
    }

    WindowResizeHandles {
        objectName: "listResizeHandles"
        targetWindow: listWindow
    }

}
