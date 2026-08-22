import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

ListView {
    id: root
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
    property var selectedTrackIds: []
    property int selectionAnchor: -1
    readonly property bool windowActive: root.Window.active
    readonly property bool showAlbumColumn: true
    readonly property bool compactColumns: width < 900
    readonly property int favoriteAlbumGap: 10
    readonly property int sequenceWidth: compactColumns ? 34 : 42
    readonly property int favoriteWidth: compactColumns ? 42 : 52
    readonly property int albumWidth: compactColumns ? 92 : 136
    readonly property int artistWidth: compactColumns ? 94 : 130
    readonly property int ratingWidth: compactColumns ? 82 : 110
    readonly property int bpmWidth: compactColumns ? 48 : 64
    readonly property int durationWidth: compactColumns ? 58 : 72
    readonly property int titleMinimumWidth: compactColumns ? 150 : 180
    readonly property int rowHeight: SettingsController.listWaveformThumbnailEnabled
                                     ? 62 : 42
    property int thumbnailItemCount: 0
    property int nextWaveformGeneration: 0
    property int dragPreviewCreationCount: 0
    property var dragTrackIds: []
    property var activeDragProxy: null
    property string dragPreviewTitle: ""
    property url dragPreviewCover: ""
    readonly property bool thumbnailWindowVisible:
        !root.Window.window || root.Window.window.visible
    readonly property bool thumbnailHostVisible:
        root.visible && root.width > 0 && root.height > 0
        && root.thumbnailWindowVisible
    model: trackModel

    // A list can be created after the playback state has already been restored.
    // Sync on creation and after asynchronous model batches so the current song
    // is never left off-screen merely because no new playback signal arrived.
    Component.onCompleted: Qt.callLater(root.ensureCurrentTrackVisible)
    onCountChanged: if (count > 0) Qt.callLater(root.ensureCurrentTrackVisible)
    onVisibleChanged: if (visible) Qt.callLater(root.ensureCurrentTrackVisible)

    readonly property bool customPlaylistSelected:
        selectedCategory !== "all" && selectedCategory !== "favorites"
        && selectedCategory !== "history" && selectedCategory !== "library"

    LibraryFileOperations { id: fileOps; libraryModel: LibraryModel }

    function formatTime(ms) {
        if (ms <= 0) return "--:--"
        var total = Math.floor(ms / 1000)
        var minutes = Math.floor(total / 60)
        return (minutes < 10 ? "0" : "") + minutes + ":"
                + (total % 60 < 10 ? "0" : "") + total % 60
    }
    function isCurrentTrack(trackId) { return PlaybackController.currentTrackId === trackId }
    function isSelected(trackId) { return selectedTrackIds.indexOf(trackId) >= 0 }
    function trackIdAt(row) {
        if (row < 0 || row >= count) return ""
        return trackModel.data(trackModel.index(row, 0), LibraryModel.TrackIdRole)
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
    function beginTrackDrag(rowItem, proxy, coverSource) {
        dragTrackIds = rowItem.dragTrackIds.slice()
        dragPreviewTitle = rowItem.title || qsTr("未知歌曲")
        dragPreviewCover = coverSource
        activeDragProxy = proxy
        dragPreviewCreationCount += 1
        dragPreviewLoader.active = true
    }
    function endTrackDrag() {
        dragPreviewLoader.active = false
        activeDragProxy = null
        dragTrackIds = []
    }
    function cancelTrackDrag() {
        if (activeDragProxy && activeDragProxy.Drag.active)
            activeDragProxy.Drag.cancel()
        endTrackDrag()
    }
    function removeSelectedFromCurrentView() {
        var ids = selectedTrackIds.slice()
        if (ids.length === 0) return
        if (customPlaylistSelected) {
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
                LibraryModel.removeTrack(ids[libraryIndex])
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
    function openDetails() {
        detailsPanel.details = fileOps.trackDetails(trackMenu.targetTrackId)
        detailsPanel.open()
    }
    function openFirstDetailsForQa() {
        var trackId = trackIdAt(0)
        if (!trackId) return
        trackMenu.targetTrackId = trackId
        trackMenu.targetTrackIds = [trackId]
        openDetails()
    }
    function selectedFileUrls() {
        var urls = []
        for (var index = 0; index < trackMenu.targetTrackIds.length; ++index) {
            var url = fileOps.fileUrl(trackMenu.targetTrackIds[index])
            if (url && String(url).length > 0) urls.push(url)
        }
        return urls
    }
    function openInAudioTool(toolIndex) {
        var urls = selectedFileUrls()
        if (urls.length === 0) return
        if (toolIndex === 0) AudioEditorController.openFile(urls[0])
        else if (toolIndex === 1) FormatConverter.loadFiles(urls)
        else if (toolIndex === 2) MetadataEditor.loadFiles(urls)
        else FilenameProcessor.loadFiles(urls)
        AudioToolsController.selectTool(toolIndex)
        WindowController.showAudioTools()
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            selectAllVisible(); event.accepted = true
        } else if (event.key === Qt.Key_Delete) {
            removeSelectedFromCurrentView(); event.accepted = true
        } else if (event.key === Qt.Key_Escape && dragPreviewLoader.active) {
            cancelTrackDrag(); event.accepted = true
        }
    }
    Shortcut { sequence: StandardKey.SelectAll; context: Qt.WindowShortcut; enabled: root.activeFocus; onActivated: root.selectAllVisible() }

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
        nameFilters: ["Audio (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.wma)"]
        onAccepted: fileOps.relocateTrackToUrl(trackMenu.targetTrackId, selectedFile)
    }
    Dialog {
        id: renameDialog
        title: qsTr("重命名")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: fileOps.renameTrack(trackMenu.targetTrackId, renameField.text)
        contentItem: TextField { id: renameField; placeholderText: qsTr("新文件名") }
        background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusMd }
    }
    Dialog {
        id: tagDialog
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
            for (var index = 0; index < trackMenu.targetTrackIds.length; ++index)
                LibraryModel.setTags(trackMenu.targetTrackIds[index], values)
        }
        contentItem: TextField { id: tagField; placeholderText: qsTr("用逗号分隔多个标签") }
        background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusMd }
    }
    Dialog {
        id: trashConfirm
        width: 460
        title: qsTr("彻底删除至回收站")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label { text: qsTr("确定把选中的音乐文件移到系统回收站？"); color: Theme.primaryText }
        onAccepted: { fileOps.trashTracks(trackMenu.targetTrackIds); root.selectedTrackIds = [] }
        background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusMd }
    }

    header: Rectangle {
        width: root.width; height: 56; color: Theme.listHeaderSurface; z: 20
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 0
            HeaderText { objectName: "trackHeaderIndex"; text: "#"; Layout.minimumWidth: root.sequenceWidth; Layout.preferredWidth: root.sequenceWidth; Layout.maximumWidth: root.sequenceWidth }
            HeaderText { objectName: "trackHeaderTitle"; text: qsTr("歌曲"); Layout.fillWidth: true; Layout.minimumWidth: root.titleMinimumWidth }
            HeaderText { objectName: "trackHeaderFavorite"; text: qsTr("收藏"); horizontalAlignment: Text.AlignHCenter; Layout.minimumWidth: root.favoriteWidth; Layout.preferredWidth: root.favoriteWidth; Layout.maximumWidth: root.favoriteWidth }
            Item { objectName: "trackHeaderFavoriteAlbumGap"; Layout.minimumWidth: root.favoriteAlbumGap; Layout.preferredWidth: root.favoriteAlbumGap; Layout.maximumWidth: root.favoriteAlbumGap }
            HeaderText { objectName: "trackHeaderArtist"; text: qsTr("艺术家"); Layout.minimumWidth: root.artistWidth; Layout.preferredWidth: root.artistWidth; Layout.maximumWidth: root.artistWidth }
            HeaderText { objectName: "trackHeaderAlbum"; text: qsTr("专辑"); visible: root.showAlbumColumn; Layout.minimumWidth: visible ? root.albumWidth : 0; Layout.preferredWidth: visible ? root.albumWidth : 0; Layout.maximumWidth: visible ? root.albumWidth : 0 }
            HeaderText { objectName: "trackHeaderRating"; text: qsTr("评分"); horizontalAlignment: Text.AlignHCenter; Layout.minimumWidth: root.ratingWidth; Layout.preferredWidth: root.ratingWidth; Layout.maximumWidth: root.ratingWidth }
            HeaderText { objectName: "trackHeaderBpm"; text: "BPM"; horizontalAlignment: Text.AlignHCenter; Layout.minimumWidth: root.bpmWidth; Layout.preferredWidth: root.bpmWidth; Layout.maximumWidth: root.bpmWidth }
            HeaderText { objectName: "trackHeaderDuration"; text: qsTr("时长"); horizontalAlignment: Text.AlignRight; Layout.minimumWidth: root.durationWidth; Layout.preferredWidth: root.durationWidth; Layout.maximumWidth: root.durationWidth }
        }
    }

    Loader {
        id: dragPreviewLoader
        active: false
        z: 1000
        x: root.activeDragProxy
           ? root.activeDragProxy.mapToItem(root, 12, 12).x : 0
        y: root.activeDragProxy
           ? root.activeDragProxy.mapToItem(root, 12, 12).y : 0
        sourceComponent: Component {
            Rectangle {
                objectName: "trackDragPreview"
                readonly property int selectedCount: root.dragTrackIds.length
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
                        text: root.dragTrackIds.length > 1
                              ? qsTr("已选择 %1 首").arg(root.dragTrackIds.length)
                              : root.dragPreviewTitle
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.maximumWidth: 230
                    }
                }
            }
        }
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
        readonly property bool systemHighlighted:
            root.isCurrentTrack(trackId) || root.isSelected(trackId)
        readonly property color systemHighlightColor:
            root.windowActive ? Theme.activeSelection : Theme.inactiveSelection
        readonly property color systemHighlightText:
            root.windowActive ? Theme.activeSelectionText : Theme.inactiveSelectionText
        readonly property var dragTrackIds:
            root.isSelected(trackId) ? root.selectedTrackIds.slice() : [trackId]
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

        Item {
            id: rowDragProxy
            objectName: "trackDragProxy"
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

        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 0
            Item {
                objectName: "trackIndexCell"
                Layout.minimumWidth: root.sequenceWidth; Layout.preferredWidth: root.sequenceWidth; Layout.maximumWidth: root.sequenceWidth; Layout.fillHeight: true
                Text { anchors.verticalCenter: parent.verticalCenter; visible: !root.isCurrentTrack(rowItem.trackId); text: rowItem.index + 1; color: rowItem.systemHighlighted ? rowItem.systemHighlightText : Theme.secondaryText; font.pixelSize: 13 }
                Item {
                    id: playingBars
                    objectName: "playingBarsIndicator"
                    readonly property int barCount: 3
                    readonly property color barColor: Theme.waveformMagenta
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
                Layout.fillWidth: true
                Layout.minimumWidth: root.titleMinimumWidth
                Layout.fillHeight: true
                RowLayout {
                    anchors.fill: parent
                    spacing: 6
                    Image {
                        id: trackCoverImage
                        objectName: "trackCover"
                        source: rowItem.coverUrl ? rowItem.coverUrl
                                                 : Theme.icon("music-2-fill")
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        sourceSize.width: 34
                        sourceSize.height: 34
                        fillMode: Image.PreserveAspectFit
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        MarqueeBodyText {
                            objectName: "trackTitleMarquee"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            y: SettingsController.listWaveformThumbnailEnabled
                               ? 9 : (parent.height - height) / 2
                            height: implicitHeight
                            text: rowItem.title || qsTr("未知歌曲")
                            trackAvailable: rowItem.available
                            highlighted: rowItem.systemHighlighted
                            highlightText: rowItem.systemHighlightText
                        }

                        Loader {
                            id: waveformWrapperLoader
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 8
                            height: 9
                            active: SettingsController.listWaveformThumbnailEnabled
                                    && root.thumbnailHostVisible
                                    && rowItem.inViewport
                            property bool counted: false
                            onLoaded: {
                                if (!counted) {
                                    counted = true
                                    root.thumbnailItemCount += 1
                                }
                            }
                            onItemChanged: {
                                if (!item && counted) {
                                    counted = false
                                    root.thumbnailItemCount -= 1
                                }
                            }
                            Component.onDestruction: {
                                if (counted)
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
                DragHandler {
                    id: titleDragHandler
                    target: rowDragProxy
                    acceptedButtons: Qt.LeftButton
                    onActiveChanged: {
                        if (active) {
                            if (!root.isSelected(rowItem.trackId))
                                root.selectOnly(rowItem.trackId, rowItem.index)
                            root.beginTrackDrag(rowItem, rowDragProxy,
                                                trackCoverImage.source)
                            rowDragProxy.Drag.active = true
                        } else if (rowDragProxy.Drag.active) {
                            var dropAction = rowDragProxy.Drag.drop()
                            var draggedTrackId = rowItem.trackId
                            var originY = rowItem.y
                            var deltaY = rowDragProxy.y
                            var draggedRowHeight = rowItem.height
                            rowDragProxy.x = 0
                            rowDragProxy.y = 0
                            root.endTrackDrag()
                            if (dropAction === Qt.IgnoreAction) {
                                root.finishRowDrag(draggedTrackId, originY,
                                                   deltaY, draggedRowHeight)
                            }
                        }
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: root.updateSelection(rowItem.trackId,
                                                   rowItem.index,
                                                   point.modifiers)
                    onDoubleTapped: {
                        if (rowItem.available) {
                            var row = LibraryModel.indexForTrackId(rowItem.trackId)
                            if (row >= 0) LibraryModel.playRow(row)
                        }
                    }
                }
            }
            ToolButton {
                objectName: "trackFavoriteCell"
                Layout.minimumWidth: root.favoriteWidth
                Layout.preferredWidth: root.favoriteWidth
                Layout.maximumWidth: root.favoriteWidth
                icon.source: rowItem.favorite ? Theme.icon("heart-fill") : Theme.icon("heart-line")
                icon.color: rowItem.favorite ? Theme.favoriteRed : Theme.secondaryText
                icon.width: 23; icon.height: 23
                onClicked: { var row = LibraryModel.indexForTrackId(rowItem.trackId); if (row >= 0) LibraryModel.setFavorite(row, !rowItem.favorite) }
                background: HoverBackground {}
            }
            Item {
                objectName: "trackFavoriteAlbumGap"
                Layout.minimumWidth: root.favoriteAlbumGap
                Layout.preferredWidth: root.favoriteAlbumGap
                Layout.maximumWidth: root.favoriteAlbumGap
            }
            Item {
                objectName: "trackArtistCell"
                Layout.minimumWidth: root.artistWidth
                Layout.preferredWidth: root.artistWidth
                Layout.maximumWidth: root.artistWidth
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
                objectName: "trackAlbumCell"
                visible: root.showAlbumColumn
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
            RowLayout {
                objectName: "trackRatingCell"
                spacing: 0
                Layout.minimumWidth: root.ratingWidth
                Layout.preferredWidth: root.ratingWidth
                Layout.maximumWidth: root.ratingWidth
                Repeater {
                    model: 5
                    delegate: ThemedIcon {
                        required property int index
                        source: index < rowItem.rating ? Theme.icon("star-fill") : Theme.icon("star-line")
                        tint: index < rowItem.rating ? Theme.ratingColor(index) : Theme.iconSecondary
                        sourceSize.width: root.compactColumns ? 15 : 17
                        sourceSize.height: root.compactColumns ? 15 : 17
                        Layout.preferredWidth: root.compactColumns ? 15 : 17
                        Layout.preferredHeight: 20
                        TapHandler { onTapped: { var row = LibraryModel.indexForTrackId(rowItem.trackId); if (row >= 0) LibraryModel.setRating(row, rowItem.rating === index + 1 ? 0 : index + 1) } }
                    }
                }
            }
            BodyText { objectName: "trackBpmCell"; text: rowItem.bpm > 0 ? Math.round(rowItem.bpm) : "—"; horizontalAlignment: Text.AlignHCenter; trackAvailable: rowItem.available; highlighted: rowItem.systemHighlighted; highlightText: rowItem.systemHighlightText; Layout.minimumWidth: root.bpmWidth; Layout.preferredWidth: root.bpmWidth; Layout.maximumWidth: root.bpmWidth }
            BodyText { objectName: "trackDurationCell"; text: root.formatTime(rowItem.durationMs); horizontalAlignment: Text.AlignRight; trackAvailable: rowItem.available; highlighted: rowItem.systemHighlighted; highlightText: rowItem.systemHighlightText; Layout.minimumWidth: root.durationWidth; Layout.preferredWidth: root.durationWidth; Layout.maximumWidth: root.durationWidth }
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
        color: Theme.secondaryText; font.pixelSize: 15; z: 5
    }

    Menu {
        id: trackMenu
        objectName: "trackContextMenu"
        width: 240
        palette.window: Theme.elevated
        palette.text: Theme.primaryText
        palette.button: Theme.elevated
        palette.buttonText: Theme.primaryText
        palette.highlight: Theme.activeSelection
        palette.highlightedText: Theme.activeSelectionText
        palette.mid: Theme.border
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
        }
        property string targetTrackId
        property var targetTrackIds: []
        property bool targetFavorite: false
        SystemMenuItem { objectName: "trackMenuPlay"; text: qsTr("播放"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: { var row = LibraryModel.indexForTrackId(trackMenu.targetTrackId); if (row >= 0) LibraryModel.playRow(row) } }
        SystemMenuItem { objectName: "trackMenuPlayNext"; text: qsTr("下一首播放"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: PlaybackController.queueNext(trackMenu.targetTrackId) }
        Menu {
            id: moveMenu
            palette.window: Theme.elevated
            palette.text: Theme.primaryText
            palette.highlight: Theme.activeSelection
            palette.highlightedText: Theme.activeSelectionText
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
                    onTriggered: root.customPlaylistSelected
                                 ? root.playlistModel.moveTracks(root.selectedCategory, playlistId, trackMenu.targetTrackIds)
                                 : root.playlistModel.addTracks(playlistId, trackMenu.targetTrackIds)
                }
                onObjectAdded: function(index, object) { moveMenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { moveMenu.removeItem(object) }
            }
        }
        Menu {
            objectName: "audioToolsTrackMenu"
            title: qsTr("使用音频工具打开")
            palette.window: Theme.elevated
            palette.text: Theme.primaryText
            palette.highlight: Theme.activeSelection
            palette.highlightedText: Theme.activeSelectionText
            background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusSm }
            SystemMenuItem { objectName: "trackMenuLightEditor"; text: qsTr("轻度剪辑"); onTriggered: root.openInAudioTool(0) }
            SystemMenuItem { objectName: "trackMenuFormatConverter"; text: qsTr("格式转换"); onTriggered: root.openInAudioTool(1) }
            SystemMenuItem { objectName: "trackMenuMetadataEditor"; text: qsTr("元数据修改"); onTriggered: root.openInAudioTool(2) }
            SystemMenuItem { objectName: "trackMenuFilenameProcessor"; text: qsTr("文件名处理"); onTriggered: root.openInAudioTool(3) }
        }
        MenuSeparator {}
        SystemMenuItem { objectName: "trackMenuShowFolder"; text: qsTr("在文件夹中显示"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: fileOps.showInFolder(trackMenu.targetTrackId) }
        SystemMenuItem { objectName: "trackMenuCopyPath"; text: qsTr("复制文件路径"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: fileOps.copyPath(trackMenu.targetTrackId) }
        SystemMenuItem { objectName: "trackMenuTag"; text: qsTr("打标签"); onTriggered: tagDialog.open() }
        SystemMenuItem { objectName: "trackMenuRename"; text: qsTr("重命名"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: root.beginRename() }
        SystemMenuItem { objectName: "trackMenuMoveFile"; text: qsTr("移动到指定文件夹"); onTriggered: moveFolderDialog.open() }
        SystemMenuItem { objectName: "trackMenuCopyFile"; text: qsTr("复制到指定文件夹"); onTriggered: copyFolderDialog.open() }
        MenuSeparator {}
        SystemMenuItem {
            objectName: "trackMenuRemove"
            text: qsTr("从列表删除")
            onTriggered: root.removeSelectedFromCurrentView()
        }
        SystemMenuItem { objectName: "trackMenuTrash"; text: qsTr("彻底删除至回收站"); onTriggered: trashConfirm.open() }
        SystemMenuItem { objectName: "trackMenuRelocate"; text: qsTr("重新定位文件"); enabled: trackMenu.targetTrackIds.length === 1; onTriggered: relocateDialog.open() }
    }

    component SystemMenuItem: MenuItem {
        id: systemMenuItem
        width: 230
        implicitWidth: 230
        implicitHeight: 34
        contentItem: Text {
            text: systemMenuItem.text
            color: systemMenuItem.highlighted || systemMenuItem.hovered
                   ? Theme.activeSelectionText
                   : systemMenuItem.enabled ? Theme.primaryText : Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Math.max(13, Qt.application.font.pixelSize)
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: systemMenuItem.highlighted || systemMenuItem.hovered
                   ? Theme.activeSelection : "transparent"
            radius: Theme.radiusSm
        }
    }

    Popup {
        id: detailsPanel
        property var details: ({})
        width: 300; height: Math.min(root.height - 24, 470)
        x: root.width - width - 12; y: root.contentY + 12
        modal: false; focus: true; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.elevated; border.color: Theme.border; radius: Theme.radiusMd }
        contentItem: ColumnLayout {
            spacing: 7
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("文件信息")
                    color: Theme.primaryText
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                ToolButton {
                    icon.source: Theme.icon("close-fill")
                    onClicked: detailsPanel.close()
                    background: null
                }
            }
            Repeater {
                model: [
                    [qsTr("文件名"), detailsPanel.details.fileName], [qsTr("格式"), detailsPanel.details.format],
                    [qsTr("采样率"), detailsPanel.details.sampleRate], [qsTr("比特率"), detailsPanel.details.bitRate],
                    [qsTr("时长"), root.formatTime(detailsPanel.details.durationMs || 0)], ["BPM", detailsPanel.details.bpm],
                    [qsTr("目录"), detailsPanel.details.directory], [qsTr("完整路径"), detailsPanel.details.path],
                    [qsTr("标签"), (detailsPanel.details.tags || []).join(", ")]
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text { text: modelData[0]; color: Theme.secondaryText; Layout.preferredWidth: 62; font.pixelSize: 11 }
                    Text { text: modelData[1] || "—"; color: Theme.primaryText; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
                }
            }
            Image {
                visible: Boolean(detailsPanel.details.coverUrl)
                source: detailsPanel.details.coverUrl || ""
                Layout.preferredWidth: 120
                Layout.preferredHeight: 120
                Layout.alignment: Qt.AlignHCenter
                fillMode: Image.PreserveAspectFit
            }
            Item { Layout.fillHeight: true }
        }
    }

    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    Connections {
        target: PlaybackController
        function onCurrentTrackIdChanged() {
            Qt.callLater(root.ensureCurrentTrackVisible)
        }
    }
    component HeaderText: Text { color: Theme.secondaryText; font.family: Theme.fontPrimary; font.pixelSize: 12; font.weight: Font.Medium; elide: Text.ElideRight }
    component BodyText: Text { property bool trackAvailable: true; property bool highlighted: false; property color highlightText: Theme.activeSelectionText; color: highlighted ? highlightText : trackAvailable ? Theme.secondaryText : Theme.favoriteRed; font.family: Theme.fontPrimary; font.pixelSize: 13; elide: Text.ElideRight; wrapMode: Text.NoWrap; maximumLineCount: 1; clip: true }
    component MarqueeBodyText: Item {
        id: marqueeRoot
        property alias text: marqueeText.text
        property bool trackAvailable: true
        property bool highlighted: false
        property color highlightText: Theme.activeSelectionText
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
                                                 : Theme.favoriteRed
            font.family: Theme.fontPrimary
            font.pixelSize: 13
            wrapMode: Text.NoWrap
        }
        HoverHandler { id: marqueeHover }
        SequentialAnimation {
            running: marqueeHover.hovered && marqueeRoot.overflowing
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
    component HoverBackground: Rectangle { color: parent.pressed ? Theme.cyan : parent.visualFocus ? Theme.border : parent.hovered ? Theme.hoverSurface : "transparent"; border.color: parent.visualFocus ? Theme.cyan : "transparent"; border.width: parent.visualFocus ? 2 : 0; radius: Theme.radiusSm }
}
