import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: root

    property string selectedCategory: "all"
    property bool expanded: true
    property int allCount: 0
    property int favoriteCount: 0
    property int historyCount: 0
    property var playlistModel: PlaylistModel
    property string contextPlaylistId: ""

    signal categorySelected(string category)
    signal createPlaylistRequested()
    signal renamePlaylistRequested(string playlistId)
    signal removePlaylistRequested(string playlistId)
    signal importRequested()
    signal importPlaylistRequested()
    signal exportPlaylistRequested(string playlistId)

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

    component SystemMenuItem: MenuItem {
        id: systemMenuItem
        width: 200
        implicitWidth: 200
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ThemedIcon {
                source: Theme.icon("music-2-fill")
                tint: Theme.iconAccent
                sourceSize.width: 19
                sourceSize.height: 19
                Layout.preferredWidth: 19
                Layout.preferredHeight: 19
            }
            Text {
                text: qsTr("歌单列表")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Math.max(15, Qt.application.font.pixelSize)
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            ToolButton {
                Accessible.name: root.expanded ? qsTr("折叠歌单列表") : qsTr("展开歌单列表")
                text: root.expanded ? "⌃" : "⌄"
                onClicked: root.expanded = !root.expanded
                background: null
            }
        }

        Flickable {
            id: categoryViewport
            objectName: "playlistScrollArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.expanded
            clip: true
            contentWidth: width
            contentHeight: categories.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    // Child playlist rows own their context click. Only use the
                    // viewport handler for genuinely blank sidebar space.
                    if (point.position.y >= categories.implicitHeight) {
                        root.contextPlaylistId = ""
                        playlistMenu.popup()
                    }
                }
            }

            Column {
                id: categories
                width: categoryViewport.width - 8
                spacing: 2

                CategoryItem {
                    width: categories.width
                    icon: "music-2-fill"
                    label: qsTr("所有歌曲")
                    count: root.allCount
                    selected: root.selectedCategory === "all"
                    onClicked: root.categorySelected("all")
                    onContextRequested: { root.contextPlaylistId = ""; playlistMenu.popup() }
                }
                CategoryItem {
                    width: categories.width
                    icon: "heart-line"
                    label: qsTr("我的收藏")
                    count: root.favoriteCount
                    selected: root.selectedCategory === "favorites"
                    onClicked: root.categorySelected("favorites")
                }
                CategoryItem {
                    width: categories.width
                    objectName: "historyCategoryButton"
                    icon: "time-line"
                    label: qsTr("播放历史")
                    count: root.historyCount
                    selected: root.selectedCategory === "history"
                    onClicked: root.categorySelected("history")
                }
                CategoryItem {
                    width: categories.width
                    objectName: "recentAddedCategoryButton"
                    icon: "add-line"
                    label: qsTr("最近添加")
                    selected: root.selectedCategory === "recentAdded"
                    onClicked: root.categorySelected("recentAdded")
                }
                CategoryItem {
                    width: categories.width
                    objectName: "neverPlayedCategoryButton"
                    icon: "time-line"
                    label: qsTr("从未播放")
                    selected: root.selectedCategory === "neverPlayed"
                    onClicked: root.categorySelected("neverPlayed")
                }

                Repeater {
                    model: root.playlistModel
                    delegate: CategoryItem {
                        required property string playlistId
                        required property string name
                        required property int trackCount
                        width: categories.width
                        objectName: "playlistCategory-" + playlistId
                        icon: "playlist-2-fill"
                        label: name
                        count: trackCount
                        selected: root.selectedCategory === playlistId
                        onClicked: root.categorySelected(playlistId)
                        onContextRequested: {
                            root.contextPlaylistId = playlistId
                            playlistMenu.popup()
                        }
                        DropArea {
                            objectName: "playlistDropTarget-" + playlistId
                            anchors.fill: parent
                            keys: ["application/x-agplayer-track-ids"]
                            onDropped: function(drop) {
                                var encoded = drop.getDataAsString(
                                            "application/x-agplayer-track-ids")
                                var ids = encoded ? JSON.parse(encoded) : []
                                if (ids.length === 0 && drop.source
                                        && drop.source.dragTrackIds) {
                                    ids = drop.source.dragTrackIds
                                }
                                if (ids.length > 0) {
                                    if (root.selectedCategory !== "all"
                                            && root.selectedCategory !== "favorites"
                                            && root.selectedCategory !== "history"
                                            && root.selectedCategory !== "recentAdded"
                                            && root.selectedCategory !== "neverPlayed") {
                                        root.playlistModel.moveTracks(root.selectedCategory,
                                                                      playlistId, ids)
                                    } else {
                                        root.playlistModel.addTracks(playlistId, ids)
                                    }
                                    drop.acceptProposedAction()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    component CategoryItem: Rectangle {
        id: categoryItem
        property string icon
        property string label
        property int count: -1
        property bool selected: false
        signal clicked()
        signal contextRequested()
        height: 38
        radius: Theme.radiusSm
        color: selected ? Theme.hoverSurface
                        : categoryHover.hovered ? Theme.panel : "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 9
            ThemedIcon {
                source: Theme.icon(categoryItem.icon)
                tint: categoryItem.selected ? Theme.iconAccent : Theme.iconSecondary
                sourceSize.width: 17
                sourceSize.height: 17
                Layout.preferredWidth: 17
                Layout.preferredHeight: 17
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 22
                clip: true

                Text {
                    id: categoryLabel
                    objectName: "playlistTitleText"
                    anchors.verticalCenter: parent.verticalCenter
                    text: categoryItem.label
                    color: categoryItem.selected ? Theme.primaryText : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Math.max(14, Qt.application.font.pixelSize)
                    SequentialAnimation on x {
                        running: categoryHover.hovered
                                 && categoryLabel.implicitWidth > categoryLabel.parent.width
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) categoryLabel.x = 0
                        PauseAnimation { duration: 500 }
                        NumberAnimation {
                            to: Math.min(0, categoryLabel.parent.width
                                         - categoryLabel.implicitWidth)
                            duration: Math.max(500,
                                (categoryLabel.implicitWidth
                                 - categoryLabel.parent.width) * 22)
                            easing.type: Easing.Linear
                        }
                        PauseAnimation { duration: 650 }
                        NumberAnimation { to: 0; duration: 220 }
                    }
                }
            }
            Text {
                visible: categoryItem.count >= 0
                text: categoryItem.count
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
            }
        }
        HoverHandler { id: categoryHover }
        TapHandler { acceptedButtons: Qt.LeftButton; onTapped: categoryItem.clicked() }
        TapHandler { acceptedButtons: Qt.RightButton; onTapped: categoryItem.contextRequested() }
    }
}
