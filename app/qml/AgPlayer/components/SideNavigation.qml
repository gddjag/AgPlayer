import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Item {
    id: root

    property var navigationModel: LibraryNavigationModel
    property var playlistModel: PlaylistModel
    property string selectedCategory: "all"
    property string selectedTagKey: ""
    property string selectedResourceFolder: ""
    property string activeNodeType: "library"
    property bool expanded: true
    property int allCount: 0
    property int favoriteCount: 0
    property int historyCount: 0
    property int recentAddedCount: 0
    property int neverPlayedCount: 0
    property string contextPlaylistId: ""
    property string contextResourceFolder: ""
    property bool contextResourceIsRoot: false
    property string pendingResourceFolderRemoval: ""
    property var resourceDropSubmitter: null

    signal categorySelected(string category)
    signal navigationSelected(string nodeType, string nodeId,
                              string resourceFolder)
    signal createPlaylistRequested()
    signal renamePlaylistRequested(string playlistId)
    signal removePlaylistRequested(string playlistId)
    signal importRequested(string playlistId)
    signal importPlaylistRequested()
    signal exportPlaylistRequested(string playlistId)
    signal resourceUrlsDropped(var urls)
    signal resourceFolderRemoved(string folder)

    function submitResourceUrls(urls) {
        if (typeof resourceDropSubmitter === "function")
            return resourceDropSubmitter(urls) === true
        resourceUrlsDropped(urls)
        return true
    }

    function resourceDropContainsPoint(x, y) {
        if (!resourceDropTarget.visible)
            return false
        var topLeft = resourceDropTarget.mapToItem(root, 0, 0)
        return x >= topLeft.x && y >= topLeft.y
                && x < topLeft.x + resourceDropTarget.width
                && y < topLeft.y + resourceDropTarget.height
    }

    function resourceSectionTop() {
        var listTop = navigationList.mapToItem(root, 0, 0).y
        var listBottom = listTop + navigationList.height
        var sectionContentY = 0
        for (var row = 0; row < navigationList.count; ++row) {
            var modelIndex = navigationModel.index(row, 0)
            var type = navigationModel.data(
                        modelIndex, LibraryNavigationModel.NodeTypeRole)
            if (type === "resourceSection") {
                var position = navigationList.contentItem.mapToItem(
                            root, 0, sectionContentY)
                return Math.max(listTop, Math.min(listBottom, position.y))
            }
            sectionContentY += 38 + navigationList.spacing
        }
        return listBottom
    }

    function confirmResourceFolderRemoval(folder) {
        if (!folder || folder.length === 0)
            return
        pendingResourceFolderRemoval = folder
        removeResourceFolderDialog.open()
    }

    function playlistIdForNode(nodeId) {
        var prefix = "playlist:"
        return nodeId.indexOf(prefix) === 0 ? nodeId.substring(prefix.length) : ""
    }

    function activateNode(nodeType, nodeId, resourceFolder) {
        activeNodeType = nodeType
        if (nodeType === "library")
            categorySelected("all")
        else if (nodeType === "favorites")
            categorySelected("favorites")
        else if (nodeType === "history")
            categorySelected("history")
        else if (nodeType === "recentAdded")
            categorySelected("recentAdded")
        else if (nodeType === "neverPlayed")
            categorySelected("neverPlayed")
        else if (nodeType === "playlist")
            categorySelected(playlistIdForNode(nodeId))
        navigationSelected(nodeType, nodeId, resourceFolder)
    }

    function iconForNode(nodeType) {
        if (nodeType === "favorites") return "heart-line"
        if (nodeType === "history") return "time-line"
        if (nodeType === "recentAdded") return "add-line"
        if (nodeType === "neverPlayed") return "time-line"
        if (nodeType === "playlist") return "list-unordered"
        if (nodeType === "resourceRoot" || nodeType === "resourceFolder")
            return "folder-open-line"
        if (nodeType === "tags") return "price-tag-3-line"
        return "music-2-line"
    }

    function nodeIsSelected(nodeType, nodeId, resourceFolder) {
        if (activeNodeType === "tags" || selectedTagKey.length > 0)
            return nodeType === "tags"
        if (activeNodeType === "resourceRoot"
                || activeNodeType === "resourceFolder"
                || selectedResourceFolder.length > 0) {
            if (nodeType !== "resourceRoot" && nodeType !== "resourceFolder")
                return false
            return selectedResourceFolder === resourceFolder
        }
        if (nodeType === "library") return selectedCategory === "all"
        if (nodeType === "favorites") return selectedCategory === "favorites"
        if (nodeType === "history") return selectedCategory === "history"
        if (nodeType === "recentAdded") return selectedCategory === "recentAdded"
        if (nodeType === "neverPlayed") return selectedCategory === "neverPlayed"
        if (nodeType === "playlist")
            return selectedCategory === playlistIdForNode(nodeId)
        return false
    }

    onSelectedTagKeyChanged: {
        if (selectedTagKey.length > 0)
            activeNodeType = "tags"
    }
    onSelectedResourceFolderChanged: {
        if (selectedResourceFolder.length > 0
                && activeNodeType !== "resourceRoot")
            activeNodeType = "resourceFolder"
    }
    onSelectedCategoryChanged: {
        if (selectedCategory === "favorites")
            activeNodeType = "favorites"
        else if (selectedCategory === "history")
            activeNodeType = "history"
        else if (selectedCategory === "recentAdded")
            activeNodeType = "recentAdded"
        else if (selectedCategory === "neverPlayed")
            activeNodeType = "neverPlayed"
        else if (selectedCategory !== "all" && selectedCategory !== "library")
            activeNodeType = "playlist"
    }

    Menu {
        id: playlistMenu
        objectName: "playlistContextMenu"
        width: 210
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
        SystemMenuItem {
            objectName: "playlistMenuCreate"
            text: qsTr("新建歌单")
            onTriggered: root.createPlaylistRequested()
        }
        SystemMenuItem {
            objectName: "playlistMenuImport"
            text: qsTr("导入音乐")
            onTriggered: root.importRequested(root.contextPlaylistId)
        }
        MenuSeparator {}
        SystemMenuItem { text: qsTr("导入歌单"); onTriggered: root.importPlaylistRequested() }
        SystemMenuItem {
            objectName: "playlistMenuExport"
            text: qsTr("导出歌单")
            enabled: root.contextPlaylistId.length > 0
            onTriggered: root.exportPlaylistRequested(root.contextPlaylistId)
        }
        MenuSeparator {}
        SystemMenuItem {
            objectName: "playlistMenuRename"
            text: qsTr("重命名")
            enabled: root.contextPlaylistId.length > 0
            onTriggered: root.renamePlaylistRequested(root.contextPlaylistId)
        }
        SystemMenuItem {
            objectName: "playlistMenuDelete"
            text: qsTr("删除歌单")
            enabled: root.contextPlaylistId.length > 0
            onTriggered: root.removePlaylistRequested(root.contextPlaylistId)
        }
    }

    Menu {
        id: resourceFolderMenu
        objectName: "resourceFolderContextMenu"
        width: 210
        palette.window: Theme.elevated
        palette.text: Theme.primaryText
        palette.highlight: Theme.activeSelection
        palette.highlightedText: Theme.activeSelectionText
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
        }
        SystemMenuItem {
            objectName: "resourceFolderMenuRemove"
            text: qsTr("移除文件夹引用")
            enabled: root.contextResourceIsRoot
            onTriggered: root.confirmResourceFolderRemoval(
                             root.contextResourceFolder)
        }
        SystemMenuItem {
            objectName: "resourceFolderMenuRescan"
            text: qsTr("重新扫描全部资源文件夹")
            onTriggered: LibraryManagerController.rescan()
        }
    }

    FolderDialog {
        id: addResourceFolderDialog
        objectName: "addResourceFolderDialog"
        title: qsTr("添加资源文件夹")
        onAccepted: root.navigationModel.addResourceFolder(selectedFolder)
    }

    Dialog {
        id: removeResourceFolderDialog
        objectName: "removeResourceFolderDialog"
        title: qsTr("移除资源文件夹")
        modal: true
        width: 450
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            var folder = root.pendingResourceFolderRemoval
            root.pendingResourceFolderRemoval = ""
            if (folder.length > 0
                    && root.navigationModel.removeResourceFolder(folder))
                root.resourceFolderRemoved(folder)
        }
        onRejected: root.pendingResourceFolderRemoval = ""
        contentItem: Label {
            objectName: "removeResourceFolderWarning"
            width: 410
            text: qsTr("只从 AgPlayer 移除此目录引用和监控，不删除电脑磁盘中的实际文件夹和音乐文件。")
            color: Theme.primaryText
            wrapMode: Text.Wrap
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }

    ListView {
        id: navigationList
        objectName: "libraryNavigationList"
        anchors.fill: parent
        anchors.margins: 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        cacheBuffer: 0
        spacing: 2
        model: root.navigationModel
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Rectangle {
            id: nodeRow
            required property int index
            required property string nodeId
            required property string nodeType
            required property int depth
            required property string displayName
            required property int count
            required property bool expanded
            required property string resourceFolder
            required property bool hasChildren

            readonly property bool selected: root.nodeIsSelected(
                                                 nodeType, nodeId,
                                                 resourceFolder)
            width: navigationList.width
            height: nodeType === "resourceSection" ? 54 : 38
            radius: nodeType === "resourceSection" ? 0 : Theme.radiusSm
            color: nodeType === "resourceSection" ? "transparent"
                   : selected ? Theme.listSelectedSurface
                   : nodeHover.hovered ? Theme.hoverSurface : "transparent"
            objectName: nodeType === "resourceSection"
                        ? "resourceFolderSection"
                        : nodeType === "playlist"
                        ? "playlistCategory-" + root.playlistIdForNode(nodeId)
                        : nodeType === "history" ? "historyCategoryButton"
                        : nodeType === "recentAdded" ? "recentAddedCategoryButton"
                        : nodeType === "neverPlayed" ? "neverPlayedCategoryButton"
                        : "navigationNode-" + nodeId

            Item {
                anchors.fill: parent
                visible: nodeRow.nodeType === "resourceSection"

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Theme.listDivider
                }
                RowLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 42
                    anchors.leftMargin: 8
                    anchors.rightMargin: 4
                    spacing: 4
                    Text {
                        text: qsTr("资源文件夹")
                        color: Theme.tagSecondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        objectName: nodeRow.nodeType === "resourceSection"
                                    ? "addResourceFolderButton" : ""
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        icon.source: Theme.icon("add-line")
                        icon.color: Theme.iconSecondary
                        icon.width: 16
                        icon.height: 16
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("添加资源文件夹")
                        onClicked: addResourceFolderDialog.open()
                        background: null
                    }
                    ToolButton {
                        objectName: nodeRow.nodeType === "resourceSection"
                                    ? "removeResourceFolderButton" : ""
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        icon.source: Theme.icon("subtract-line")
                        icon.color: enabled ? Theme.iconSecondary
                                            : Theme.secondaryText
                        icon.width: 16
                        icon.height: 16
                        enabled: root.activeNodeType === "resourceRoot"
                                 && root.selectedResourceFolder.length > 0
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("移除资源文件夹")
                        onClicked: root.confirmResourceFolderRemoval(
                                       root.selectedResourceFolder)
                        background: null
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8 + nodeRow.depth * 18
                anchors.rightMargin: 8
                spacing: 7
                visible: nodeRow.nodeType !== "resourceSection"

                ToolButton {
                    objectName: "navigationExpandButton"
                    visible: (nodeRow.nodeType === "library"
                              || nodeRow.nodeType === "resourceRoot"
                              || nodeRow.nodeType === "resourceFolder")
                             && nodeRow.hasChildren
                    Layout.preferredWidth: visible ? 20 : 0
                    Layout.preferredHeight: 28
                    icon.source: Theme.icon(nodeRow.expanded
                                            ? "arrow-down-s-line"
                                            : "arrow-right-s-line")
                    icon.color: Theme.tagSecondaryText
                    icon.width: 15
                    icon.height: 15
                    background: null
                    onClicked: root.navigationModel.setExpanded(
                                   nodeRow.nodeId, !nodeRow.expanded)
                }
                Item {
                    visible: !((nodeRow.nodeType === "library"
                                || nodeRow.nodeType === "resourceRoot"
                                || nodeRow.nodeType === "resourceFolder")
                               && nodeRow.hasChildren)
                    Layout.preferredWidth: visible ? 20 : 0
                    Layout.preferredHeight: 1
                }
                ThemedIcon {
                    source: Theme.icon(root.iconForNode(nodeRow.nodeType))
                    tint: nodeRow.selected ? Theme.iconAccent
                                           : Theme.iconSecondary
                    sourceSize.width: 17
                    sourceSize.height: 17
                    Layout.preferredWidth: 17
                    Layout.preferredHeight: 17
                }
                Text {
                    text: nodeRow.displayName
                    color: nodeRow.selected ? Theme.primaryText
                                            : Theme.tagSecondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Math.max(13, Qt.application.font.pixelSize)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: nodeRow.count
                    color: Theme.tagSecondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }
            }

            HoverHandler { id: nodeHover }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                enabled: nodeRow.nodeType !== "resourceSection"
                onTapped: root.activateNode(nodeRow.nodeType, nodeRow.nodeId,
                                            nodeRow.resourceFolder)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                enabled: nodeRow.nodeType !== "resourceSection"
                onTapped: {
                    if (nodeRow.nodeType === "playlist") {
                        root.contextPlaylistId = root.playlistIdForNode(
                                    nodeRow.nodeId)
                        playlistMenu.popup()
                    } else if (nodeRow.nodeType === "library") {
                        root.contextPlaylistId = ""
                        playlistMenu.popup()
                    } else if (nodeRow.nodeType === "resourceRoot"
                               || nodeRow.nodeType === "resourceFolder") {
                        root.contextResourceFolder = nodeRow.resourceFolder
                        root.contextResourceIsRoot =
                                nodeRow.nodeType === "resourceRoot"
                        resourceFolderMenu.popup()
                    }
                }
            }
            DropArea {
                objectName: nodeRow.nodeType === "playlist"
                            ? "playlistDropTarget-"
                              + root.playlistIdForNode(nodeRow.nodeId) : ""
                anchors.fill: parent
                enabled: nodeRow.nodeType === "playlist"
                keys: ["application/x-agplayer-track-ids"]
                onDropped: function(drop) {
                    var encoded = drop.getDataAsString(
                                "application/x-agplayer-track-ids")
                    var ids = encoded ? JSON.parse(encoded) : []
                    if (ids.length === 0 && drop.source
                            && drop.source.dragTrackIds)
                        ids = drop.source.dragTrackIds
                    var targetId = root.playlistIdForNode(nodeRow.nodeId)
                    if (ids.length > 0 && targetId.length > 0) {
                        if (root.selectedCategory !== "all"
                                && root.selectedCategory !== "favorites"
                                && root.selectedCategory !== "history"
                                && root.selectedCategory !== "recentAdded"
                                && root.selectedCategory !== "neverPlayed"
                                && root.selectedCategory !== "library") {
                            root.playlistModel.moveTracks(root.selectedCategory,
                                                          targetId, ids)
                        } else {
                            root.playlistModel.addTracks(targetId, ids)
                        }
                        drop.acceptProposedAction()
                    }
                }
            }
        }
    }

    FileDropArea {
        id: resourceDropTarget
        objectName: "resourceFolderDropTarget"
        anchors.left: navigationList.left
        anchors.right: navigationList.right
        y: {
            navigationList.contentY
            navigationList.count
            return root.resourceSectionTop()
        }
        height: Math.max(0, navigationList.y + navigationList.height - y)
        visible: height > 0
        z: -1
        urlsSubmitter: root.submitResourceUrls
    }

    component SystemMenuItem: MenuItem {
        id: systemMenuItem
        width: 200
        implicitWidth: 200
        implicitHeight: 34
        contentItem: Text {
            text: systemMenuItem.text
            color: systemMenuItem.highlighted || systemMenuItem.hovered
                   ? Theme.activeSelectionText
                   : systemMenuItem.enabled ? Theme.primaryText
                                            : Theme.secondaryText
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
}
