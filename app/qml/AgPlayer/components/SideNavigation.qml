import QtQuick
import QtQuick.Controls
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

    signal categorySelected(string category)
    signal navigationSelected(string nodeType, string nodeId,
                              string resourceFolder)
    signal createPlaylistRequested()
    signal renamePlaylistRequested(string playlistId)
    signal removePlaylistRequested(string playlistId)
    signal importRequested()
    signal importPlaylistRequested()
    signal exportPlaylistRequested(string playlistId)

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
        else if (nodeType === "playlist")
            categorySelected(playlistIdForNode(nodeId))
        navigationSelected(nodeType, nodeId, resourceFolder)
    }

    function iconForNode(nodeType) {
        if (nodeType === "favorites") return "heart-line"
        if (nodeType === "playlist") return "playlist-2-fill"
        if (nodeType === "resourceRoot" || nodeType === "resourceFolder")
            return "folder-open-line"
        if (nodeType === "tags") return "list-unordered"
        return "music-2-fill"
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
        if (nodeType === "playlist")
            return selectedCategory === playlistIdForNode(nodeId)
        return false
    }

    onSelectedTagKeyChanged: {
        if (selectedTagKey.length > 0)
            activeNodeType = "tags"
    }
    onSelectedResourceFolderChanged: {
        if (selectedResourceFolder.length > 0)
            activeNodeType = "resourceFolder"
    }
    onSelectedCategoryChanged: {
        if (selectedCategory === "favorites")
            activeNodeType = "favorites"
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
        SystemMenuItem { text: qsTr("新建歌单"); onTriggered: root.createPlaylistRequested() }
        SystemMenuItem { text: qsTr("导入音乐"); onTriggered: root.importRequested() }
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

            readonly property bool selected: root.nodeIsSelected(
                                                 nodeType, nodeId,
                                                 resourceFolder)
            width: navigationList.width
            height: 38
            radius: Theme.radiusSm
            color: selected ? Theme.listSelectedSurface
                            : nodeHover.hovered ? Theme.hoverSurface : "transparent"
            objectName: nodeType === "playlist"
                        ? "playlistCategory-" + root.playlistIdForNode(nodeId)
                        : "navigationNode-" + nodeId

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8 + nodeRow.depth * 18
                anchors.rightMargin: 8
                spacing: 7

                ToolButton {
                    objectName: "navigationExpandButton"
                    visible: nodeRow.nodeType === "resourceRoot"
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
                    visible: nodeRow.nodeType !== "resourceRoot"
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
                onTapped: root.activateNode(nodeRow.nodeType, nodeRow.nodeId,
                                            nodeRow.resourceFolder)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    if (nodeRow.nodeType === "playlist") {
                        root.contextPlaylistId = root.playlistIdForNode(
                                    nodeRow.nodeId)
                        playlistMenu.popup()
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
