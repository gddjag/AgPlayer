import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: navigation
    objectName: "audioToolsTopNav"
    property bool referenceWorkbench: false
    property bool separationWorkbench: false
    color: referenceWorkbench ? "#071925" : Theme.panel
    border.color: referenceWorkbench ? "#142b3a" : Theme.border
    border.width: 1
    radius: separationWorkbench ? 7 : referenceWorkbench ? 0 : Theme.radiusMd
    implicitHeight: separationWorkbench ? 44 : referenceWorkbench ? 52 : 55

    property int currentTool: 0
    property Window window
    readonly property var visibleToolOrder: [0, 4, 1, 2, 3]
    readonly property var visibleTools: [
        { toolId: 0, name: qsTr("音频编辑"), icon: "equalizer-line" },
        { toolId: 4, name: qsTr("人声伴奏分离"), icon: "music-2-line" },
        { toolId: 1, name: qsTr("格式转换"), icon: "briefcase-4-line" },
        { toolId: 2, name: qsTr("元数据编辑"), icon: "information-line" },
        { toolId: 3, name: qsTr("文件名处理"), icon: "file-copy-line" }
    ]
    signal toolSelected(int toolId)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: separationWorkbench ? 0
                                                : referenceWorkbench ? 20 : Theme.spacingLg
        anchors.rightMargin: separationWorkbench ? 0
                                                 : referenceWorkbench ? 20 : Theme.spacingLg
        spacing: referenceWorkbench ? 0 : Theme.spacingSm

        Item { Layout.fillWidth: !navigation.referenceWorkbench }

        Repeater {
            model: navigation.visibleTools

            Button {
                id: navButton
                objectName: "audioToolNavButton"
                Layout.fillWidth: navigation.separationWorkbench
                Layout.preferredWidth: navigation.separationWorkbench ? 1
                                                                      : navigation.referenceWorkbench ? 164 : 154
                Layout.preferredHeight: navigation.separationWorkbench ? 44
                                                                       : navigation.referenceWorkbench ? 52 : 53
                Layout.maximumHeight: navigation.separationWorkbench ? 44
                                                                     : navigation.referenceWorkbench ? 52 : 53
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
                        tint: checked ? Theme.cyan : Theme.iconSecondary
                        sourceSize.width: 18
                        sourceSize.height: 18
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                    }
                    Text {
                        text: modelData.name
                        color: checked && navigation.referenceWorkbench
                               ? Theme.accent
                               : checked ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 15
                        font.weight: checked ? Font.DemiBold : Font.Normal
                    }
                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: navigation.referenceWorkbench
                           ? (checked ? "#0b2638"
                              : navButton.hovered ? "#0a2130" : "transparent")
                           : checked ? Qt.rgba(Theme.accent.r,
                                               Theme.accent.g,
                                               Theme.accent.b, 0.14)
                                     : (navButton.hovered ? Theme.hoverSurface : "transparent")
                    border.width: 0
                    radius: navigation.separationWorkbench ? 6
                                                           : navigation.referenceWorkbench ? 0 : Theme.radiusMd
                    Rectangle {
                        visible: navigation.referenceWorkbench && navButton.checked
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

        Item { Layout.fillWidth: !navigation.separationWorkbench }
    }
}
