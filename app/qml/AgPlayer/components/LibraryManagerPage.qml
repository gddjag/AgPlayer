import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: "libraryManagerPage"

    property string selectedTrackId: ""
    property var selectedTrackIds: []
    property string searchText: ""
    property string formatFilter: ""
    property int minBpm: 60
    property int maxBpm: 160
    property int exactRating: 0
    readonly property int visibleRowCount: 10
    readonly property int trackRowHeight: 42
    readonly property int preferredTrackViewportHeight:
        visibleRowCount * trackRowHeight
    readonly property int preferredWindowHeight: 570
    implicitHeight: preferredWindowHeight
    readonly property int pageSize: manager.pageSize
    readonly property int pageCount: manager.pageCount
    readonly property var selectedTrack: selectedTrackId.length > 0
                                        ? LibraryModel.trackForId(selectedTrackId) : ({})

    function formatBytes(value) {
        if (value >= 1099511627776) return (value / 1099511627776).toFixed(1) + " TB"
        if (value >= 1073741824) return (value / 1073741824).toFixed(1) + " GB"
        if (value >= 1048576) return (value / 1048576).toFixed(1) + " MB"
        return Math.max(0, value / 1024).toFixed(1) + " KB"
    }
    function formatDuration(value) {
        var seconds = Math.max(0, Math.floor(Number(value) / 1000))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }
    function toggleSelection(trackId, additive) {
        var ids = additive ? selectedTrackIds.slice() : []
        var position = ids.indexOf(trackId)
        if (position >= 0) ids.splice(position, 1)
        else ids.push(trackId)
        selectedTrackIds = ids
        selectedTrackId = ids.length > 0 ? ids[ids.length - 1] : ""
    }
    component Surface: Rectangle {
        radius: Theme.radiusMd
        color: Theme.panel
        border.color: Theme.border
        border.width: 1
    }

    component ActionButton: Button {
        implicitHeight: 36
        leftPadding: 14
        rightPadding: 14
        palette.button: highlighted ? Theme.accent : Theme.elevated
        palette.buttonText: highlighted ? Theme.activeSelectionText : Theme.primaryText
        palette.highlight: Theme.accent
        palette.highlightedText: Theme.activeSelectionText
    }

    component SystemMenuItem: MenuItem {
        id: menuItem
        implicitWidth: 230
        implicitHeight: 34
        contentItem: Text {
            text: menuItem.text
            color: menuItem.highlighted || menuItem.hovered
                   ? Theme.activeSelectionText
                   : menuItem.enabled ? Theme.primaryText : Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Math.max(13, Qt.application.font.pixelSize)
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: menuItem.highlighted || menuItem.hovered
                   ? Theme.activeSelection : "transparent"
            radius: Theme.radiusSm
        }
    }

    LibraryManagerController {
        id: manager
        objectName: "libraryManagerController"
        libraryModel: LibraryModel
        importController: ImportController
        storagePath: SettingsController.libraryManagerPath
        keyword: root.searchText
        formatFilter: root.formatFilter
        exactRating: root.exactRating
    }

    LibraryFileOperations { id: fileOps; libraryModel: LibraryModel }

    function openTrackMenu(trackId) {
        if (selectedTrackIds.indexOf(trackId) < 0) {
            selectedTrackIds = [trackId]
            selectedTrackId = trackId
        }
        trackMenu.targetTrackId = trackId
        trackMenu.targetTrackIds = selectedTrackIds.slice()
        trackMenu.popup()
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
        if (toolIndex === 0) LightEditor.queueFiles(urls)
        else if (toolIndex === 1) FormatConverter.loadFiles(urls)
        else if (toolIndex === 2) MetadataEditor.loadFiles(urls)
        else FilenameProcessor.loadFiles(urls)
        AudioToolsController.selectTool(toolIndex)
        WindowController.showAudioTools()
    }

    onMinBpmChanged: manager.setBpmRange(minBpm, maxBpm)
    onMaxBpmChanged: manager.setBpmRange(minBpm, maxBpm)

    FolderDialog {
        id: folderDialog
        title: qsTr("选择音乐文件夹")
        onAccepted: manager.addMonitoredFolderUrl(selectedFolder)
    }

    FileDialog {
        id: backupDialog
        title: qsTr("备份曲库数据")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("AgPlayer 曲库备份 (*.db)")]
        defaultSuffix: "db"
        currentFile: manager.defaultBackupUrl
        onAccepted: manager.backupLibraryData(selectedFile)
    }

    FileDialog {
        id: importBackupDialog
        title: qsTr("导入曲库备份")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("AgPlayer 曲库备份 (*.db)")]
        onAccepted: manager.importLibraryBackup(selectedFile)
    }

    Menu {
        id: backupMenu
        objectName: "libraryBackupMenu"
        MenuItem {
            objectName: "libraryBackupAction"
            text: qsTr("备份曲库")
            onTriggered: backupDialog.open()
        }
        MenuItem {
            objectName: "libraryImportBackupAction"
            text: qsTr("导入备份")
            onTriggered: importBackupDialog.open()
        }
    }

    FolderDialog {
        id: moveTrackFolderDialog
        title: qsTr("移动到指定文件夹")
        onAccepted: fileOps.moveTracksToUrl(trackMenu.targetTrackIds,
                                             selectedFolder,
                                             LibraryFileOperations.AutoRename)
    }
    FolderDialog {
        id: copyTrackFolderDialog
        title: qsTr("复制到指定文件夹")
        onAccepted: fileOps.copyTracksToUrl(trackMenu.targetTrackIds,
                                             selectedFolder,
                                             LibraryFileOperations.AutoRename)
    }
    FileDialog {
        id: relocateTrackDialog
        title: qsTr("重新定位文件")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Audio (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.wma)"]
        onAccepted: fileOps.relocateTrackToUrl(trackMenu.targetTrackId,
                                               selectedFile)
    }
    Dialog {
        id: renameTrackDialog
        width: 420
        title: qsTr("重命名")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            var details = fileOps.trackDetails(trackMenu.targetTrackId)
            renameTrackField.text = String(details.fileName || "")
                    .replace(/\.[^.]+$/, "")
            renameTrackField.forceActiveFocus()
            renameTrackField.selectAll()
        }
        onAccepted: fileOps.renameTrack(trackMenu.targetTrackId,
                                        renameTrackField.text)
        contentItem: TextField { id: renameTrackField }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }
    Dialog {
        id: tagTrackDialog
        width: 420
        title: qsTr("打标签")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            var details = fileOps.trackDetails(trackMenu.targetTrackId)
            tagTrackField.text = (details.tags || []).join(", ")
            tagTrackField.forceActiveFocus()
            tagTrackField.selectAll()
        }
        onAccepted: {
            var values = tagTrackField.text.split(/[,，]/).map(
                        function(value) { return value.trim() })
            for (var index = 0; index < trackMenu.targetTrackIds.length; ++index)
                LibraryModel.setTags(trackMenu.targetTrackIds[index], values)
        }
        contentItem: TextField {
            id: tagTrackField
            placeholderText: qsTr("用逗号分隔多个标签")
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }
    Dialog {
        id: trashTrackDialog
        width: 440
        title: qsTr("彻底删除至回收站")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label {
            text: qsTr("所选歌曲将移入系统回收站，是否继续？")
            color: Theme.primaryText
        }
        onAccepted: {
            fileOps.trashTracks(trackMenu.targetTrackIds)
            root.selectedTrackIds = []
            root.selectedTrackId = ""
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }

    Menu {
        id: trackMenu
        objectName: "libraryManagerTrackMenu"
        width: 240
        palette.window: Theme.elevated
        palette.text: Theme.primaryText
        palette.highlight: Theme.activeSelection
        palette.highlightedText: Theme.activeSelectionText
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusSm
        }
        property string targetTrackId
        property var targetTrackIds: []

        SystemMenuItem {
            objectName: "libraryTrackPlay"
            text: qsTr("播放")
            enabled: trackMenu.targetTrackIds.length === 1
            onTriggered: LibraryModel.playRow(
                             LibraryModel.indexForTrackId(trackMenu.targetTrackId))
        }
        SystemMenuItem {
            objectName: "libraryTrackPlayNext"
            text: qsTr("下一首播放")
            enabled: trackMenu.targetTrackIds.length === 1
            onTriggered: PlaybackController.queueNext(trackMenu.targetTrackId)
        }
        Menu {
            id: addToPlaylistMenu
            objectName: "libraryTrackAddToPlaylist"
            title: qsTr("加入歌单")
            palette.window: Theme.elevated
            palette.text: Theme.primaryText
            palette.highlight: Theme.activeSelection
            palette.highlightedText: Theme.activeSelectionText
            background: Rectangle {
                color: Theme.elevated
                border.color: Theme.border
                radius: Theme.radiusSm
            }
            Instantiator {
                model: PlaylistModel
                delegate: SystemMenuItem {
                    required property string playlistId
                    required property string name
                    text: name
                    onTriggered: PlaylistModel.addTracks(playlistId,
                                                          trackMenu.targetTrackIds)
                }
                onObjectAdded: function(index, object) {
                    addToPlaylistMenu.insertItem(index, object)
                }
                onObjectRemoved: function(index, object) {
                    addToPlaylistMenu.removeItem(object)
                }
            }
        }
        Menu {
            objectName: "libraryTrackAudioTools"
            title: qsTr("使用音频工具打开")
            palette.window: Theme.elevated
            palette.text: Theme.primaryText
            palette.highlight: Theme.activeSelection
            palette.highlightedText: Theme.activeSelectionText
            background: Rectangle {
                color: Theme.elevated
                border.color: Theme.border
                radius: Theme.radiusSm
            }
            SystemMenuItem { text: qsTr("轻度剪辑"); onTriggered: root.openInAudioTool(0) }
            SystemMenuItem { text: qsTr("格式转换"); onTriggered: root.openInAudioTool(1) }
            SystemMenuItem { text: qsTr("元数据修改"); onTriggered: root.openInAudioTool(2) }
            SystemMenuItem { text: qsTr("文件名处理"); onTriggered: root.openInAudioTool(3) }
        }
        MenuSeparator {}
        SystemMenuItem {
            objectName: "libraryTrackShowFolder"
            text: qsTr("在文件夹中显示")
            enabled: trackMenu.targetTrackIds.length === 1
            onTriggered: fileOps.showInFolder(trackMenu.targetTrackId)
        }
        SystemMenuItem {
            objectName: "libraryTrackCopyPath"
            text: qsTr("复制文件路径")
            enabled: trackMenu.targetTrackIds.length === 1
            onTriggered: fileOps.copyPath(trackMenu.targetTrackId)
        }
        SystemMenuItem {
            objectName: "libraryTrackTag"
            text: qsTr("打标签")
            onTriggered: tagTrackDialog.open()
        }
        SystemMenuItem {
            objectName: "libraryTrackRename"
            text: qsTr("重命名")
            enabled: trackMenu.targetTrackIds.length === 1
            onTriggered: renameTrackDialog.open()
        }
        SystemMenuItem {
            objectName: "libraryTrackMoveFile"
            text: qsTr("移动到指定文件夹")
            onTriggered: moveTrackFolderDialog.open()
        }
        SystemMenuItem {
            objectName: "libraryTrackCopyFile"
            text: qsTr("复制到指定文件夹")
            onTriggered: copyTrackFolderDialog.open()
        }
        MenuSeparator {}
        SystemMenuItem {
            objectName: "libraryTrackRemove"
            text: qsTr("从列表删除")
            onTriggered: {
                for (var index = 0; index < trackMenu.targetTrackIds.length;
                     ++index)
                    LibraryModel.removeTrack(trackMenu.targetTrackIds[index])
                root.selectedTrackIds = []
                root.selectedTrackId = ""
            }
        }
        SystemMenuItem {
            objectName: "libraryTrackTrash"
            text: qsTr("彻底删除至回收站")
            onTriggered: trashTrackDialog.open()
        }
        SystemMenuItem {
            objectName: "libraryTrackRelocate"
            text: qsTr("重新定位文件")
            enabled: trackMenu.targetTrackIds.length === 1
            onTriggered: relocateTrackDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            spacing: 10
            Text {
                text: qsTr("曲库管理")
                color: Theme.primaryText
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Item { Layout.preferredWidth: 18 }
            TextField {
                Layout.fillWidth: true
                Layout.maximumWidth: 430
                placeholderText: qsTr("搜索歌曲、艺术家、专辑或文件夹")
                text: root.searchText
                onTextChanged: {
                    root.searchText = text
                    manager.currentPage = 0
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("添加文件夹")
                icon.source: Theme.icon("folder-add-line")
                onClicked: folderDialog.open()
            }
            ActionButton {
                highlighted: true
                text: manager.scanning ? qsTr("停止扫描") : qsTr("扫描整理")
                icon.source: Theme.icon(manager.scanning ? "pause-fill" : "arrow-go-forward-line")
                onClicked: manager.scanning ? manager.cancelScan() : manager.rescan()
            }
            ActionButton {
                objectName: "libraryBackupButton"
                text: qsTr("备份/导入")
                icon.source: Theme.icon("file-copy-line")
                onClicked: backupMenu.popup()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 6
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: [
                    {id: "all", t: qsTr("总歌曲量"), v: manager.totalCount, icon: "music-2-fill", c: Theme.violet},
                    {id: "storage", t: qsTr("占用大小"), v: root.formatBytes(manager.totalBytes), icon: "folder-open-line", c: Theme.waveformBlue},
                    {id: "missing", t: qsTr("丢失文件"), v: manager.missingCount, icon: "information-line", c: Theme.favoriteRed},
                    {id: "duplicates", t: qsTr("重复歌曲"), v: manager.duplicateCount, icon: "file-copy-line", c: Theme.ratingGold},
                    {id: "uncovered", t: qsTr("无封面"), v: manager.uncoveredCount, icon: "picture-in-picture-2-line", c: Theme.secondaryText},
                    {id: "untagged", t: qsTr("无标签"), v: manager.untaggedCount, icon: "checkbox-blank-line", c: "#36d56a"},
                    {id: "recentAdded", t: qsTr("最近添加"), v: manager.recentAddedCount, icon: "add-line", c: Theme.violet},
                    {id: "recentPlayed", t: qsTr("最近播放"), v: manager.recentPlayedCount, icon: "play-fill", c: Theme.waveformBlue},
                    {id: "highFrequency", t: qsTr("高频播放"), v: manager.highFrequencyCount, icon: "equalizer-line", c: "#19cbd1"},
                    {id: "lowFrequency", t: qsTr("低频播放"), v: manager.lowFrequencyCount, icon: "equalizer-line", c: "#19cbd1"},
                    {id: "neverPlayed", t: qsTr("从未播放"), v: manager.neverPlayedCount, icon: "time-line", c: Theme.ratingGold},
                    {id: "unrated", t: qsTr("未评分"), v: manager.unratedCount, icon: "star-line", c: Theme.violet}
                ]
                delegate: Surface {
                    required property var modelData
                    objectName: "librarySummaryCard-" + modelData.id
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: manager.activeCategory === modelData.id
                           ? Theme.activeSelection : Theme.panel
                    border.color: manager.activeCategory === modelData.id
                                  ? Theme.accent : Theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 7
                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 8
                            color: Qt.rgba(modelData.c.r, modelData.c.g, modelData.c.b, 0.14)
                            ThemedIcon {
                                anchors.centerIn: parent
                                width: 16
                                height: 16
                                source: Theme.icon(modelData.icon)
                                tint: modelData.c
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { text: modelData.t; color: Theme.secondaryText; font.pixelSize: 10 }
                            Text {
                                text: modelData.v
                                color: Theme.primaryText
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: manager.activeCategory = modelData.id
                    }
                }
            }
        }

        Surface {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 18
                ColumnLayout {
                    Layout.preferredWidth: 235
                    spacing: 3
                    Text { text: qsTr("文件夹监控"); color: Theme.secondaryText; font.pixelSize: 11 }
                    ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("正在监控 %1 个文件夹").arg(manager.monitoredFolders.length)]
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 170
                    spacing: 3
                    Text { text: qsTr("格式筛选"); color: Theme.secondaryText; font.pixelSize: 11 }
                    ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("全部格式"), "MP3", "WAV", "FLAC", "AAC", "M4A", "OGG"]
                        onActivated: {
                            root.formatFilter = currentIndex === 0 ? "" : currentText
                            manager.currentPage = 0
                        }
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 236
                    spacing: 3
                    Text { text: qsTr("BPM筛选"); color: Theme.secondaryText; font.pixelSize: 11 }
                    RowLayout {
                        spacing: 6
                        Label { text: root.minBpm; color: Theme.primaryText; Layout.preferredWidth: 24 }
                        RangeSlider {
                            id: bpmRange
                            objectName: "libraryManagerBpmRange"
                            Layout.preferredWidth: 150
                            from: 60
                            to: 160
                            stepSize: 1
                            first.value: root.minBpm
                            second.value: root.maxBpm
                            first.onMoved: { root.minBpm = Math.round(first.value); manager.currentPage = 0 }
                            second.onMoved: { root.maxBpm = Math.round(second.value); manager.currentPage = 0 }
                            first.handle: Rectangle {
                                x: bpmRange.leftPadding + bpmRange.first.visualPosition * (bpmRange.availableWidth - width)
                                y: bpmRange.topPadding + bpmRange.availableHeight / 2 - height / 2
                                width: 12; height: 12; radius: 6
                                color: Theme.primaryText; border.color: Theme.accent
                            }
                            second.handle: Rectangle {
                                x: bpmRange.leftPadding + bpmRange.second.visualPosition * (bpmRange.availableWidth - width)
                                y: bpmRange.topPadding + bpmRange.availableHeight / 2 - height / 2
                                width: 12; height: 12; radius: 6
                                color: Theme.primaryText; border.color: Theme.accent
                            }
                        }
                        Label { text: root.maxBpm; color: Theme.primaryText; Layout.preferredWidth: 26 }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text { text: qsTr("星级收藏"); color: Theme.secondaryText; font.pixelSize: 11 }
                    Row {
                        spacing: 2
                        Repeater {
                            model: 5
                            ToolButton {
                                required property int index
                                width: 30
                                height: 30
                                icon.source: Theme.icon(index < root.exactRating ? "star-fill" : "star-line")
                                icon.color: index < root.exactRating ? Theme.ratingGold : Theme.secondaryText
                                onClicked: {
                                    root.exactRating = root.exactRating === index + 1 ? 0 : index + 1
                                    manager.currentPage = 0
                                }
                            }
                        }
                    }
                }
                ActionButton {
                    text: qsTr("清空")
                    onClicked: {
                        root.searchText = ""
                        root.formatFilter = ""
                        root.minBpm = 60
                        root.maxBpm = 160
                        root.exactRating = 0
                        manager.currentPage = 0
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Surface {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: Theme.elevated
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 0
                            CheckBox {
                                Layout.preferredWidth: 26
                                scale: 0.8
                                onToggled: {
                                    if (!checked) root.selectedTrackIds = []
                                    else {
                                        var ids = []
                                        for (var i = 0; i < manager.rowCount(); ++i)
                                            ids.push(manager.data(manager.index(i, 0), LibraryManagerController.TrackIdRole))
                                        root.selectedTrackIds = ids
                                    }
                                }
                            }
                            Text { text: "#"; color: Theme.secondaryText; Layout.preferredWidth: 28 }
                            Text { text: qsTr("歌曲"); color: Theme.secondaryText; Layout.fillWidth: true; Layout.minimumWidth: 170 }
                            Text { text: qsTr("艺术家"); color: Theme.secondaryText; Layout.preferredWidth: 90 }
                            Text { text: qsTr("专辑"); color: Theme.secondaryText; Layout.preferredWidth: 100; visible: root.width >= 1080 }
                            Text { text: qsTr("收藏"); color: Theme.secondaryText; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 36 }
                            Text { text: qsTr("评分"); color: Theme.secondaryText; Layout.preferredWidth: 90 }
                            Text { text: "BPM"; color: Theme.secondaryText; Layout.preferredWidth: 48 }
                            Text { text: qsTr("时长"); color: Theme.secondaryText; Layout.preferredWidth: 56 }
                            Text { text: qsTr("状态"); color: Theme.secondaryText; Layout.preferredWidth: 70 }
                        }
                    }
                    ListView {
                        id: trackView
                        objectName: "libraryManagerTrackList"
                        Layout.fillWidth: true
                        // Keep the default management viewport at ten complete
                        // rows.  Additional tracks remain scrollable instead of
                        // silently increasing the first-open window density.
                        Layout.fillHeight: false
                        Layout.preferredHeight: root.preferredTrackViewportHeight
                        Layout.minimumHeight: root.preferredTrackViewportHeight
                        Layout.maximumHeight: root.preferredTrackViewportHeight
                        clip: true
                        focus: true
                        model: manager
                        boundsBehavior: Flickable.StopAtBounds
                        Keys.onDeletePressed: {
                            for (var i = 0; i < root.selectedTrackIds.length; ++i)
                                LibraryModel.removeTrack(root.selectedTrackIds[i])
                            root.selectedTrackIds = []
                            root.selectedTrackId = ""
                        }
                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property string trackId
                            required property string title
                            required property string artist
                            required property string album
                            required property string format
                            required property string status
                            required property string duplicateGroup
                            required property bool favorite
                            required property int rating
                            required property real bpm
                            required property double durationMs
                            width: ListView.view.width
                            height: root.trackRowHeight
                            color: root.selectedTrackIds.indexOf(trackId) >= 0
                                   ? Qt.rgba(0.45, 0.2, 0.85, 0.32)
                                   : rowHover.hovered ? Theme.hoverSurface : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 0
                                CheckBox {
                                    Layout.preferredWidth: 26
                                    scale: 0.8
                                    checked: root.selectedTrackIds.indexOf(trackId) >= 0
                                    onClicked: root.toggleSelection(trackId, true)
                                }
                                Text { text: manager.currentPage * manager.pageSize + index + 1; color: Theme.secondaryText; Layout.preferredWidth: 28 }
                                Text { text: title || qsTr("未知歌曲"); color: Theme.primaryText; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 170 }
                                Text { text: artist || "—"; color: Theme.secondaryText; elide: Text.ElideRight; Layout.preferredWidth: 90 }
                                Text { text: album || "—"; color: Theme.secondaryText; elide: Text.ElideRight; Layout.preferredWidth: 100; visible: root.width >= 1080 }
                                ToolButton {
                                    objectName: "libraryTrackFavorite"
                                    Layout.preferredWidth: 36
                                    icon.source: Theme.icon(favorite ? "heart-fill" : "heart-line")
                                    icon.color: favorite ? Theme.favoriteRed : Theme.secondaryText
                                    icon.width: 17; icon.height: 17
                                    onClicked: {
                                        var libraryRow = LibraryModel.indexForTrackId(trackId)
                                        if (libraryRow >= 0)
                                            LibraryModel.setFavorite(libraryRow, !favorite)
                                    }
                                    background: null
                                }
                                Row {
                                    Layout.preferredWidth: 90
                                    spacing: 0
                                    Repeater {
                                        model: 5
                                        ToolButton {
                                            required property int index
                                            objectName: "libraryTrackRatingStar"
                                            width: 17
                                            height: 17
                                            icon.source: Theme.icon(index < row.rating ? "star-fill" : "star-line")
                                            icon.color: index < row.rating
                                                        ? Theme.ratingGold : Theme.secondaryText
                                            icon.width: 17
                                            icon.height: 17
                                            padding: 0
                                            background: null
                                            onClicked: {
                                                var libraryRow = LibraryModel.indexForTrackId(trackId)
                                                if (libraryRow >= 0)
                                                    LibraryModel.setRating(
                                                        libraryRow,
                                                        row.rating === index + 1 ? 0 : index + 1)
                                            }
                                        }
                                    }
                                }
                                Text { text: bpm > 0 ? Math.round(bpm) : "—"; color: Theme.secondaryText; Layout.preferredWidth: 48 }
                                Text { text: root.formatDuration(durationMs); color: Theme.secondaryText; Layout.preferredWidth: 56 }
                                Text {
                                    text: status === "missing" ? qsTr("丢失文件")
                                        : duplicateGroup.length > 0 ? qsTr("重复歌曲") : qsTr("正常")
                                    color: status === "missing" ? Theme.favoriteRed
                                        : duplicateGroup.length > 0 ? Theme.ratingGold : "#39d66d"
                                    Layout.preferredWidth: 70
                                }
                            }
                            HoverHandler { id: rowHover }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                grabPermissions: PointerHandler.TakeOverForbidden
                                onTapped: function(eventPoint, button) {
                                    root.toggleSelection(row.trackId,
                                        Boolean(eventPoint.modifiers & Qt.ControlModifier))
                                    trackView.forceActiveFocus()
                                }
                                onDoubleTapped: {
                                    LibraryModel.playRow(LibraryModel.indexForTrackId(row.trackId))
                                }
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: root.openTrackMenu(row.trackId)
                            }
                        }
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        color: Theme.elevated
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            Text {
                                text: qsTr("已选择 %1 首歌曲").arg(root.selectedTrackIds.length)
                                color: Theme.secondaryText
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("共 %1 首歌曲 / %2").arg(manager.filteredCount)
                                      .arg(root.formatBytes(manager.totalBytes))
                                color: Theme.secondaryText
                            }
                            ToolButton {
                                enabled: manager.currentPage > 0
                                icon.source: Theme.icon("skip-back-fill")
                                onClicked: manager.currentPage = manager.currentPage - 1
                            }
                            Label {
                                text: (manager.currentPage + 1) + " / " + root.pageCount
                                color: Theme.primaryText
                            }
                            ToolButton {
                                enabled: manager.currentPage + 1 < root.pageCount
                                icon.source: Theme.icon("skip-forward-fill")
                                onClicked: manager.currentPage = manager.currentPage + 1
                            }
                        }
                    }
                }
            }

            Surface {
                objectName: "libraryTrackDetailsPanel"
                Layout.preferredWidth: 176
                Layout.fillHeight: true
                ColumnLayout {
                    id: detailsContent
                    objectName: "libraryTrackDetailsContent"
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7
                    Text { text: qsTr("歌曲信息"); color: Theme.primaryText; font.weight: Font.DemiBold }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: width
                        radius: Theme.radiusSm
                        color: Theme.elevated
                        Image {
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectCrop
                            source: root.selectedTrack.coverUrl || ""
                            visible: source.toString().length > 0
                        }
                        ThemedIcon {
                            anchors.centerIn: parent
                            width: 44; height: 44
                            visible: !parent.children[0].visible
                            source: Theme.icon("music-2-fill")
                            tint: Theme.secondaryText
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.selectedTrack.title || qsTr("未选择歌曲")
                        color: Theme.primaryText
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.selectedTrack.artist || "—"
                        color: Theme.secondaryText
                        elide: Text.ElideRight
                    }
                    Row {
                        spacing: 1
                        Repeater {
                            model: 5
                            ThemedIcon {
                                required property int index
                                width: 17; height: 17
                                source: Theme.icon(index < Number(root.selectedTrack.rating || 0)
                                                   ? "star-fill" : "star-line")
                                tint: index < Number(root.selectedTrack.rating || 0)
                                      ? Theme.ratingGold : Theme.secondaryText
                            }
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 4
                        rowSpacing: 5
                        Repeater {
                            model: [
                                qsTr("专辑"), root.selectedTrack.album || "—",
                                qsTr("BPM"), root.selectedTrack.bpm > 0 ? Math.round(root.selectedTrack.bpm) : "—",
                                qsTr("时长"), root.formatDuration(root.selectedTrack.durationMs || 0),
                                qsTr("采样率"), root.selectedTrack.sampleRate > 0 ? (root.selectedTrack.sampleRate / 1000).toFixed(1) + " kHz" : "—",
                                qsTr("类型"), root.selectedTrack.format || "—",
                                qsTr("码率"), root.selectedTrack.bitRate > 0 ? Math.round(root.selectedTrack.bitRate / 1000) + " kbps" : "—"
                            ]
                            delegate: Text {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: index % 2 === 1
                                objectName: index === 9 ? "libraryDetailsFormat"
                                            : index === 11 ? "libraryDetailsBitrate" : ""
                                text: modelData
                                color: index % 2 === 0 ? Theme.secondaryText : Theme.primaryText
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            visible: manager.scanning
            from: 0
            to: 100
            value: manager.progress
        }
    }
}
