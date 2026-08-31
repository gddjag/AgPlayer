import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: navigation
    objectName: "audioToolsTopNav"

    property bool referenceWorkbench: false
    property bool separationWorkbench: false
    property int currentTool: 0
    property Window window
    readonly property color activeLabelColor: Theme.primaryText
    readonly property var visibleToolOrder: [0, 4, 1, 2, 3]
    readonly property var visibleTools: [
        { toolId: 0, name: qsTr("音频编辑"), icon: "equalizer-line" },
        { toolId: 4, name: qsTr("人声伴奏分离"), icon: "music-2-line" },
        { toolId: 1, name: qsTr("格式转换"), icon: "briefcase-4-line" },
        { toolId: 2, name: qsTr("元数据编辑"), icon: "information-line" },
        { toolId: 3, name: qsTr("文件名处理"), icon: "file-copy-line" }
    ]

    signal toolSelected(int toolId)

    color: Theme.panel
    border.color: Theme.border
    border.width: 1
    radius: separationWorkbench ? 7 : Theme.radiusMd
    implicitHeight: separationWorkbench ? 44
                                         : referenceWorkbench ? 52 : 59

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: 12
        anchors.rightMargin: separationWorkbench ? 0 : Theme.spacingLg
        spacing: Theme.spacingSm

        Repeater {
            model: navigation.visibleTools

            Button {
                id: navButton
                objectName: "audioToolNav_" + modelData.toolId
                Layout.preferredWidth: 154
                Layout.preferredHeight: navigation.separationWorkbench ? 44
                                        : navigation.referenceWorkbench ? 52 : 58
                Layout.maximumHeight: Layout.preferredHeight
                flat: true
                checked: navigation.currentTool === modelData.toolId
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.name
                Accessible.role: Accessible.PageTab

                contentItem: RowLayout {
                    spacing: 7
                    Item { Layout.preferredWidth: 12 }
                    ThemedIcon {
                        source: Theme.icon(modelData.icon)
                        tint: navButton.checked ? Theme.accent
                                                : Theme.iconSecondary
                        sourceSize.width: 18
                        sourceSize.height: 18
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                    }
                    Text {
                        text: modelData.name
                        color: navButton.checked ? navigation.activeLabelColor
                                                 : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 15
                        font.weight: navButton.checked ? Font.DemiBold
                                                       : Font.Normal
                    }
                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: navButton.checked
                           ? Theme.subtleGlassActive
                           : navButton.hovered ? Theme.subtleGlassHover
                                               : "transparent"
                    border.color: navButton.checked ? Theme.accent
                                                    : "transparent"
                    border.width: navButton.checked ? 1 : 0
                    radius: navigation.separationWorkbench ? 6
                                                           : Theme.radiusMd

                    Rectangle {
                        visible: navButton.checked
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: Theme.accent
                    }
                }

                onClicked: navigation.toolSelected(modelData.toolId)
            }
        }

        Item { Layout.fillWidth: true }
    }
}
