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
    property int workoutCount: 0
    property int carCount: 0
    property int networkCount: 0

    signal categorySelected(string category)
    signal importRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Image {
                source: Theme.icon("music-2-fill")
                sourceSize.width: 18
                sourceSize.height: 18
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                fillMode: Image.PreserveAspectFit
            }

            Text {
                text: qsTr("音乐列表")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            ToolButton {
                icon.source: root.expanded
                             ? Theme.icon("arrow-up-s-line")
                             : Theme.icon("arrow-down-s-line")
                icon.color: Theme.secondaryText
                icon.width: 14
                icon.height: 14
                onClicked: root.expanded = !root.expanded

                background: Rectangle {
                    color: "transparent"
                }
            }
        }

        // Categories
        ColumnLayout {
            id: categories
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

            CategoryItem {
                Layout.fillWidth: true
                icon: "playlist-2-fill"
                label: qsTr("健身歌单")
                count: root.workoutCount
                selected: root.selectedCategory === "workout"
                onClicked: root.categorySelected("workout")
            }

            CategoryItem {
                Layout.fillWidth: true
                icon: "playlist-2-fill"
                label: qsTr("车载歌单")
                count: root.carCount
                selected: root.selectedCategory === "car"
                onClicked: root.categorySelected("car")
            }

            CategoryItem {
                Layout.fillWidth: true
                icon: "playlist-2-fill"
                label: qsTr("网络流行")
                count: root.networkCount
                selected: root.selectedCategory === "network"
                onClicked: root.categorySelected("network")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Button {
                Layout.fillWidth: true
                text: qsTr("歌单")
                icon.source: Theme.icon("playlist-2-fill")
                icon.color: Theme.secondaryText
                onClicked: root.categorySelected("all")

                background: Rectangle {
                    color: parent.pressed ? Theme.cyan
                          : parent.hovered ? Theme.border
                          : Theme.panel
                    radius: Theme.radiusSm
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingSm
                    Image {
                        source: parent.parent.icon.source
                        sourceSize.width: 14
                        sourceSize.height: 14
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        text: parent.parent.text
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                }
            }

            Button {
                id: importButton
                objectName: "importButton"
                Layout.fillWidth: true
                text: qsTr("导入")
                icon.source: Theme.icon("folder-open-fill")
                icon.color: Theme.secondaryText
                onClicked: root.importRequested()

                background: Rectangle {
                    color: parent.pressed ? Theme.cyan
                          : parent.hovered ? Theme.border
                          : Theme.panel
                    radius: Theme.radiusSm
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingSm
                    Image {
                        source: parent.parent.icon.source
                        sourceSize.width: 14
                        sourceSize.height: 14
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        text: parent.parent.text
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
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

            Image {
                source: Theme.icon(catRoot.icon)
                sourceSize.width: 16
                sourceSize.height: 16
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                fillMode: Image.PreserveAspectFit
            }

            Text {
                text: catRoot.label
                color: catRoot.selected ? Theme.primaryText : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                Layout.fillWidth: true
            }

            Text {
                text: catRoot.count
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: catRoot.clicked()
        }
    }
}
