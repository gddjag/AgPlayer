import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root

    property string objectNamePrefix: "library"
    property var tagModel: TagModel
    property var filterModel: null
    property var lyricsService: LyricsService
    property int expandedWidth: 312
    property int collapsedWidth: 42
    property int headerHeight: 30
    property int lyricsTabWidth: 72
    property int toggleSize: 30
    property int toggleIconSize: 22
    property int tabFontSize: Theme.fontSizeBodyStrong
    property int currentPage: 0
    property bool expanded: true
    property alias tagSearchText: tagPanel.searchText

    signal pageRequested(int page)
    signal expandedRequested(bool expanded)

    objectName: objectNamePrefix + "TagColumn"
    Layout.preferredWidth: expanded ? expandedWidth : collapsedWidth
    Layout.minimumWidth: Layout.preferredWidth
    Layout.maximumWidth: Layout.preferredWidth
    Layout.fillHeight: true
    color: Theme.panel
    border.color: Theme.integratedSoftOutline
    border.width: 1
    radius: Theme.radiusSm

    Item {
        anchors.fill: parent
        anchors.margins: root.expanded ? Theme.spacingSm + 2
                                       : Theme.spacingSm - 2

        RowLayout {
            id: sidePanelHeader
            objectName: root.objectNamePrefix + "SidePanelHeader"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.headerHeight
            spacing: Theme.spacingXs

            Button {
                id: tagTabButton
                objectName: root.objectNamePrefix + "TagTabButton"
                visible: root.expanded
                Layout.fillWidth: true
                Layout.fillHeight: true
                flat: true
                text: qsTr("标签管理") + " (" + (root.tagModel
                      ? root.tagModel.rowCount() : 0) + ")"
                Accessible.name: qsTr("标签管理")
                onClicked: root.pageRequested(0)
                contentItem: Text {
                    text: tagTabButton.text
                    color: root.currentPage === 0
                           ? Theme.primaryText : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: root.tabFontSize
                    font.weight: root.currentPage === 0
                                 ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    objectName: root.objectNamePrefix + "TagTabOutline"
                    color: root.currentPage === 0
                           ? Theme.subtleGlassActive
                           : tagTabButton.hovered
                             ? Theme.subtleGlassHover : Theme.subtleGlassFill
                    border.color: root.currentPage === 0
                                  ? Theme.accent : Theme.subtleGlassBorder
                    border.width: 1
                    radius: Theme.radiusSm
                }
            }

            Button {
                id: lyricsTabButton
                objectName: root.objectNamePrefix + "LyricsTabButton"
                visible: root.expanded
                Layout.preferredWidth: root.lyricsTabWidth
                Layout.fillHeight: true
                flat: true
                text: qsTr("歌词")
                Accessible.name: text
                onClicked: root.pageRequested(1)
                contentItem: Text {
                    text: lyricsTabButton.text
                    color: root.currentPage === 1
                           ? Theme.primaryText : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: root.tabFontSize
                    font.weight: root.currentPage === 1
                                 ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    objectName: root.objectNamePrefix + "LyricsTabOutline"
                    color: root.currentPage === 1
                           ? Theme.subtleGlassActive
                           : lyricsTabButton.hovered
                             ? Theme.subtleGlassHover : Theme.subtleGlassFill
                    border.color: root.currentPage === 1
                                  ? Theme.accent : Theme.subtleGlassBorder
                    border.width: 1
                    radius: Theme.radiusSm
                }
            }

            ToolButton {
                id: sidePanelToggleButton
                objectName: root.objectNamePrefix + "SidePanelToggleButton"
                Layout.preferredWidth: root.toggleSize
                Layout.preferredHeight: root.toggleSize
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                padding: Theme.spacingXs
                flat: true
                Accessible.name: root.expanded
                                 ? qsTr("隐藏标签和歌词侧栏")
                                 : qsTr("显示标签和歌词侧栏")
                onClicked: root.expandedRequested(!root.expanded)
                ToolTip.text: Accessible.name
                ToolTip.visible: hovered
                background: Rectangle {
                    color: sidePanelToggleButton.hovered
                           ? Theme.hoverSurface : "transparent"
                    radius: Theme.radiusSm
                }
                contentItem: ThemedIcon {
                    objectName: root.objectNamePrefix + "SidePanelToggleIcon"
                    source: Theme.icon("side-panel-toggle")
                    tint: Theme.iconPrimary
                    sourceSize.width: root.toggleIconSize
                    sourceSize.height: root.toggleIconSize
                    mirror: !root.expanded
                }
            }
        }

        Item {
            objectName: root.objectNamePrefix + "SidePanelContent"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: sidePanelHeader.bottom
            anchors.topMargin: Theme.spacingSm
            anchors.bottom: parent.bottom
            visible: root.expanded

            Item {
                objectName: root.objectNamePrefix + "TagContent"
                anchors.fill: parent
                visible: root.currentPage === 0

                TagManagementPanel {
                    id: tagPanel
                    objectName: root.objectNamePrefix + "TagManagementPanel"
                    anchors.fill: parent
                    tagModel: root.tagModel
                    filterModel: root.filterModel
                    compact: true
                    collapsible: false
                    expanded: true
                    showHeader: false
                }
            }

            Item {
                objectName: root.objectNamePrefix + "LyricsContent"
                anchors.fill: parent
                visible: root.currentPage === 1
                clip: true

                LyricsPanel {
                    objectName: root.objectNamePrefix + "LyricsPanel"
                    anchors.fill: parent
                    service: root.lyricsService
                    spatialMode: false
                    onCloseRequested: {
                        PlayerExperienceController.lyricsVisible = false
                        root.expandedRequested(false)
                    }
                }
            }
        }
    }
}
