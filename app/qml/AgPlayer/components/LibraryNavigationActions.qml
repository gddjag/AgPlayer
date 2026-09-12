import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import AgPlayer

// Owned by the main window, not the replaceable shell: an import may finish
// after switching themes and must still reach its original target playlist.
Item {
    id: root
    property var navigation: null
    property var filterModel: null
    property var playlistModel: PlaylistModel
    property var hostWindow: null
    property string importTargetPlaylistId: ""
    property bool importBatchActive: false
    property var activeImportDialog: null
    property string exportPlaylistId: ""

    function enterPlaylist(id) {
        if (!id) return
        LibraryNavigationModel.setExpanded("library:all", true)
        filterModel.category = id
        filterModel.tagKey = ""
        TagModel.selectedKey = ""
        filterModel.resourceFolder = ""
        Qt.callLater(function() {
            if (root.navigation) root.navigation.revealNode("playlist:" + id)
        })
    }

    function importUrls(urls) {
        if (!urls || !urls.length || ImportController.busy || importBatchActive)
            return false
        importBatchActive = true
        ImportController.importUrls(urls)
        return true
    }

    function trackPaths(id) {
        var paths = ({})
        var ids = playlistModel.trackIdsForPlaylist(id)
        for (var i = 0; i < ids.length; ++i) {
            var track = LibraryModel.trackForId(ids[i])
            if (track && track.path) paths[ids[i]] = track.path
        }
        return paths
    }

    function openFileDialog(component) {
        var dialog = component.createObject(root)
        if (dialog) dialog.open()
    }

    Connections {
        target: root.navigation
        function onCreatePlaylistRequested() { createDialog.open() }
        function onRenamePlaylistRequested(id) {
            renameDialog.playlistId = id
            renameField.text = root.playlistModel.nameForId(id)
            renameDialog.open()
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        function onRemovePlaylistRequested(id) {
            removeDialog.playlistId = id
            removeDialog.open()
        }
        function onImportRequested(id) {
            if (ImportController.busy || root.importBatchActive || root.activeImportDialog) return
            root.importTargetPlaylistId = id || ""
            if (id) root.enterPlaylist(id)
            var dialog = audioDialogComponent.createObject(root)
            root.activeImportDialog = dialog
            if (dialog) dialog.open()
            else root.importTargetPlaylistId = ""
        }
        function onImportPlaylistRequested() {
            if (!ImportController.busy && !root.importBatchActive)
                root.openFileDialog(importPlaylistDialogComponent)
        }
        function onExportPlaylistRequested(id) {
            root.exportPlaylistId = id
            copyFiles.checked = false
            exportOptions.open()
        }
        function onResourceUrlsDropped(urls) {
            var accepted = false
            for (var i = 0; i < urls.length; ++i) {
                var entry = ResourceFolderController.classifyDropUrl(urls[i])
                if (entry.kind === ResourceFolderController.Directory
                        && ResourceFolderController.addMonitoredFolder(entry.path))
                    accepted = true
            }
            root.navigation.resourceDropAccepted = accepted
        }
        function onResourceFolderRemoved(folder) {
            if (root.filterModel.resourceFolder
                    && ResourceFolderController.pathIsWithin(root.filterModel.resourceFolder, folder)) {
                root.filterModel.resourceFolder = ""
                root.filterModel.category = "all"
                root.navigation.activeNodeType = "library"
            }
        }
    }

    Connections {
        target: ImportController
        function onFinished() {
            if (!root.importBatchActive) return
            if (root.importTargetPlaylistId && ImportController.importedTrackIds.length)
                root.playlistModel.addTracks(root.importTargetPlaylistId, ImportController.importedTrackIds)
            root.importTargetPlaylistId = ""
            root.importBatchActive = false
        }
    }

    component PlaylistDialog: ThemedDialog {
        parent: root.hostWindow ? root.hostWindow.contentItem : root
        anchors.centerIn: parent
        implicitWidth: Math.max(280, implicitContentWidth + leftPadding + rightPadding,
                                implicitHeaderWidth, implicitFooterWidth)
        width: Math.min(implicitWidth, parent.width - 24)
        standardButtons: Dialog.Ok | Dialog.Cancel
    }

    PlaylistDialog {
        id: createDialog
        objectName: "createPlaylistDialog"
        title: qsTranslate("ListWindow", "新建歌单")
        onOpened: { createField.clear(); createField.forceActiveFocus() }
        onAccepted: root.enterPlaylist(root.playlistModel.createPlaylist(createField.text))
        contentItem: ThemedTextField {
            id: createField
            objectName: "createPlaylistField"
            placeholderText: qsTranslate("ListWindow", "歌单名称")
        }
    }
    PlaylistDialog {
        id: renameDialog
        objectName: "renamePlaylistDialog"
        property string playlistId: ""
        title: qsTranslate("ListWindow", "重命名歌单")
        onAccepted: root.playlistModel.renamePlaylist(playlistId, renameField.text)
        contentItem: ThemedTextField { id: renameField; objectName: "renamePlaylistField" }
    }
    PlaylistDialog {
        id: removeDialog
        objectName: "removePlaylistDialog"
        property string playlistId: ""
        width: Math.min(420, parent.width - 24)
        title: qsTranslate("ListWindow", "删除歌单")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            if (root.playlistModel.removePlaylist(playlistId)
                    && root.filterModel.category === playlistId)
                root.filterModel.category = "all"
        }
        contentItem: Label {
            text: qsTranslate("ListWindow", "确定删除这个歌单？音乐文件不会被删除。")
            color: Theme.primaryText
            wrapMode: Text.Wrap
        }
    }
    Component {
        id: audioDialogComponent
        FileDialog {
            id: audioDialog
            objectName: "importAudioDialog"
            parentWindow: root.hostWindow
            fileMode: FileDialog.OpenFiles
            nameFilters: [ResourceFolderController.audioFileNameFilter]
            onAccepted: {
                if (!root.importUrls(selectedFiles) && !root.importBatchActive)
                    root.importTargetPlaylistId = ""
                if (root.activeImportDialog === audioDialog)
                    root.activeImportDialog = null
                Qt.callLater(destroy)
            }
            onRejected: {
                if (root.activeImportDialog === audioDialog) {
                    if (!root.importBatchActive) root.importTargetPlaylistId = ""
                    root.activeImportDialog = null
                }
                Qt.callLater(destroy)
            }
        }
    }
    Component {
        id: importPlaylistDialogComponent
        FileDialog {
            objectName: "importPlaylistDialog"
            parentWindow: root.hostWindow
            title: qsTranslate("ListWindow", "导入歌单")
            fileMode: FileDialog.OpenFile
            nameFilters: ["Playlist files (*.m3u *.m3u8 *.pls *.json)"]
            onAccepted: {
                var paths = root.playlistModel.pathsFromPlaylist(selectedFile.toString())
                var id = root.playlistModel.importPlaylist(selectedFile.toString())
                if (id) {
                    root.enterPlaylist(id)
                    ImportController.importPaths(paths)
                }
                Qt.callLater(destroy)
            }
            onRejected: Qt.callLater(destroy)
        }
    }
    PlaylistDialog {
        id: exportOptions
        objectName: "exportOptionsDialog"
        title: qsTranslate("ListWindow", "导出歌单")
        onAccepted: root.openFileDialog(exportDialogComponent)
        contentItem: ThemedCheckBox {
            id: copyFiles
            text: qsTranslate("ListWindow", "同时复制歌曲文件")
        }
    }
    Component {
        id: exportDialogComponent
        FileDialog {
            parentWindow: root.hostWindow
            title: qsTranslate("ListWindow", "导出歌单")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "m3u8"
            nameFilters: ["M3U8 playlist (*.m3u8)", "AgPlayer playlist (*.json)", "PLS playlist (*.pls)"]
            onAccepted: {
                root.playlistModel.exportPlaylist(root.exportPlaylistId,
                            selectedFile.toString(), root.trackPaths(root.exportPlaylistId), copyFiles.checked)
                Qt.callLater(destroy)
            }
            onRejected: Qt.callLater(destroy)
        }
    }
}
