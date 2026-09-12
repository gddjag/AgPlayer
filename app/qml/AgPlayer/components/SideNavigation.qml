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
    // Classic keeps the existing tag-management route.  Integrated renders
    // the same TagManagementPanel persistently in its right column instead.
    property bool showTagManagementEntry: true
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
    property bool resourceDropAccepted: false
    readonly property int navigationRowHeight: Theme.navigationRowHeight
    readonly property int resourceSectionHeight: 54
    readonly property int navigationIconVisualSize:
        Theme.navigationIconVisualSize
    readonly property int navigationActionExtent:
        Theme.navigationActionExtent

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
        resourceDropAccepted = false
        resourceUrlsDropped(urls)
        return resourceDropAccepted
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
            if (type === "tags" && !showTagManagementEntry)
                continue
            if (type === "resourceSection") {
                var position = navigationList.contentItem.mapToItem(
                            root, 0, sectionContentY)
                return Math.max(listTop, Math.min(listBottom, position.y))
            }
            sectionContentY += (type === "resourceSection"
                                ? resourceSectionHeight
                                : navigationRowHeight) + navigationList.spacing
        }
        return listBottom
    }

    function revealNode(nodeId) {
        for (var row = 0; row < navigationList.count; ++row) {
            var modelIndex = navigationModel.index(row, 0)
            if (navigationModel.data(
                        modelIndex, LibraryNavigationModel.NodeIdRole)
                    === nodeId) {
                navigationList.positionViewAtIndex(row, ListView.Contain)
                return true
            }
        }
        return false
    }

    function resourceNodeAt(x, y) {
        var contentPoint = navigationList.contentItem.mapFromItem(
                    navigationList, x, y)
        var row = navigationList.indexAt(contentPoint.x, contentPoint.y)
        if (row < 0)
            return null
        var modelIndex = navigationModel.index(row, 0)
        var nodeType = navigationModel.data(
                    modelIndex, LibraryNavigationModel.NodeTypeRole)
        if (nodeType !== "resourceRoot" && nodeType !== "resourceFolder")
            return null
        var depth = navigationModel.data(
                    modelIndex, LibraryNavigationModel.DepthRole)
        var hasChildren = navigationModel.data(
                    modelIndex, LibraryNavigationModel.HasChildrenRole)
        var expandLeft = 6 + depth * 12
        if (hasChildren && contentPoint.x >= expandLeft
                && contentPoint.x < expandLeft + navigationActionExtent)
            return null
        return {
            "nodeType": nodeType,
            "nodeId": navigationModel.data(
                modelIndex, LibraryNavigationModel.NodeIdRole),
            "resourceFolder": navigationModel.data(
                modelIndex, LibraryNavigationModel.ResourceFolderRole)
        }
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
        if (nodeType === "favorites") return "heart-fill"
        if (nodeType === "history") return "time-line"
        if (nodeType === "recentAdded") return "add-line"
        if (nodeType === "neverPlayed") return "time-line"
        if (nodeType === "playlist") return "list-unordered"
        if (nodeType === "resourceRoot" || nodeType === "resourceFolder")
            return "folder-open-line"
        if (nodeType === "tags") return "price-tag-3-line"
        return "music-2-line"
    }

    function suppliedIconForNode(nodeType) {
        if (nodeType === "library") return "user-library"
        if (nodeType === "playlist") return "user-playlist"
        if (nodeType === "resourceRoot") return "user-resource-root-red"
        if (nodeType === "resourceFolder") return "user-resource-subfolder"
        if (nodeType === "tags") return "user-tag"
        return ""
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
            onTriggered: ResourceFolderController.rescan()
        }
    }

    FolderDialog {
        id: addResourceFolderDialog
        objectName: "addResourceFolderDialog"
        title: qsTr("添加资源文件夹")
        onAccepted: root.navigationModel.addResourceFolder(selectedFolder)
    }

    ThemedDialog {
        id: removeResourceFolderDialog
        objectName: "removeResourceFolderDialog"
        title: qsTr("移除资源文件夹")
        modal: true
        width: 360
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

        // Resource topology updates reset the model.  Keep resource selection
        // on the stable view so a reset between press and release cannot
        // destroy the handler that owns the gesture.
        TapHandler {
            property var pressedResourceNode: null
            acceptedButtons: Qt.LeftButton
            onPressedChanged: {
                if (pressed)
                    pressedResourceNode = root.resourceNodeAt(
                                point.position.x, point.position.y)
            }
            onTapped: function(eventPoint) {
                var releasedNode = root.resourceNodeAt(
                            eventPoint.position.x, eventPoint.position.y)
                var pressedNode = pressedResourceNode
                pressedResourceNode = null
                if (!pressedNode || !releasedNode
                        || pressedNode.nodeId !== releasedNode.nodeId)
                    return
                root.activateNode(pressedNode.nodeType, pressedNode.nodeId,
                                  pressedNode.resourceFolder)
            }
            onCanceled: pressedResourceNode = null
        }

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
            readonly property int nodeIconVisualSize:
                nodeType === "library" ? root.navigationIconVisualSize + 2
                                         : root.navigationIconVisualSize

            readonly property bool selected: root.nodeIsSelected(
                                                 nodeType, nodeId,
                                                 resourceFolder)
            property real dropLoadPulse: 0
            width: navigationList.width
            visible: root.showTagManagementEntry || nodeType !== "tags"
            height: !visible ? 0 : nodeType === "resourceSection"
                    ? root.resourceSectionHeight : root.navigationRowHeight
            radius: nodeType === "resourceSection" ? 0 : Theme.radiusSm
            color: nodeType === "resourceSection" ? "transparent"
                   : selected ? Theme.listSelectedSurface
                   : nodeHover.hovered ? Theme.hoverSurface : "transparent"
            objectName: !visible ? "" : nodeType === "resourceSection"
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
                    Image {
                        source: Theme.icon("user-resource-folder")
                        sourceSize.width: root.navigationIconVisualSize
                        sourceSize.height: root.navigationIconVisualSize
                        fillMode: Image.PreserveAspectFit
                        Layout.preferredWidth: root.navigationIconVisualSize
                        Layout.preferredHeight: root.navigationIconVisualSize
                    }
                    Text {
                        text: qsTr("资源文件夹")
                        color: Theme.tagSecondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        objectName: nodeRow.nodeType === "resourceSection"
                                    ? "addResourceFolderButton" : ""
                        Layout.preferredWidth: root.navigationActionExtent
                        Layout.preferredHeight: root.navigationActionExtent
                        icon.source: Theme.icon("user-add-resource-folder")
                        contentItem: Image {
                            source: Theme.icon("user-add-resource-folder")
                            sourceSize.width: root.navigationIconVisualSize
                            sourceSize.height: root.navigationIconVisualSize
                            fillMode: Image.PreserveAspectFit
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("添加资源文件夹")
                        onClicked: addResourceFolderDialog.open()
                        background: null
                    }
                    ToolButton {
                        objectName: nodeRow.nodeType === "resourceSection"
                                    ? "removeResourceFolderButton" : ""
                        Layout.preferredWidth: root.navigationActionExtent
                        Layout.preferredHeight: root.navigationActionExtent
                        icon.source: Theme.icon("subtract-line")
                        icon.color: enabled ? Theme.iconSecondary
                                            : Theme.secondaryText
                        icon.width: root.navigationIconVisualSize
                        icon.height: root.navigationIconVisualSize
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
                anchors.leftMargin: 6 + nodeRow.depth * 12
                anchors.rightMargin: 8
                spacing: 4
                visible: nodeRow.nodeType !== "resourceSection"
                z: 4

                ToolButton {
                    objectName: "navigationExpandButton"
                    visible: nodeRow.nodeType === "library"
                             || ((nodeRow.nodeType === "resourceRoot"
                                  || nodeRow.nodeType === "resourceFolder")
                                 && nodeRow.hasChildren)
                    Layout.preferredWidth: visible
                                           ? root.navigationActionExtent : 0
                    Layout.preferredHeight: root.navigationActionExtent
                    icon.source: Theme.icon(nodeRow.expanded
                                            ? "arrow-down-s-line"
                                            : "arrow-right-s-line")
                    icon.color: Theme.tagSecondaryText
                    icon.width: root.navigationIconVisualSize
                    icon.height: root.navigationIconVisualSize
                    background: null
                    onClicked: root.navigationModel.setExpanded(
                                   nodeRow.nodeId, !nodeRow.expanded)
                }
                Item {
                    visible: !(nodeRow.nodeType === "library"
                                || ((nodeRow.nodeType === "resourceRoot"
                                || nodeRow.nodeType === "resourceFolder")
                               && nodeRow.hasChildren))
                    Layout.preferredWidth: visible
                                           ? root.navigationActionExtent : 0
                    Layout.preferredHeight: 1
                }
                Image {
                    objectName: "suppliedNodeIcon-" + nodeRow.nodeType
                    visible: root.suppliedIconForNode(nodeRow.nodeType) !== ""
                    source: visible
                            ? Theme.icon(root.suppliedIconForNode(nodeRow.nodeType))
                            : ""
                    sourceSize.width: nodeRow.nodeIconVisualSize
                    sourceSize.height: nodeRow.nodeIconVisualSize
                    fillMode: Image.PreserveAspectFit
                    Layout.preferredWidth: visible
                                           ? nodeRow.nodeIconVisualSize : 0
                    Layout.preferredHeight: nodeRow.nodeIconVisualSize
                }
                ThemedIcon {
                    visible: root.suppliedIconForNode(nodeRow.nodeType) === ""
                    source: Theme.icon(root.iconForNode(nodeRow.nodeType))
                    tint: nodeRow.nodeType === "favorites" ? Theme.favoriteRed
                                                             : nodeRow.selected
                                                               ? Theme.iconAccent
                                                               : Theme.iconSecondary
                    sourceSize.width: root.navigationIconVisualSize
                    sourceSize.height: root.navigationIconVisualSize
                    Layout.preferredWidth: visible
                                           ? root.navigationIconVisualSize : 0
                    Layout.preferredHeight: root.navigationIconVisualSize
                }
                Text {
                    text: nodeRow.displayName
                    color: playlistDropTarget.containsDrag
                           ? Theme.primaryText
                           : nodeRow.selected ? Theme.primaryText
                                            : Theme.tagSecondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: nodeRow.count
                    color: Theme.tagSecondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
                }
            }

            HoverHandler { id: nodeHover }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                enabled: nodeRow.nodeType !== "resourceSection"
                         && nodeRow.nodeType !== "resourceRoot"
                         && nodeRow.nodeType !== "resourceFolder"
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
            Rectangle {
                objectName: nodeRow.nodeType === "playlist"
                            ? "playlistDropFeedback-"
                              + root.playlistIdForNode(nodeRow.nodeId) : ""
                anchors.fill: parent
                radius: nodeRow.radius
                visible: playlistDropTarget.containsDrag
                         || nodeRow.dropLoadPulse > 0
                color: playlistDropTarget.containsDrag
                       ? Theme.listSelectedSurface : "transparent"
                border.color: Theme.accent
                border.width: 1
                opacity: playlistDropTarget.containsDrag
                         ? 1 : nodeRow.dropLoadPulse
                scale: 1 - nodeRow.dropLoadPulse * 0.04
                z: 2
            }
            SequentialAnimation {
                id: playlistLoadAnimation
                NumberAnimation {
                    target: nodeRow
                    property: "dropLoadPulse"
                    from: 0
                    to: 1
                    duration: 90
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    target: nodeRow
                    property: "dropLoadPulse"
                    from: 1
                    to: 0
                    duration: 150
                    easing.type: Easing.InCubic
                }
            }
            DropArea {
                id: playlistDropTarget
                objectName: nodeRow.nodeType === "playlist"
                            ? "playlistDropTarget-"
                              + root.playlistIdForNode(nodeRow.nodeId) : ""
                property int acceptedAnimationCount: 0
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
                        var affected = 0
                        if (root.selectedCategory !== "all"
                                && root.selectedCategory !== "favorites"
                                && root.selectedCategory !== "history"
                                && root.selectedCategory !== "recentAdded"
                                && root.selectedCategory !== "neverPlayed"
                                && root.selectedCategory !== "library") {
                            affected = root.playlistModel.moveTracks(
                                        root.selectedCategory, targetId, ids)
                        } else {
                            affected = root.playlistModel.addTracks(targetId,
                                                                    ids)
                        }
                        if (affected > 0) {
                            acceptedAnimationCount += 1
                            playlistLoadAnimation.restart()
                            drop.acceptProposedAction()
                        }
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
            font.pixelSize: Theme.fontSizeBody
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
