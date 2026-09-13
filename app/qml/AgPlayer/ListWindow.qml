import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Window {
    id: listWindow
    objectName: "listWindow"
    visible: false
    readonly property int titleBarHeight: Theme.titleBarHeight
    readonly property int trackHeaderHeight: Theme.tableHeaderHeight
    readonly property int defaultVisibleTrackCount: 10
    readonly property int filterBarHeight: 36
    readonly property int defaultTrackRowHeight:
        SettingsController.listWaveformThumbnailEnabled
        ? Theme.mediaListRowHeight : Theme.listRowHeight
    readonly property int defaultListHeight:
        titleBarHeight + trackHeaderHeight
        + defaultVisibleTrackCount * defaultTrackRowHeight + filterBarHeight
    width: 960
    height: defaultListHeight
    readonly property int pageMinimumWidth: 956
    // Cocoa enforces minimumWidth even on controller-driven resize().
    minimumWidth: Qt.platform.os === "osx" && !WindowController.listWindowDetached
                  ? Math.min(pageMinimumWidth, WindowController.mainWindowGeometry.width)
                  : pageMinimumWidth
    minimumHeight: 320
    flags: Qt.FramelessWindowHint
    color: "transparent"
    title: qsTr("AgPlayer 音乐列表")
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

    property var windows: WindowController
    property var filterModel: null
    property alias tagSearchText: tagManagementPanel.searchText
    property var playlistModel: PlaylistModel
    property string importTargetPlaylistId: ""
    property var activeImportDialog: null
    property bool importBatchActive: false
    property string exportPlaylistId: ""
    property string resourceDropStatus: "idle"
    onResourceDropStatusChanged: {
        if (resourceDropStatus === "completed") resourceDropDismiss.restart()
        else resourceDropDismiss.stop()
    }
    Timer {
        id: resourceDropDismiss
        interval: 5000
        repeat: false
        onTriggered: {
            if (listWindow.resourceDropStatus === "completed")
                listWindow.resourceDropStatus = "idle"
        }
    }
    readonly property bool resourceDropActive:
        resourceDropStatus === "pending"
        || resourceDropStatus === "waiting"
        || resourceDropStatus === "scanned"
        || resourceDropStatus === "importing"
    readonly property string resourceDropStatusText:
        resourceDropStatus === "completed" ? qsTr("资源文件夹刷新完成")
        : resourceDropStatus === "failed" ? qsTr("资源文件夹刷新失败")
        : resourceDropStatus === "importing" ? qsTr("正在导入资源文件夹…")
        : resourceDropActive ? qsTr("正在刷新资源文件夹…")
        : ""
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
        if (!ResourceFolderController.pathIsWithin(
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
            listWindow.normalizeLegacyCategory()
        }
    }

    function normalizeLegacyCategory() {
        if (filterModel && filterModel.category === "library")
            filterModel.category = "all"
    }
    onFilterModelChanged: normalizeLegacyCategory()
    Component.onCompleted: normalizeLegacyCategory()
    Connections {
        target: ImportController
        function onBusyChanged() {
            if (listWindow.resourceDropStatus === "scanned"
                    && ImportController.busy)
                listWindow.resourceDropStatus = "importing"
        }
        function onFinished() {
            if (listWindow.resourceDropStatus === "waiting") {
                listWindow.resourceDropStatus = "pending"
                ResourceFolderController.rescan()
            } else if (listWindow.resourceDropStatus === "importing") {
                listWindow.finishResourceDrop()
            }
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
            var classified = ResourceFolderController.classifyDropUrl(urls[index])
            if (classified.kind === ResourceFolderController.Directory
                    || classified.kind === ResourceFolderController.AudioFile)
                accepted.push(classified.url)
        }
        return accepted.length > 0 && beginImport(accepted)
    }
    function handleResourceDropUrls(urls) {
        if (!urls || urls.length === 0)
            return false
        var seenPaths = ({})
        var audioUrls = []
        var directoryPaths = []
        for (var index = 0; index < urls.length; ++index) {
            var classified = ResourceFolderController.classifyDropUrl(urls[index])
            var path = String(classified.path || "")
            if (!path)
                continue
            var identity = Qt.platform.os === "windows"
                         ? path.toLocaleLowerCase() : path
            if (seenPaths[identity])
                continue
            seenPaths[identity] = true
            if (classified.kind === ResourceFolderController.Directory) {
                directoryPaths.push(path)
            } else if (classified.kind === ResourceFolderController.AudioFile) {
                audioUrls.push(classified.url)
            }
        }
        var audioImportStarted = audioUrls.length > 0
                && beginImport(audioUrls)
        var registeredCount = 0
        for (var pathIndex = 0; pathIndex < directoryPaths.length;
             ++pathIndex) {
            if (ResourceFolderController.addMonitoredFolder(
                        directoryPaths[pathIndex]))
                ++registeredCount
        }
        if (registeredCount <= 0)
            return audioImportStarted
        // A synchronous true means accepted/pending only.  Completion is
        // reported after the shared scanner and importer signals finish.
        resourceDropStatus = "pending"
        return true
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
            nameFilters: [ResourceFolderController.audioFileNameFilter]
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

    ThemedDialog {
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

    ThemedDialog {
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
            if (id) {
                sideNavigation.navigationModel.setExpanded("library:all", true)
                listWindow.enterCategory(id, "playlist")
                Qt.callLater(function() {
                    sideNavigation.revealNode("playlist:" + id)
                })
            }
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
    ThemedDialog {
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
    ThemedDialog {
        id: removePlaylistDialog
        objectName: "removePlaylistDialog"
        property string playlistId
        title: qsTr("删除歌单")
        modal: true
        width: 360
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            if (playlistModel.removePlaylist(playlistId) && filterModel
                    && filterModel.category === playlistId)
                listWindow.enterCategory("all", "library")
        }
        contentItem: Label {
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
                    ThemedMacWindowControls {
                        id: macListControls
                        targetWindow: listWindow
                        allowFullScreen: false
                        onCloseRequested: windows.hideListWindow()
                    }
                    Label {
                        objectName: "resourceDropStatusLabel"
                        visible: listWindow.resourceDropStatus !== "idle"
                        text: listWindow.resourceDropStatusText
                        color: listWindow.resourceDropStatus === "failed"
                               ? Theme.danger
                               : listWindow.resourceDropStatus === "completed"
                                 ? Theme.success : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        elide: Text.ElideRight
                        Layout.maximumWidth: 240
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        objectName: "listWindowMinimizeButton"
                        visible: Qt.platform.os !== "osx"
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.secondaryText
                        icon.width: 16
                        icon.height: 16
                        onClicked: listWindow.showMinimized()
                        background: null
                    }
                    ToolButton {
                        objectName: "listWindowCloseButton"
                        visible: Qt.platform.os !== "osx"
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
                    anchors.leftMargin: Qt.platform.os === "osx" ? 14 + macListControls.width : 0
                    anchors.rightMargin: Qt.platform.os === "osx" ? 0 : 84
                    acceptedButtons: Qt.LeftButton
                    onPressed: listWindow.startSystemMove()
                }
            }

            Rectangle {
                id: listWorkspace
                objectName: "listWorkspace"
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property int leftColumnWidth: Theme.navigationWidth
                readonly property int rightColumnWidth: Theme.navigationWidthExpanded
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
                        objectName: "leftWorkspaceDivider"
                        Layout.preferredWidth: listWorkspace.dividerWidth
                        Layout.fillHeight: true
                        color: Theme.listDivider
                        opacity: 0.3
                    }

                    ColumnLayout {
                        id: centerColumn
                        objectName: "centerTrackColumn"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 0

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: LibraryModel.count === 0
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
                                    activeTagKey: filterModel
                                                  ? filterModel.tagKey : ""
                                    searchText: filterModel
                                                ? filterModel.searchText : ""
                                    layoutProfile: "classic"
                                    tagManagementLayout: listWindow.tagManagementMode
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
                                        font.pixelSize: Theme.fontSizeSection
                                    }
                                    Button {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: qsTr("一键清空筛选")
                                        onClicked: searchFilter.clearFilters()
                                    }
                                }
                            }
                        }

                        Item {
                            id: centerTrackFooter
                            objectName: "centerTrackFooter"
                            Layout.fillWidth: true
                            Layout.preferredHeight: listWindow.filterBarHeight

                            SearchFilter {
                                id: searchFilter
                                objectName: "librarySearchFilter"
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: parent.width
                                       + (listWindow.tagManagementMode
                                          ? listWorkspace.dividerWidth
                                            + listWorkspace.rightColumnWidth : 0)
                                z: 5
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
                        Layout.bottomMargin: listWindow.filterBarHeight
                        visible: listWindow.tagManagementMode
                        color: Theme.listDivider
                        opacity: 0.3
                    }

                    TagManagementPanel {
                        id: tagManagementPanel
                        objectName: "tagManagementPanel"
                        Layout.preferredWidth: listWorkspace.rightColumnWidth
                        Layout.minimumWidth: listWorkspace.rightColumnWidth
                        Layout.maximumWidth: listWorkspace.rightColumnWidth
                        Layout.fillHeight: true
                        Layout.bottomMargin: listWindow.filterBarHeight
                        visible: listWindow.tagManagementMode
                        tagModel: TagModel
                        filterModel: listWindow.filterModel
                    }
                    }

                }

                Rectangle {
                    objectName: "tagPanelBottomDivider"
                    visible: listWindow.tagManagementMode
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: listWindow.filterBarHeight
                    width: listWorkspace.rightColumnWidth
                    height: 1
                    color: Theme.listDivider
                    opacity: 0.3
                    z: 4
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

    function boundedLyricsWindowX(hostLeft, hostRight, lyricsWidth,
                                  screenLeft, screenRight) {
        var spacing = Theme.spacingSm
        var rightCandidate = hostRight + spacing
        if (rightCandidate + lyricsWidth <= screenRight)
            return rightCandidate
        var leftCandidate = hostLeft - lyricsWidth - spacing
        if (leftCandidate >= screenLeft)
            return leftCandidate
        return Math.max(screenLeft,
                        Math.min(rightCandidate, screenRight - lyricsWidth))
    }

    function boundedLyricsWindowY(hostTop, lyricsHeight,
                                  screenTop, screenBottom) {
        return Math.max(screenTop,
                        Math.min(hostTop, screenBottom - lyricsHeight))
    }

    function lyricsDockEdgeForGeometry(windowX, windowY, windowWidth,
                                       windowHeight, hostGeometry,
                                       snapDistance) {
        var spacing = Theme.spacingSm
        var horizontalOverlap = Math.max(0, Math.min(
            windowX + windowWidth, hostGeometry.x + hostGeometry.width)
            - Math.max(windowX, hostGeometry.x))
        var verticalOverlap = Math.max(0, Math.min(
            windowY + windowHeight, hostGeometry.y + hostGeometry.height)
            - Math.max(windowY, hostGeometry.y))
        var horizontalProjection = horizontalOverlap > 0
                || Math.abs((windowX + windowWidth / 2)
                            - (hostGeometry.x + hostGeometry.width / 2))
                   <= hostGeometry.width / 2 + snapDistance
        var verticalProjection = verticalOverlap > 0
                || Math.abs((windowY + windowHeight / 2)
                            - (hostGeometry.y + hostGeometry.height / 2))
                   <= hostGeometry.height / 2 + snapDistance
        var distances = [
            { "edge": "left",
              "distance": Math.abs(windowX + windowWidth
                                   - (hostGeometry.x - spacing)),
              "eligible": verticalProjection },
            { "edge": "right",
              "distance": Math.abs(windowX
                                   - (hostGeometry.x
                                      + hostGeometry.width + spacing)),
              "eligible": verticalProjection },
            { "edge": "top",
              "distance": Math.abs(windowY + windowHeight
                                   - (hostGeometry.y - spacing)),
              "eligible": horizontalProjection },
            { "edge": "bottom",
              "distance": Math.abs(windowY
                                   - (hostGeometry.y
                                      + hostGeometry.height + spacing)),
              "eligible": horizontalProjection }
        ]
        distances = distances.filter(function(candidate) {
            return candidate.eligible
        })
        if (distances.length === 0)
            return "none"
        distances.sort(function(first, second) {
            return first.distance - second.distance
        })
        return distances[0].distance <= snapDistance
                ? distances[0].edge : "none"
    }

    Window {
        id: listLyricsWindow
        objectName: "listLyricsWindow"
        transientParent: listWindow
        flags: Qt.Tool | Qt.FramelessWindowHint
        color: "transparent"
        title: qsTr("AgPlayer · 歌词")
        visible: listWindow.visible
                 && PlayerExperienceController.lyricsVisible
                 && PlayerExperienceController.immersiveMode
                    === PlayerExperienceController.Off
        readonly property var hostScreen: listWindow.screen
        readonly property rect screenWorkArea:
            hostScreen
            ? WindowController.availableGeometryForWindow(listWindow)
            : Qt.rect(0, 0, 0, 0)
        readonly property rect availableGeometry:
            screenWorkArea.width > 0 && screenWorkArea.height > 0
            ? screenWorkArea
            : Qt.rect(0, 0, listWindow.width,
                      listWindow.y + listWindow.height
                      + height + Theme.spacingSm)
        readonly property rect combinedHostGeometry: {
            var main = listWindow.windows
                    ? listWindow.windows.mainWindowGeometry : Qt.rect(0, 0, 0, 0)
            if (listWindow.windows.listWindowDetached
                    || !main || main.width <= 0 || main.height <= 0) {
                return Qt.rect(listWindow.x, listWindow.y,
                               listWindow.width, listWindow.height)
            }
            var left = Math.min(main.x, listWindow.x)
            var top = Math.min(main.y, listWindow.y)
            var right = Math.max(main.x + main.width,
                                 listWindow.x + listWindow.width)
            var bottom = Math.max(main.y + main.height,
                                  listWindow.y + listWindow.height)
            return Qt.rect(left, top, right - left, bottom - top)
        }
        property string dockEdge: "right"
        property bool docked: true
        property bool draggingWindow: false
        property real dragStartX: 0
        property real dragStartY: 0
        property real sideWidth: Math.min(420, availableGeometry.width,
                                          Math.max(280, Math.round(
                                              combinedHostGeometry.width * 0.32)))
        property real horizontalHeight: Math.min(300,
                                                  Math.max(220,
                                                      combinedHostGeometry.height * 0.32))
        readonly property real snapDistance: 120
        property string dragSnapPreviewEdge: "none"

        width: sideWidth
        height: Math.min(availableGeometry.height,
                         Math.max(200, combinedHostGeometry.height))
        x: listWindow.boundedLyricsWindowX(
               combinedHostGeometry.x,
               combinedHostGeometry.x + combinedHostGeometry.width,
               width, availableGeometry.x,
               availableGeometry.x + availableGeometry.width)
        y: listWindow.boundedLyricsWindowY(
               combinedHostGeometry.y, height,
               availableGeometry.y,
               availableGeometry.y + availableGeometry.height)

        function syncDockGeometry() {
            if (!docked || draggingWindow || !visible)
                return
            var host = combinedHostGeometry
            var area = availableGeometry
            var spacing = Theme.spacingSm
            if (dockEdge === "left" || dockEdge === "right") {
                width = Math.min(sideWidth, area.width)
                height = Math.min(area.height, Math.max(200, host.height))
                x = dockEdge === "left"
                        ? host.x - width - spacing
                        : host.x + host.width + spacing
                y = listWindow.boundedLyricsWindowY(
                            host.y, height, area.y, area.y + area.height)
            } else {
                width = Math.min(area.width, Math.max(280, host.width))
                height = Math.min(horizontalHeight, area.height)
                x = Math.max(area.x,
                             Math.min(host.x, area.x + area.width - width))
                y = dockEdge === "top"
                        ? host.y - height - spacing
                        : host.y + host.height + spacing
            }
            x = Math.max(area.x, Math.min(x, area.x + area.width - width))
            y = Math.max(area.y, Math.min(y, area.y + area.height - height))
        }

        function finishWindowDrag() {
            draggingWindow = false
            var edge = listWindow.lyricsDockEdgeForGeometry(
                        x, y, width, height, combinedHostGeometry,
                        snapDistance)
            if (edge === "none") {
                dragSnapPreviewEdge = "none"
                docked = false
                x = Math.max(availableGeometry.x,
                             Math.min(x, availableGeometry.x
                                      + availableGeometry.width - width))
                y = Math.max(availableGeometry.y,
                             Math.min(y, availableGeometry.y
                                      + availableGeometry.height - height))
                return
            }
            dockEdge = edge
            docked = true
            dragSnapPreviewEdge = "none"
            syncDockGeometry()
        }

        onVisibleChanged: if (visible) Qt.callLater(syncDockGeometry)

        Connections {
            target: listWindow
            function onXChanged() { listLyricsWindow.syncDockGeometry() }
            function onYChanged() { listLyricsWindow.syncDockGeometry() }
            function onWidthChanged() { listLyricsWindow.syncDockGeometry() }
            function onHeightChanged() { listLyricsWindow.syncDockGeometry() }
        }

        Item {
            objectName: "lyricsWindowMoveRegion"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 38
            height: 30
            z: 20

            DragHandler {
                id: lyricsWindowDrag
                objectName: "lyricsWindowDragHandler"
                target: null
                acceptedButtons: Qt.LeftButton
                onActiveChanged: {
                    if (active) {
                        listLyricsWindow.dragStartX = listLyricsWindow.x
                        listLyricsWindow.dragStartY = listLyricsWindow.y
                        listLyricsWindow.draggingWindow = true
                        listLyricsWindow.docked = false
                    } else if (listLyricsWindow.draggingWindow) {
                        listLyricsWindow.finishWindowDrag()
                    }
                }
                onTranslationChanged: {
                    if (!active)
                        return
                    listLyricsWindow.x = listLyricsWindow.dragStartX
                            + translation.x
                    listLyricsWindow.y = listLyricsWindow.dragStartY
                            + translation.y
                    listLyricsWindow.dragSnapPreviewEdge =
                            listWindow.lyricsDockEdgeForGeometry(
                                listLyricsWindow.x, listLyricsWindow.y,
                                listLyricsWindow.width, listLyricsWindow.height,
                                listLyricsWindow.combinedHostGeometry,
                                listLyricsWindow.snapDistance)
                }
            }
        }

        Rectangle {
            objectName: "lyricsSnapPreview"
            anchors.fill: parent
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
            radius: Theme.radiusMd
            visible: listLyricsWindow.draggingWindow
                     && listLyricsWindow.dragSnapPreviewEdge !== "none"
            opacity: 0.72
            z: 19
        }

        LyricsPanel {
            id: listLyricsPanel
            objectName: "listLyricsPanel"
            anchors.fill: parent
            service: LyricsService
            spatialMode: false
            onCloseRequested: PlayerExperienceController.lyricsVisible = false
        }
    }
    Connections {
        target: ResourceFolderController
        function onScanFinished() {
            if (!listWindow.resourceDropActive)
                return
            if (ImportController.busy) {
                listWindow.resourceDropStatus = "waiting"
                return
            }
            listWindow.resourceDropStatus = "scanned"
            Qt.callLater(function() {
                if (listWindow.resourceDropStatus !== "scanned")
                    return
                if (ImportController.busy)
                    listWindow.resourceDropStatus = "importing"
                else
                    listWindow.finishResourceDrop()
            })
        }
    }

    function finishResourceDrop() {
        resourceDropStatus = resourceDropStatus === "importing"
                && ImportController.errors.length > 0
                ? "failed" : "completed"
    }

}
