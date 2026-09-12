import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

ListView {
    id: root
    readonly property Item popupOverlay: Overlay.overlay
    objectName: "trackList"
    clip: true
    focus: true
    boundsBehavior: Flickable.StopAtBounds
    headerPositioning: ListView.OverlayHeader
    reuseItems: true
    cacheBuffer: 0

    property var trackModel: LibraryModel
    property var playlistModel: PlaylistModel
    property var thumbnailProvider: TrackWaveformThumbnailProvider
    property string selectedCategory: "all"
    property string searchText: ""
    property bool tagFilterActive: false
    property string activeTagKey: ""
    // The data model and delegate stay shared; shells select only presentation.
    property string layoutProfile: "classic"
    readonly property bool relaxedClassicColumns: layoutProfile === "classic"
    readonly property bool singleWindowLayout:
        layoutProfile === "integrated" || layoutProfile === "rolling"
    readonly property bool integratedCompact: singleWindowLayout
    property bool tagManagementLayout: false
    property bool thumbnailVisibilityFollowsSetting: true
    property var selectedTrackIds: []
    property int selectionAnchor: -1
    property var lastTrashResult: ({ successCount: 0, failureCount: 0, failures: [] })
    readonly property bool windowActive: root.Window.active
    readonly property bool showArtistColumn:
        !relaxedClassicColumns && !singleWindowLayout
    readonly property bool showAlbumColumn:
        !relaxedClassicColumns && !singleWindowLayout
    // Named production profiles never change trailing metadata geometry when
    // the window grows.  The legacy detailed fixture retains its historical
    // narrow-width behavior for non-shell consumers.
    readonly property bool compactColumns:
        layoutProfile === "detailed" ? width < 900 : true
    readonly property int favoriteAlbumGap: 6
    readonly property int artistAlbumGap: 6
    readonly property int albumRatingGap: 6
    readonly property int sequenceWidth: compactColumns ? 34 : 42
    readonly property int favoriteWidth: compactColumns ? 46 : 52
    readonly property int albumWidth: compactColumns ? 78 : 112
    readonly property int artistWidth: compactColumns ? 80 : 112
    readonly property int ratingWidth: Math.max(
                                           ratingIconSize * 5,
                                           singleWindowLayout ? 84
                                           : tagFilterActive
                                           ? (compactColumns ? 64 : 86)
                                           : (compactColumns ? 82 : 110))
    readonly property int bpmWidth: compactColumns ? 48 : 64
    readonly property int durationWidth: compactColumns ? 58 : 72
    readonly property int trailingColumnGap: singleWindowLayout ? 6 : 0
    // The shared integrated/rolling row keeps equal breathing room around the
    // flexible waveform while leaving the fixed metadata columns compact.
    readonly property int singleWindowWaveformGap: 6
    readonly property int titleMinimumWidth:
        singleWindowLayout ? 180 : (compactColumns ? 150 : 180)
    readonly property int singleWindowFlexibleWidth: Math.max(
        titleMinimumWidth + 120,
        width - 32 - sequenceWidth - durationWidth - ratingWidth
        - favoriteWidth - trailingColumnGap * 3 - 48)
    // Both the title block and waveform grow with the window. A proportional
    // share avoids the old 320-DIP title ceiling while preserving a useful
    // waveform region at every supported width.
    readonly property int singleWindowTitleWidth: Math.max(
        titleMinimumWidth, Math.floor(singleWindowFlexibleWidth * 0.56))
    readonly property int singleWindowMediaHeight: 34
    readonly property int singleWindowWaveformHeight: 26
    readonly property int singleWindowSubtitleFontSize:
        Math.max(10, Theme.fontSizeCaption - 1)
    readonly property bool showBpmColumn:
        singleWindowLayout ? false
        : (relaxedClassicColumns ? !tagManagementLayout : !tagFilterActive)
    readonly property bool showDurationColumn:
        singleWindowLayout || relaxedClassicColumns || !tagFilterActive
    readonly property int ratingIconSize:
        relaxedClassicColumns ? 15 : (compactColumns ? 15 : 17)
    readonly property int favoriteIconSize: relaxedClassicColumns ? 20 : 18
    readonly property int headerHeight: Theme.tableHeaderHeight
    readonly property int headerFontWeight: Font.DemiBold
    readonly property bool waveformThumbnailsVisible:
        !thumbnailVisibilityFollowsSetting
        || SettingsController.listWaveformThumbnailEnabled
    readonly property int rowHeight:
        singleWindowLayout ? Theme.mediaListRowHeight
                           : (waveformThumbnailsVisible
                              ? Theme.mediaListRowHeight : Theme.listRowHeight)
    property int thumbnailItemCount: 0
    property int nextWaveformGeneration: 0
    property int dragPreviewCreationCount: 0
    property int lastTrackDragDropAction: Qt.IgnoreAction
    property var dragTrackIds: []
    property var activeDragProxy: null
    property bool dragSessionActive: false
    property string draggedTrackId: ""
    property real dragOriginY: 0
    property real draggedRowHeight: 0
    property string dragPreviewTitle: ""
    property url dragPreviewCover: ""
    readonly property bool thumbnailWindowVisible:
        !root.Window.window || root.Window.window.visible
    readonly property bool thumbnailHostVisible:
        root.visible && root.width > 0 && root.height > 0
        && root.thumbnailWindowVisible
    model: trackModel

    FontMetrics {
        id: trackMenuFontMetrics
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
    }

    function fittedMenuWidth(labels, submenuLabels) {
        var required = 120
        for (var index = 0; index < labels.length; ++index)
            required = Math.max(required,
                                trackMenuFontMetrics.advanceWidth(labels[index]) + 24)
        for (var submenuIndex = 0; submenuIndex < submenuLabels.length;
             ++submenuIndex) {
            required = Math.max(
                        required,
                        trackMenuFontMetrics.advanceWidth(
                            submenuLabels[submenuIndex]) + 42)
        }
        return Math.ceil(required)
    }

    // Category navigation is user-directed.  Start each category at its top;
    // only an actual playback-track change may scroll back to the playing row.
    onSelectedCategoryChanged: Qt.callLater(root.positionViewAtBeginning)

    readonly property bool customPlaylistSelected:
        selectedCategory !== "all" && selectedCategory !== "favorites"
        && selectedCategory !== "history" && selectedCategory !== "recentAdded"
        && selectedCategory !== "neverPlayed"
    readonly property bool canRemoveFromCurrentView:
        tagFilterActive ? activeTagKey.length > 0
                        : selectedCategory === "all"
                          || selectedCategory === "favorites"
                          || selectedCategory === "history"
                          || customPlaylistSelected

    LibraryFileOperations { id: fileOps; libraryModel: LibraryModel }

    function formatTime(ms) {
        if (ms <= 0) return "--:--"
        var total = Math.floor(ms / 1000)
        var minutes = Math.floor(total / 60)
        return (minutes < 10 ? "0" : "") + minutes + ":"
                + (total % 60 < 10 ? "0" : "") + total % 60
    }
    function formatBpm(value) {
        var bpm = Number(value)
        if (!isFinite(bpm) || bpm <= 0) return "—"
        var rounded = Math.round(bpm * 10) / 10
        return Math.abs(rounded - Math.round(rounded)) < 0.001
                ? Math.round(rounded).toString() : rounded.toFixed(1)
    }
    function isCurrentTrack(trackId) { return PlaybackController.currentTrackId === trackId }
    function isSelected(trackId) { return selectedTrackIds.indexOf(trackId) >= 0 }
    function trackIdAt(row) {
        if (row < 0 || row >= count) return ""
        return trackModel.data(trackModel.index(row, 0), LibraryModel.TrackIdRole)
    }
    function visibleTrackIds() {
        var ids = []
        for (var row = 0; row < count; ++row) {
            var id = trackIdAt(row)
            if (id) ids.push(id)
        }
        return ids
    }
    function ensureCurrentTrackVisible() {
        var currentTrackId = PlaybackController.currentTrackId
        if (!currentTrackId || count <= 0) return
        for (var row = 0; row < count; ++row) {
            if (trackIdAt(row) === currentTrackId) {
                positionViewAtIndex(row, ListView.Contain)
                return
            }
        }
    }
    function selectOnly(trackId, row) { selectedTrackIds = trackId ? [trackId] : []; selectionAnchor = row }
    function locateTrack(trackId) {
        for (var row = 0; row < count; ++row) {
            if (trackIdAt(row) === trackId) {
                selectOnly(trackId, row)
                positionViewAtIndex(row, ListView.Contain)
                forceActiveFocus()
                return true
            }
        }
        return false
    }
    function updateSelection(trackId, row, modifiers) {
        forceActiveFocus()
        if ((modifiers & Qt.ShiftModifier) && selectionAnchor >= 0) {
            var range = []
            for (var index = Math.min(selectionAnchor, row);
                 index <= Math.max(selectionAnchor, row); ++index) {
                var id = trackIdAt(index)
                if (id) range.push(id)
            }
            selectedTrackIds = range
        } else if (modifiers & Qt.ControlModifier) {
            var toggled = selectedTrackIds.slice()
            var position = toggled.indexOf(trackId)
            if (position >= 0) toggled.splice(position, 1); else toggled.push(trackId)
            selectedTrackIds = toggled
            selectionAnchor = row
        } else selectOnly(trackId, row)
    }
    function selectAllVisible() {
        var all = []
        for (var row = 0; row < count; ++row) {
            var id = trackIdAt(row); if (id) all.push(id)
        }
        selectedTrackIds = all
        selectionAnchor = count > 0 ? 0 : -1
    }
    function finishRowDrag(trackId, originY, deltaY, rowHeight) {
        var targetIndex = indexAt(sequenceWidth,
                                  originY + deltaY + rowHeight / 2)
        var targetId = trackIdAt(targetIndex)
        var ids = isSelected(trackId) ? selectedTrackIds.slice() : [trackId]
        if (!targetId || ids.indexOf(targetId) >= 0) return
        if (customPlaylistSelected)
            playlistModel.reorderTracks(selectedCategory, ids, targetId)
        else if (selectedCategory === "all")
            LibraryModel.reorderTracks(ids, targetId)
    }
    function beginTrackDrag(trackId, ids, title, coverSource, proxy,
                            originY, rowHeight) {
        if (dragSessionActive)
            cancelTrackDrag()
        lastTrackDragDropAction = Qt.IgnoreAction
        dragTrackIds = ids.slice()
        dragPreviewTitle = title || qsTr("未知歌曲")
        dragPreviewCover = coverSource
        draggedTrackId = trackId
        dragOriginY = originY
        draggedRowHeight = rowHeight
        activeDragProxy = proxy
        dragSessionActive = true
        dragPreviewLoader.active = true
    }
    function dragRowAtViewportPoint(point) {
        var contentPoint = root.mapToItem(root.contentItem,
                                          point.x, point.y)
        var rowIndex = root.indexAt(contentPoint.x, contentPoint.y)
        var row = rowIndex >= 0 ? root.itemAtIndex(rowIndex) : null
        if (!row || !row.dragAreaItem)
            return null
        var local = root.contentItem.mapToItem(row.dragAreaItem,
                                               contentPoint.x,
                                               contentPoint.y)
        return local.x >= 0 && local.x <= row.dragAreaItem.width
                ? row : null
    }
    function isLocalTrackReorderPoint(point) {
        if (!point || !isFinite(point.x) || !isFinite(point.y)
                || point.x < 0 || point.x > root.width
                || point.y < (root.headerItem ? root.headerItem.height : 0)
                || point.y > root.height)
            return false
        var contentPoint = root.mapToItem(root.contentItem,
                                          point.x, point.y)
        return root.indexAt(contentPoint.x, contentPoint.y) >= 0
    }
    function clearTrackDragSession() {
        if (!dragSessionActive && !dragPreviewLoader.active
                && !activeDragProxy && dragTrackIds.length === 0)
            return
        dragSessionActive = false
        dragPreviewLoader.active = false
        activeDragProxy = null
        dragTrackIds = []
        draggedTrackId = ""
        dragOriginY = 0
        draggedRowHeight = 0
    }
    function cancelTrackDrag() {
        var proxy = activeDragProxy
        clearTrackDragSession()
        if (proxy && proxy.Drag.active)
            proxy.Drag.cancel()
    }
    function completeTrackDrag(proxy, rowDeltaY, releasePoint) {
        if (!dragSessionActive || activeDragProxy !== proxy)
            return
        var trackId = draggedTrackId
        var originY = dragOriginY
        var deltaY = Number(rowDeltaY)
        if (!isFinite(deltaY))
            deltaY = 0
        var rowHeight = draggedRowHeight
        var canReorderLocally = isLocalTrackReorderPoint(releasePoint)
        var dropAction = proxy.Drag.drop()
        lastTrackDragDropAction = dropAction
        clearTrackDragSession()
        if (dropAction === Qt.IgnoreAction && canReorderLocally)
            finishRowDrag(trackId, originY, deltaY, rowHeight)
    }
    function removeSelectedFromCurrentView() {
        var ids = selectedTrackIds.slice()
        if (ids.length === 0 || !canRemoveFromCurrentView) return
        if (tagFilterActive) {
            LibraryModel.removeTagFromTracks(ids, activeTagKey)
        } else if (customPlaylistSelected) {
            playlistModel.removeTracks(selectedCategory, ids)
        } else if (selectedCategory === "favorites") {
            for (var favoriteIndex = 0; favoriteIndex < ids.length; ++favoriteIndex) {
                var favoriteRow = LibraryModel.indexForTrackId(ids[favoriteIndex])
                if (favoriteRow >= 0) LibraryModel.setFavorite(favoriteRow, false)
            }
        } else if (selectedCategory === "history") {
            for (var historyIndex = 0; historyIndex < ids.length; ++historyIndex)
                LibraryModel.removeFromHistory(ids[historyIndex])
        } else if (selectedCategory === "all") {
            for (var libraryIndex = 0; libraryIndex < ids.length; ++libraryIndex)
                ResourceFolderController.removeTrackFromLibrary(ids[libraryIndex])
        }
        selectedTrackIds = []
        selectionAnchor = -1
    }
    function openTrackMenu(trackId, favorite, row) {
        if (!isSelected(trackId)) selectOnly(trackId, row)
        trackMenu.targetTrackId = trackId
        trackMenu.targetTrackIds = selectedTrackIds.slice()
        trackMenu.targetFavorite = favorite
        trackMenu.popup()
    }
    function beginRename() {
        var details = fileOps.trackDetails(trackMenu.targetTrackId)
        renameField.text = String(details.fileName || "").replace(/\.[^.]+$/, "")
        renameDialog.open(); renameField.forceActiveFocus(); renameField.selectAll()
    }
    function applyTagsToTracks(trackIds, values) {
        return LibraryModel.setTagsForTracks(trackIds, values)
    }
    function selectedFileUrls() {
        var urls = []
        for (var index = 0; index < trackMenu.targetTrackIds.length; ++index) {
            var url = fileOps.fileUrl(trackMenu.targetTrackIds[index])
            if (url && String(url).length > 0) urls.push(url)
        }
        return urls
    }
    function actionTrackIds() {
        return trackMenu.targetTrackIds.length > 0
                ? trackMenu.targetTrackIds.slice()
                : selectedTrackIds.slice()
    }
    function openInAudioTool(toolIndex) {
        var ids = actionTrackIds()
        var urls = []
        for (var index = 0; index < ids.length; ++index) {
            var url = fileOps.fileUrl(ids[index])
            if (url && String(url).length > 0)
                urls.push(url)
        }
        if (urls.length === 0) return
        // Present the destination first: a loader issue must not make a real
        // context-menu click appear to do nothing.
        AudioToolsController.selectTool(toolIndex)
        WindowController.showAudioTools()
        if (toolIndex === 0) {
            AudioEditorController.openFile(urls[0])
        } else if (toolIndex === 1) {
            if (typeof FormatConverter.loadFiles === "function") FormatConverter.loadFiles(urls)
            else FormatConverter.loadFile(urls[0])
        } else if (toolIndex === 2) {
            if (typeof MetadataEditor.loadFiles === "function") MetadataEditor.loadFiles(urls)
            else MetadataEditor.loadFile(urls[0])
        } else if (toolIndex === 5) {
            LosslessAnalysisController.loadFiles(urls)
        } else {
            if (typeof FilenameProcessor.loadFiles === "function") FilenameProcessor.loadFiles(urls)
            else FilenameProcessor.loadFile(urls[0])
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            selectAllVisible(); event.accepted = true
        } else if (event.key === Qt.Key_Delete && root.canRemoveFromCurrentView) {
            removeSelectedFromCurrentView(); event.accepted = true
        }
    }
    Shortcut { sequence: StandardKey.SelectAll; context: Qt.WindowShortcut; enabled: root.activeFocus; onActivated: root.selectAllVisible() }
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.dragSessionActive
        onActivated: root.cancelTrackDrag()
    }
    Connections {
        target: root.Window.window
        function onActiveChanged() {
            if (root.Window.window && !root.Window.window.active)
                root.cancelTrackDrag()
        }
    }

    FolderDialog {
        id: moveFolderDialog
        title: qsTr("移动到指定文件夹")
        onAccepted: fileOps.moveTracksToUrl(trackMenu.targetTrackIds, selectedFolder,
                                             LibraryFileOperations.AutoRename)
    }
    FolderDialog {
        id: copyFolderDialog
        title: qsTr("复制到指定文件夹")
        onAccepted: fileOps.copyTracksToUrl(trackMenu.targetTrackIds, selectedFolder,
                                             LibraryFileOperations.AutoRename)
    }
    FileDialog {
        id: relocateDialog
        title: qsTr("重新定位文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [ResourceFolderController.audioFileNameFilter]
        onAccepted: fileOps.relocateTrackToUrl(trackMenu.targetTrackId, selectedFile)
    }
    ThemedDialog {
        id: renameDialog
        title: qsTr("重命名")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: fileOps.renameTrack(trackMenu.targetTrackId, renameField.text)
        contentItem: TextField { id: renameField; placeholderText: qsTr("新文件名") }
        background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusMd }
    }
    ThemedDialog {
        id: tagDialog
        objectName: "trackTagDialog"
        margins: Theme.spacingLg
        parent: root.popupOverlay
        width: Math.min(296, parent && parent.width > 32 ? parent.width - 32 : 296)
        title: qsTr("自定义标签")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            var details = fileOps.trackDetails(trackMenu.targetTrackId)
            tagField.text = (details.tags || []).join(", ")
        }
        onAccepted: {
            var values = tagField.text.split(/[,，]/).map(function(value) { return value.trim() })
            root.applyTagsToTracks(trackMenu.targetTrackIds, values)
        }
        contentItem: ThemedTextField { id: tagField; placeholderText: qsTr("用逗号分隔多个标签") }
        background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusMd }
    }
    ThemedDialog {
        id: trashConfirm
        width: 360
        title: qsTr("彻底删除至回收站")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label { text: qsTr("确定把选中的音乐文件移到系统回收站？"); color: Theme.primaryText; wrapMode: Text.WordWrap }
        onAccepted: {
            root.lastTrashResult = fileOps.trashTracks(trackMenu.targetTrackIds)
            root.selectedTrackIds = []
            if (root.lastTrashResult.failureCount > 0)
                trashResultDialog.open()
        }
        background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusMd }
    }
    ThemedDialog {
        id: trashResultDialog
        width: 520
        title: qsTr("部分文件未删除")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Close
        contentItem: Label {
            width: 480
            wrapMode: Text.WordWrap
            color: Theme.primaryText
            text: qsTr("已移入回收站 %1 个，失败 %2 个。\n%3")
                .arg(root.lastTrashResult.successCount || 0)
                .arg(root.lastTrashResult.failureCount || 0)
                .arg((root.lastTrashResult.failures || []).map(function(item) {
                    return (item.path || item.trackId) + "：" + item.reason
                }).join("\n"))
        }
        background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusMd }
    }

    header: Rectangle {
        width: root.width; height: root.headerHeight; color: Theme.listHeaderSurface; z: 20
        GridLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            columns: 11
            rows: 1
            columnSpacing: 0
            HeaderText { objectName: "trackHeaderIndex"; text: "#"; Layout.column: 0; Layout.minimumWidth: root.sequenceWidth; Layout.preferredWidth: root.sequenceWidth; Layout.maximumWidth: root.sequenceWidth }
            HeaderText { objectName: "trackHeaderTitle"; text: qsTr("歌曲"); Layout.column: 1; Layout.fillWidth: !root.singleWindowLayout; Layout.minimumWidth: root.titleMinimumWidth; Layout.preferredWidth: root.singleWindowLayout ? root.singleWindowTitleWidth : -1; Layout.maximumWidth: root.singleWindowLayout ? root.singleWindowTitleWidth : Number.POSITIVE_INFINITY }
            Item { objectName: "trackHeaderWaveform"; visible: root.singleWindowLayout; Layout.column: 2; Layout.fillWidth: visible }
            HeaderText { objectName: "trackHeaderFavorite"; text: qsTr("收藏"); Layout.column: root.singleWindowLayout ? 5 : (root.relaxedClassicColumns ? 4 : 2); Layout.leftMargin: root.singleWindowLayout ? root.trailingColumnGap : 0; horizontalAlignment: Text.AlignHCenter; Layout.minimumWidth: root.favoriteWidth; Layout.preferredWidth: root.favoriteWidth; Layout.maximumWidth: root.favoriteWidth }
            Item { objectName: "trackHeaderFavoriteAlbumGap"; visible: !root.relaxedClassicColumns && !root.singleWindowLayout; Layout.column: 3; Layout.minimumWidth: visible ? root.favoriteAlbumGap : 0; Layout.preferredWidth: visible ? root.favoriteAlbumGap : 0; Layout.maximumWidth: visible ? root.favoriteAlbumGap : 0 }
            HeaderText { objectName: "trackHeaderArtist"; text: qsTr("艺术家"); visible: root.showArtistColumn; Layout.column: 4; Layout.minimumWidth: visible ? root.artistWidth : 0; Layout.preferredWidth: visible ? root.artistWidth : 0; Layout.maximumWidth: visible ? root.artistWidth : 0 }
            Item { objectName: "trackHeaderArtistAlbumGap"; visible: root.showArtistColumn; Layout.column: 5; Layout.minimumWidth: visible ? root.artistAlbumGap : 0; Layout.preferredWidth: visible ? root.artistAlbumGap : 0; Layout.maximumWidth: visible ? root.artistAlbumGap : 0 }
            HeaderText { objectName: "trackHeaderAlbum"; text: qsTr("专辑"); visible: root.showAlbumColumn; Layout.column: 6; Layout.minimumWidth: visible ? root.albumWidth : 0; Layout.preferredWidth: visible ? root.albumWidth : 0; Layout.maximumWidth: visible ? root.albumWidth : 0 }
            Item { objectName: "trackHeaderAlbumRatingGap"; visible: root.showAlbumColumn; Layout.column: 7; Layout.minimumWidth: visible ? root.albumRatingGap : 0; Layout.preferredWidth: visible ? root.albumRatingGap : 0; Layout.maximumWidth: visible ? root.albumRatingGap : 0 }
            HeaderText { objectName: "trackHeaderRating"; text: qsTr("评分"); Layout.column: root.singleWindowLayout ? 4 : (root.relaxedClassicColumns ? 3 : 8); Layout.leftMargin: root.singleWindowLayout ? root.trailingColumnGap : (root.relaxedClassicColumns ? 12 : 0); horizontalAlignment: Text.AlignHCenter; Layout.minimumWidth: root.ratingWidth; Layout.preferredWidth: root.ratingWidth; Layout.maximumWidth: root.ratingWidth }
            HeaderText { objectName: "trackHeaderBpm"; text: "BPM"; visible: root.showBpmColumn; Layout.column: root.relaxedClassicColumns ? 5 : 9; horizontalAlignment: Text.AlignHCenter; Layout.minimumWidth: visible ? root.bpmWidth : 0; Layout.preferredWidth: visible ? root.bpmWidth : 0; Layout.maximumWidth: visible ? root.bpmWidth : 0 }
            HeaderText { objectName: "trackHeaderDuration"; text: qsTr("时长"); visible: root.showDurationColumn; Layout.column: root.singleWindowLayout ? 3 : (root.relaxedClassicColumns ? 2 : 10); Layout.leftMargin: root.singleWindowLayout ? root.trailingColumnGap : 0; horizontalAlignment: root.singleWindowLayout ? Text.AlignHCenter : Text.AlignRight; Layout.minimumWidth: visible ? root.durationWidth : 0; Layout.preferredWidth: visible ? root.durationWidth : 0; Layout.maximumWidth: visible ? root.durationWidth : 0 }
        }
    }

    Loader {
        id: dragPreviewLoader
        parent: Overlay.overlay
        active: false
        onLoaded: root.dragPreviewCreationCount += 1
        z: 1000
        x: {
            if (!root.activeDragProxy)
                return 0
            root.activeDragProxy.x
            return root.activeDragProxy.mapToItem(parent, 12, 12).x
        }
        y: {
            if (!root.activeDragProxy)
                return 0
            root.activeDragProxy.y
            return root.activeDragProxy.mapToItem(parent, 12, 12).y
        }
        sourceComponent: Component {
            Rectangle {
                objectName: "trackDragPreview"
                readonly property int selectedCount: root.dragTrackIds.length
                readonly property bool windowOverlayHosted:
                    dragPreviewLoader.parent === Overlay.overlay
                width: Math.min(300, previewLayout.implicitWidth + 24)
                height: 46
                radius: Theme.radiusSm
                color: Theme.elevated
                border.color: Theme.listWorkspaceBorder
                border.width: 1
                opacity: 0.68

                RowLayout {
                    id: previewLayout
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 8
                    Image {
                        source: root.dragPreviewCover
                        sourceSize.width: 34
                        sourceSize.height: 34
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        id: previewTitle
                        objectName: "trackDragPreviewTitle"
                        text: root.dragPreviewTitle
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        elide: Text.ElideRight
                        Layout.maximumWidth: root.dragTrackIds.length > 1
                                             ? 176 : 230
                    }
                    Rectangle {
                        visible: root.dragTrackIds.length > 1
                        Layout.preferredWidth: previewCount.implicitWidth + 12
                        Layout.preferredHeight: 22
                        radius: 11
                        color: Theme.listSelectedSurface
                        border.color: Theme.accent
                        border.width: 1

                        Text {
                            id: previewCount
                            objectName: "trackDragPreviewCount"
                            anchors.centerIn: parent
                            text: qsTr("%1 首").arg(root.dragTrackIds.length)
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: root.singleWindowSubtitleFontSize
                        }
                    }
                }
            }
        }
    }

    Item {
        id: windowDragProxy
        objectName: "trackDragProxy"
        parent: Overlay.overlay
        width: 1
        height: 1
        Drag.active: false
        Drag.dragType: Drag.Internal
        Drag.supportedActions: Qt.MoveAction
        Drag.keys: ["application/x-agplayer-track-ids"]
        Drag.source: root
        Drag.hotSpot.x: 0
        Drag.hotSpot.y: 0
        Drag.mimeData: ({"application/x-agplayer-track-ids":
                         JSON.stringify(root.dragTrackIds)})
    }

    delegate: Rectangle {
        id: rowItem
        required property int index
        required property string trackId
        required property string path
        required property string title
        required property string artist
        required property string album
        required property url coverUrl
        required property bool favorite
        required property int rating
        required property double bpm
        required property double durationMs
        required property bool available
        required property string fileStatus

        objectName: root.isCurrentTrack(trackId) ? "currentTrackRow" : "trackRow"
        readonly property bool currentTrack: root.isCurrentTrack(trackId)
        readonly property bool selectedTrack: root.isSelected(trackId)
        readonly property bool systemHighlighted: currentTrack || selectedTrack
        readonly property color systemHighlightColor:
            root.windowActive
            ? (currentTrack ? Theme.currentTrackSelection
                            : Theme.selectedTrackSelection)
            : (currentTrack ? Theme.currentTrackSelectionInactive
                            : Theme.selectedTrackSelectionInactive)
        readonly property color systemHighlightText:
            currentTrack ? Theme.accentText : Theme.primaryText
        readonly property var rowTags:
            typeof model !== "undefined" && model.tags !== undefined
            ? model.tags : []
        readonly property var dragTrackIds:
            root.isSelected(trackId) ? root.selectedTrackIds.slice() : [trackId]
        property alias dragAreaItem: titleCell
        readonly property url dragCoverSource: trackCoverImage.source
        property int waveformGeneration: 0
        property bool pooled: false
        readonly property bool inViewport:
            rowItem.ListView.view === root && rowItem.visible && !pooled
            && y + height > root.contentY
                            + (root.headerItem ? root.headerItem.height : 0)
            && y < root.contentY + root.height
        width: root.width; height: root.rowHeight
        color: systemHighlighted ? systemHighlightColor
               : rowHover.hovered ? Theme.hoverSurface : "transparent"

        Component.onCompleted: waveformGeneration = ++root.nextWaveformGeneration
        ListView.onPooled: {
            pooled = true
            waveformGeneration = ++root.nextWaveformGeneration
        }
        ListView.onReused: {
            waveformGeneration = ++root.nextWaveformGeneration
            pooled = false
        }

        GridLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            columns: 11
            rows: 1
            columnSpacing: 0
            Item {
                objectName: "trackIndexCell"
                Layout.column: 0
                Layout.minimumWidth: root.sequenceWidth; Layout.preferredWidth: root.sequenceWidth; Layout.maximumWidth: root.sequenceWidth; Layout.fillHeight: true
                Text { anchors.verticalCenter: parent.verticalCenter; visible: !root.isCurrentTrack(rowItem.trackId); text: rowItem.index + 1; color: rowItem.systemHighlighted ? rowItem.systemHighlightText : Theme.secondaryText; font.pixelSize: Theme.fontSizeBody }
                Item {
                    id: playingBars
                    objectName: "playingBarsIndicator"
                    readonly property int barCount: 3
                    readonly property color barColor: Theme.accentText
                    readonly property real barGap: 2
                    readonly property bool animated:
                        visible && PlaybackController.state === PlaybackController.Playing
                    visible: root.isCurrentTrack(rowItem.trackId)
                    anchors.verticalCenter: parent.verticalCenter
                    width: 21
                    height: 21

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        spacing: playingBars.barGap

                        Repeater {
                            model: 3
                            delegate: Item {
                                id: barCell
                                required property int index
                                readonly property real naturalHeight:
                                    index === 0 ? 12 : index === 1 ? 19 : 15
                                readonly property int periodMs:
                                    index === 0 ? 260 : index === 1 ? 390 : 520
                                width: 4
                                height: 21

                                Rectangle {
                                    id: bar
                                    objectName: "playingBar" + barCell.index
                                    anchors.bottom: parent.bottom
                                    width: 4
                                    height: barCell.naturalHeight
                                    radius: 1
                                    color: playingBars.barColor
                                    transformOrigin: Item.Bottom
                                    property real level: 0.4
                                    scale: 1
                                    transform: Scale {
                                        origin.x: bar.width / 2
                                        origin.y: bar.height
                                        yScale: bar.level
                                    }
                                }

                                SequentialAnimation {
                                    running: playingBars.animated
                                    loops: Animation.Infinite
                                    onRunningChanged: {
                                        if (!running)
                                            bar.level = 0.4
                                    }
                                    NumberAnimation {
                                        target: bar; property: "level"
                                        from: 0.25; to: 0.95
                                        duration: barCell.periodMs / 2
                                        easing.type: Easing.InOutSine
                                    }
                                    NumberAnimation {
                                        target: bar; property: "level"
                                        from: 0.95; to: 0.25
                                        duration: barCell.periodMs / 2
                                        easing.type: Easing.InOutSine
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item {
                id: titleCell
                objectName: "trackRowDragArea"
                Layout.column: 1
                Layout.fillWidth: !root.singleWindowLayout
                Layout.minimumWidth: root.titleMinimumWidth
                Layout.preferredWidth: root.singleWindowLayout
                                       ? root.singleWindowTitleWidth : -1
                Layout.maximumWidth: root.singleWindowLayout
                                     ? root.singleWindowTitleWidth
                                     : Number.POSITIVE_INFINITY
                Layout.fillHeight: true
                RowLayout {
                    anchors.fill: parent
                    spacing: 4
                    Image {
                        id: trackCoverImage
                        objectName: "trackCover"
                        source: rowItem.coverUrl ? rowItem.coverUrl
                                                 : Theme.icon("music-2-fill")
                        Layout.preferredWidth: root.singleWindowMediaHeight
                        Layout.preferredHeight: root.singleWindowMediaHeight
                        sourceSize.width: root.singleWindowMediaHeight
                        sourceSize.height: root.singleWindowMediaHeight
                        fillMode: Image.PreserveAspectFit
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        MarqueeBodyText {
                            id: trackTitleMarquee
                            objectName: "trackTitleMarquee"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            y: root && root.singleWindowLayout
                               ? Math.round((parent.height - height
                                             - singleWindowTrackSubtitle.implicitHeight
                                             - 1) / 2)
                               : Math.round((parent.height - height
                                     - (root && root.waveformThumbnailsVisible
                                        ? waveformWrapperLoader.height + 3 : 0)) / 2)
                            height: implicitHeight
                            text: rowItem.title || qsTr("未知歌曲")
                            trackAvailable: rowItem.available
                            highlighted: rowItem.systemHighlighted
                            highlightText: rowItem.systemHighlightText
                            fontWeight: Font.DemiBold
                        }

                        TrackSubtitle {
                            id: singleWindowTrackSubtitle
                            objectName: "singleWindowTrackSubtitle"
                            visible: root.singleWindowLayout
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: trackTitleMarquee.bottom
                            anchors.topMargin: 1
                            artist: rowItem.artist
                            album: rowItem.album
                            tags: rowItem.rowTags
                            color: rowItem.currentTrack
                                   ? rowItem.systemHighlightText
                                   : Theme.secondaryText
                            font.pixelSize: root.singleWindowSubtitleFontSize
                        }

                        Loader {
                            id: waveformWrapperLoader
                            objectName: "trackWaveformThumbnailLoader"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: trackTitleMarquee.bottom
                            anchors.topMargin: 3
                            height: 16
                            visible: !root.singleWindowLayout
                            active: root && root.waveformThumbnailsVisible
                                    && !root.singleWindowLayout
                                    && root.thumbnailHostVisible
                                    && rowItem.inViewport
                            property bool counted: false
                            onLoaded: {
                                if (!counted) {
                                    counted = true
                                    if (root)
                                        root.thumbnailItemCount += 1
                                }
                            }
                            onItemChanged: {
                                if (!item && counted) {
                                    counted = false
                                    if (root)
                                        root.thumbnailItemCount -= 1
                                }
                            }
                            Component.onDestruction: {
                                if (counted && root)
                                    root.thumbnailItemCount -= 1
                            }
                            sourceComponent: Component {
                                TrackWaveformThumbnail {
                                    trackId: rowItem.trackId
                                    sourcePath: rowItem.path
                                    delegateGeneration: rowItem.waveformGeneration
                                    mode: SettingsController.listWaveformThumbnailMode
                                    provider: root.thumbnailProvider
                                }
                            }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    preventStealing: false
                    onClicked: function(mouse) {
                        root.updateSelection(rowItem.trackId,
                                             rowItem.index,
                                             mouse.modifiers)
                    }
                    onDoubleClicked: {
                        if (rowItem.available) {
                            PlaybackController.playTrackIds(
                                        root.visibleTrackIds(), rowItem.trackId)
                        }
                    }
                }
                DragHandler {
                    id: titleTrackDragHandler
                    target: null
                    acceptedButtons: Qt.LeftButton
                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                                     | PointerHandler.ApprovesTakeOverByAnything
                    property bool ownsTrackSession: false
                    property real activeTranslationY: 0
                    property point activeRootPosition: Qt.point(0, 0)
                    onTranslationChanged: {
                        if (!active)
                            return
                        activeTranslationY = translation.y
                        activeRootPosition = titleCell.mapToItem(
                                    root, centroid.position.x,
                                    centroid.position.y)
                        var overlayPosition = titleCell.mapToItem(
                                    windowDragProxy.parent,
                                    centroid.position.x, centroid.position.y)
                        windowDragProxy.x = overlayPosition.x
                        windowDragProxy.y = overlayPosition.y
                    }
                    onActiveChanged: {
                        if (active) {
                            activeTranslationY = 0
                            var press = centroid.pressPosition
                            activeRootPosition = titleCell.mapToItem(
                                        root, press.x, press.y)
                            if (!root.isSelected(rowItem.trackId))
                                root.selectOnly(rowItem.trackId, rowItem.index)
                            var pointerOrigin = titleCell.mapToItem(
                                        windowDragProxy.parent, press.x, press.y)
                            windowDragProxy.x = pointerOrigin.x
                            windowDragProxy.y = pointerOrigin.y
                            root.beginTrackDrag(rowItem.trackId,
                                                rowItem.dragTrackIds,
                                                rowItem.title,
                                                rowItem.dragCoverSource,
                                                windowDragProxy,
                                                rowItem.y, rowItem.height)
                            windowDragProxy.Drag.active = true
                            ownsTrackSession = true
                        } else if (ownsTrackSession) {
                            ownsTrackSession = false
                            root.completeTrackDrag(windowDragProxy,
                                                   activeTranslationY,
                                                   activeRootPosition)
                        }
                    }
                }
            }
            Loader {
                id: singleWindowWaveformLoader
                objectName: "singleWindowWaveformThumbnailLoader"
                visible: root.singleWindowLayout
                active: visible && root.waveformThumbnailsVisible
                        && root.thumbnailHostVisible && rowItem.inViewport
                Layout.column: 2
                Layout.fillWidth: true
                Layout.preferredHeight: root.singleWindowWaveformHeight
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: root.singleWindowWaveformGap
                Layout.rightMargin: 0
                sourceComponent: Component {
                    TrackWaveformThumbnail {
                        trackId: rowItem.trackId
                        sourcePath: rowItem.path
                        delegateGeneration: rowItem.waveformGeneration
                        mode: SettingsController.listWaveformThumbnailMode
                        provider: root.thumbnailProvider
                    }
                }
                MouseArea {
                    objectName: "singleWindowWaveformActivation"
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: function(mouse) {
                        root.updateSelection(rowItem.trackId,
                                             rowItem.index,
                                             mouse.modifiers)
                    }
                    onDoubleClicked: {
                        if (rowItem.available) {
                            PlaybackController.playTrackIds(
                                        root.visibleTrackIds(), rowItem.trackId)
                        }
                    }
                }
            }
            ToolButton {
                objectName: "trackFavoriteCell"
                Layout.column: root.singleWindowLayout
                               ? 5 : (root.relaxedClassicColumns ? 4 : 2)
                Layout.minimumWidth: root.favoriteWidth
                Layout.preferredWidth: root.favoriteWidth
                Layout.maximumWidth: root.favoriteWidth
                Layout.leftMargin: root.singleWindowLayout ? root.trailingColumnGap : 0
                icon.source: rowItem.favorite ? Theme.icon("heart-fill") : Theme.icon("heart-line")
                icon.color: rowItem.currentTrack ? rowItem.systemHighlightText
                            : rowItem.favorite ? Theme.favoriteRed
                                               : Theme.secondaryText
                icon.width: root.favoriteIconSize
                icon.height: root.favoriteIconSize
                onClicked: { var row = LibraryModel.indexForTrackId(rowItem.trackId); if (row >= 0) LibraryModel.setFavorite(row, !rowItem.favorite) }
                background: HoverBackground {}
            }
            Item {
                objectName: "trackFavoriteAlbumGap"
                visible: !root.relaxedClassicColumns && !root.singleWindowLayout
                Layout.column: 3
                Layout.minimumWidth: visible ? root.favoriteAlbumGap : 0
                Layout.preferredWidth: visible ? root.favoriteAlbumGap : 0
                Layout.maximumWidth: visible ? root.favoriteAlbumGap : 0
            }
            Item {
                objectName: "trackArtistCell"
                visible: root.showArtistColumn
                Layout.column: 4
                Layout.minimumWidth: visible ? root.artistWidth : 0
                Layout.preferredWidth: visible ? root.artistWidth : 0
                Layout.maximumWidth: visible ? root.artistWidth : 0
                Layout.fillHeight: true
                MarqueeBodyText {
                    objectName: "trackArtistMarquee"
                    anchors.fill: parent
                    text: rowItem.artist || "—"
                    trackAvailable: rowItem.available
                    highlighted: rowItem.systemHighlighted
                    highlightText: rowItem.systemHighlightText
                }
            }
            Item {
                objectName: "trackArtistAlbumGap"
                visible: root.showArtistColumn
                Layout.column: 5
                Layout.minimumWidth: visible ? root.artistAlbumGap : 0
                Layout.preferredWidth: visible ? root.artistAlbumGap : 0
                Layout.maximumWidth: visible ? root.artistAlbumGap : 0
            }
            Item {
                objectName: "trackAlbumCell"
                visible: root.showAlbumColumn
                Layout.column: 6
                Layout.minimumWidth: visible ? root.albumWidth : 0
                Layout.preferredWidth: visible ? root.albumWidth : 0
                Layout.maximumWidth: visible ? root.albumWidth : 0
                Layout.fillHeight: true
                MarqueeBodyText {
                    objectName: "trackAlbumMarquee"
                    anchors.fill: parent
                    text: rowItem.album || "—"
                    trackAvailable: rowItem.available
                    highlighted: rowItem.systemHighlighted
                    highlightText: rowItem.systemHighlightText
                }
            }
            Item {
                objectName: "trackAlbumRatingGap"
                visible: root.showAlbumColumn
                Layout.column: 7
                Layout.minimumWidth: visible ? root.albumRatingGap : 0
                Layout.preferredWidth: visible ? root.albumRatingGap : 0
                Layout.maximumWidth: visible ? root.albumRatingGap : 0
            }
            RowLayout {
                objectName: "trackRatingCell"
                Layout.column: root.singleWindowLayout
                               ? 4 : (root.relaxedClassicColumns ? 3 : 8)
                Layout.leftMargin: root.singleWindowLayout ? root.trailingColumnGap : (root.relaxedClassicColumns ? 12 : 0)
                spacing: 0
                Layout.minimumWidth: root.ratingWidth
                Layout.preferredWidth: root.ratingWidth
                Layout.maximumWidth: root.ratingWidth
                Repeater {
                    model: 5
                    delegate: ThemedIcon {
                        required property int index
                        objectName: "trackRatingStar" + index
                        source: index < rowItem.rating ? Theme.icon("star-fill") : Theme.icon("star-line")
                        tint: rowItem.currentTrack ? rowItem.systemHighlightText
                              : index < rowItem.rating
                                ? Theme.ratingColor(index) : Theme.iconSecondary
                        sourceSize.width: root.ratingIconSize
                        sourceSize.height: root.ratingIconSize
                        Layout.preferredWidth: root.ratingIconSize
                        Layout.preferredHeight: 20
                        TapHandler { onTapped: { var row = LibraryModel.indexForTrackId(rowItem.trackId); if (row >= 0) LibraryModel.setRating(row, rowItem.rating === index + 1 ? 0 : index + 1) } }
                    }
                }
            }
            BodyText { objectName: "trackBpmCell"; text: root.formatBpm(rowItem.bpm); visible: root.showBpmColumn; Layout.column: root.relaxedClassicColumns ? 5 : 9; horizontalAlignment: Text.AlignHCenter; trackAvailable: rowItem.available; highlighted: rowItem.systemHighlighted; highlightText: rowItem.systemHighlightText; Layout.minimumWidth: visible ? root.bpmWidth : 0; Layout.preferredWidth: visible ? root.bpmWidth : 0; Layout.maximumWidth: visible ? root.bpmWidth : 0 }
            BodyText { objectName: "trackDurationCell"; text: root.formatTime(rowItem.durationMs); visible: root.showDurationColumn; Layout.column: root.singleWindowLayout ? 3 : (root.relaxedClassicColumns ? 2 : 10); Layout.leftMargin: root.singleWindowLayout ? root.trailingColumnGap : 0; horizontalAlignment: root.singleWindowLayout ? Text.AlignHCenter : Text.AlignRight; trackAvailable: rowItem.available; highlighted: rowItem.systemHighlighted; highlightText: rowItem.systemHighlightText; Layout.minimumWidth: visible ? root.durationWidth : 0; Layout.preferredWidth: visible ? root.durationWidth : 0; Layout.maximumWidth: visible ? root.durationWidth : 0 }
        }

        HoverHandler { id: rowHover }
        TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.openTrackMenu(rowItem.trackId, rowItem.favorite, rowItem.index) }
        DropArea {
            anchors.fill: parent
            keys: ["application/x-agplayer-track-ids"]
            onDropped: function(drop) {
                var encoded = drop.getDataAsString("application/x-agplayer-track-ids")
                var ids = encoded ? JSON.parse(encoded) : []
                if (ids.length === 0 && drop.source && drop.source.dragTrackIds)
                    ids = drop.source.dragTrackIds
                if (ids.length === 0 || ids.indexOf(rowItem.trackId) >= 0) return
                if (root.customPlaylistSelected) {
                    root.playlistModel.reorderTracks(root.selectedCategory,
                                                     ids, rowItem.trackId)
                } else if (root.selectedCategory === "all") {
                    LibraryModel.reorderTracks(ids, rowItem.trackId)
                }
                drop.acceptProposedAction()
            }
        }
    }

    Label {
        objectName: "emptyTrackResult"
        visible: root.count === 0
        x: (root.width - width) / 2
        y: root.contentY + (root.height - height) / 2
        text: qsTr("未找到符合条件的歌曲")
        color: Theme.secondaryText; font.pixelSize: Theme.fontSizeBody; z: 5
    }

    Menu {
        id: trackMenu
        objectName: "trackContextMenu"
        width: root.fittedMenuWidth([
            qsTr("播放"), qsTr("下一首播放"), qsTr("在文件夹中显示"),
            qsTr("复制文件路径"), qsTr("打标签"),
            qsTr("移动到指定文件夹"), qsTr("复制到指定文件夹"),
            qsTr("从列表删除"), qsTr("彻底删除至回收站")
        ], [qsTr("加入歌单"), qsTr("使用音频工具打开")])
        palette.window: Theme.elevated
        palette.text: Theme.primaryText
        palette.button: Theme.elevated
        palette.buttonText: Theme.primaryText
        palette.highlight: Theme.activeSelection
        palette.highlightedText: Theme.activeSelectionText
        palette.mid: Theme.border
        delegate: ThemedMenuItem {
            implicitWidth: Math.ceil(contentItem.implicitWidth
                                     + leftPadding + rightPadding)
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
        }
        property string targetTrackId
        property var targetTrackIds: []
        property bool targetFavorite: false
        SystemMenuItem { objectName: "trackMenuPlay"; text: qsTr("播放"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: PlaybackController.playTrackIds(root.visibleTrackIds(), trackMenu.targetTrackId) }
        SystemMenuItem { objectName: "trackMenuPlayNext"; text: qsTr("下一首播放"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: PlaybackController.queueNext(trackMenu.targetTrackId) }
        Menu {
            id: moveMenu
            width: 154
            palette.window: Theme.elevated
            palette.text: Theme.primaryText
            palette.button: Theme.elevated
            palette.buttonText: Theme.primaryText
            palette.highlight: Theme.activeSelection
            palette.highlightedText: Theme.activeSelectionText
            palette.mid: Theme.border
            delegate: ThemedMenuItem {
                implicitWidth: Math.ceil(contentItem.implicitWidth
                                         + leftPadding + rightPadding)
            }
            background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusSm }
            objectName: "moveTracksMenu"
            title: qsTr("加入歌单")
            enabled: root.playlistModel.count > 0
            Instantiator {
                model: root.playlistModel
                delegate: SystemMenuItem {
                    required property string playlistId
                    required property string name
                    objectName: "playlistMoveTarget-" + playlistId
                    text: name; enabled: playlistId !== root.selectedCategory
                    onClicked: {
                        var ids = root.actionTrackIds()
                        if (ids.length === 0)
                            return
                        if (root.customPlaylistSelected)
                            root.playlistModel.moveTracks(root.selectedCategory,
                                                          playlistId, ids)
                        else
                            root.playlistModel.addTracks(playlistId, ids)
                    }
                }
                onObjectAdded: function(index, object) { moveMenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { moveMenu.removeItem(object) }
            }
        }
        Menu {
            id: audioToolsMenu
            objectName: "audioToolsTrackMenu"
            width: root.fittedMenuWidth([
                qsTr("音频编辑"), qsTr("格式转换"), qsTr("元数据修改"),
                qsTr("文件名处理"), qsTr("无损鉴别")
            ], [])
            title: qsTr("使用音频工具打开")
            palette.window: Theme.elevated
            palette.text: Theme.primaryText
            palette.button: Theme.elevated
            palette.buttonText: Theme.primaryText
            palette.highlight: Theme.activeSelection
            palette.highlightedText: Theme.activeSelectionText
            palette.mid: Theme.border
            delegate: ThemedMenuItem {
                implicitWidth: Math.ceil(contentItem.implicitWidth
                                         + leftPadding + rightPadding)
            }
            background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusSm }
            SystemMenuItem { objectName: "trackMenuAudioEditor"; text: qsTr("音频编辑"); onClicked: root.openInAudioTool(0) }
            SystemMenuItem { objectName: "trackMenuFormatConverter"; text: qsTr("格式转换"); onClicked: root.openInAudioTool(1) }
            SystemMenuItem { objectName: "trackMenuMetadataEditor"; text: qsTr("元数据修改"); onClicked: root.openInAudioTool(2) }
            SystemMenuItem { objectName: "trackMenuFilenameProcessor"; text: qsTr("文件名处理"); onClicked: root.openInAudioTool(3) }
            SystemMenuItem { objectName: "trackMenuLosslessIdentify"; text: qsTr("无损鉴别"); onClicked: root.openInAudioTool(5) }
        }
        MenuSeparator {}
        SystemMenuItem { objectName: "trackMenuShowFolder"; text: qsTr("在文件夹中显示"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: fileOps.showInFolder(trackMenu.targetTrackId) }
        SystemMenuItem { objectName: "trackMenuCopyPath"; text: qsTr("复制文件路径"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: fileOps.copyPath(trackMenu.targetTrackId) }
        SystemMenuItem { objectName: "trackMenuTag"; text: qsTr("打标签"); onTriggered: tagDialog.open() }
        SystemMenuItem { objectName: "trackMenuMoveFile"; text: qsTr("移动到指定文件夹"); onTriggered: moveFolderDialog.open() }
        SystemMenuItem { objectName: "trackMenuCopyFile"; text: qsTr("复制到指定文件夹"); onTriggered: copyFolderDialog.open() }
        MenuSeparator {}
        SystemMenuItem {
            objectName: "trackMenuRemove"
            text: qsTr("从列表删除")
            enabled: root.canRemoveFromCurrentView
            onTriggered: root.removeSelectedFromCurrentView()
        }
        SystemMenuItem { objectName: "trackMenuTrash"; text: qsTr("彻底删除至回收站"); onTriggered: trashConfirm.open() }
    }

    component SystemMenuItem: ThemedMenuItem {
        width: parent ? parent.width : implicitWidth
        implicitWidth: Math.ceil(contentItem.implicitWidth
                                 + leftPadding + rightPadding)
    }

    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    Connections {
        target: PlaybackController
        function onCurrentTrackIdChanged() {
            Qt.callLater(root.ensureCurrentTrackVisible)
        }
    }
    component HeaderText: Text { color: Theme.secondaryText; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeCaption; font.weight: root.headerFontWeight; elide: Text.ElideRight }
    component BodyText: Text { property bool trackAvailable: true; property bool highlighted: false; property color highlightText: Theme.activeSelectionText; color: highlighted ? highlightText : trackAvailable ? Theme.secondaryText : Theme.error; font.family: Theme.fontPrimary; font.pixelSize: Theme.fontSizeBody; elide: Text.ElideRight; wrapMode: Text.NoWrap; maximumLineCount: 1; clip: true }
    component MarqueeBodyText: Item {
        id: marqueeRoot
        property alias text: marqueeText.text
        property bool trackAvailable: true
        property bool highlighted: false
        property color highlightText: Theme.activeSelectionText
        property int fontWeight: Font.Normal
        readonly property bool overflowing: marqueeText.implicitWidth > width
        readonly property real textOffset: marqueeText.x
        implicitHeight: marqueeText.implicitHeight
        clip: true

        Text {
            id: marqueeText
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            color: marqueeRoot.highlighted ? marqueeRoot.highlightText
                   : marqueeRoot.trackAvailable ? Theme.secondaryText
                                                 : Theme.error
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            font.weight: marqueeRoot.fontWeight
            wrapMode: Text.NoWrap
        }
        MouseArea {
            id: marqueeHover
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
        }
        SequentialAnimation {
            running: marqueeHover.containsMouse && marqueeRoot.overflowing
            loops: Animation.Infinite
            onRunningChanged: if (!running) marqueeText.x = 0
            PauseAnimation { duration: 350 }
            NumberAnimation {
                target: marqueeText
                property: "x"
                to: marqueeRoot.width - marqueeText.implicitWidth
                duration: Math.max(700,
                                   (marqueeText.implicitWidth - marqueeRoot.width) * 22)
                easing.type: Easing.Linear
            }
            PauseAnimation { duration: 450 }
            NumberAnimation {
                target: marqueeText
                property: "x"
                to: 0
                duration: 300
                easing.type: Easing.InOutQuad
            }
        }
    }
    component HoverBackground: Rectangle { color: parent.pressed ? Theme.surfacePressed : parent.visualFocus ? Theme.surfaceHover : parent.hovered ? Theme.hoverSurface : "transparent"; border.color: parent.visualFocus ? Theme.focus : "transparent"; border.width: parent.visualFocus ? 2 : 0; radius: Theme.radiusSm }
}
