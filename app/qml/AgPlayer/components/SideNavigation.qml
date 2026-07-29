import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.background

    property string selectedCategory: "all"
    property bool expanded: true
    property int allCount: 0
    property int favoriteCount: 0
    property int historyCount: 0
    property var playlistModel: PlaylistModel

    readonly property bool customPlaylistSelected:
        selectedCategory !== "all"
        && selectedCategory !== "favorites"
        && selectedCategory !== "history"

    signal categorySelected(string category)
    signal createPlaylistRequested()
    signal renamePlaylistRequested(string playlistId)
    signal removePlaylistRequested(string playlistId)
    signal importRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ToolButton {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                enabled: false
                padding: 0
                icon.source: Theme.icon("music-2-fill")
                icon.color: Theme.secondaryText
                icon.width: 18
                icon.height: 18
                background: null
            }

            Text {
                text: qsTr("播放列表")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            ToolButton {
                Accessible.name: root.expanded ? qsTr("折叠播放列表") : qsTr("展开播放列表")
                onClicked: root.expanded = !root.expanded
                contentItem: Text {
                    text: root.expanded ? "\u2303" : "\u2304"
                    color: Theme.secondaryText
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: "transparent" }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            visible: root.expanded

            CategoryItem {
                Layout.fillWidth: true
                icon: "music-2-fill"
                label: qsTr("所有歌曲")
                count: root.allCount
                selected: root.selectedCategory === "all"
                onClicked: root.categorySelected("all")
            }

            CategoryItem {
                Layout.fillWidth: true
                icon: "heart-line"
                label: qsTr("我的收藏")
                count: root.favoriteCount
                selected: root.selectedCategory === "favorites"
                onClicked: root.categorySelected("favorites")
            }

            CategoryItem {
                Layout.fillWidth: true
                icon: "checkbox-blank-circle-fill"
                label: qsTr("播放历史")
                count: root.historyCount
                selected: root.selectedCategory === "history"
                onClicked: root.categorySelected("history")
            }

            Repeater {
                model: root.playlistModel
                delegate: CategoryItem {
                    required property string playlistId
                    required property string name
                    required property int trackCount
                    Layout.fillWidth: true
                    icon: "playlist-2-fill"
                    label: name
                    count: trackCount
                    selected: root.selectedCategory === playlistId
                    onClicked: root.categorySelected(playlistId)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.customPlaylistSelected
            spacing: Theme.spacingSm

            Button {
                Layout.fillWidth: true
                text: qsTr("重命名")
                onClicked: root.renamePlaylistRequested(root.selectedCategory)
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("删除")
                onClicked: root.removePlaylistRequested(root.selectedCategory)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Button {
                Layout.fillWidth: true
                text: qsTr("新建歌单")
                icon.source: Theme.icon("playlist-2-fill")
                icon.color: Theme.secondaryText
                palette.buttonText: Theme.secondaryText
                onClicked: root.createPlaylistRequested()
                background: Rectangle {
                    color: parent.pressed ? Theme.cyan
                          : parent.hovered ? Theme.border : Theme.panel
                    radius: Theme.radiusSm
                }
            }

            Button {
                id: importButton
                objectName: "importButton"
                Layout.fillWidth: true
                text: qsTr("导入音乐")
                icon.source: Theme.icon("folder-open-fill")
                icon.color: Theme.secondaryText
                palette.buttonText: Theme.secondaryText
                onClicked: root.importRequested()
                background: Rectangle {
                    color: parent.pressed ? Theme.cyan
                          : parent.hovered ? Theme.border : Theme.panel
                    radius: Theme.radiusSm
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    component CategoryItem: Rectangle {
        id: catRoot
        color: selected ? Theme.panel : "transparent"
        radius: Theme.radiusSm
        height: 36

        property string icon
        property string label
        property int count
        property bool selected: false
        signal clicked()

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingSm
            anchors.rightMargin: Theme.spacingSm
            spacing: Theme.spacingSm

            ThemedIcon {
                source: Theme.icon(catRoot.icon)
                tint: catRoot.selected ? Theme.iconAccent : Theme.iconSecondary
                sourceSize.width: 16
                sourceSize.height: 16
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
            }
            Text {
                text: catRoot.label
                color: catRoot.selected ? Theme.primaryText : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: catRoot.count
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
            }
        }

        TapHandler { onTapped: catRoot.clicked() }
    }
}
