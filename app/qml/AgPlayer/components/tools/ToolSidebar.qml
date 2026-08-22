import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: navigation
    objectName: "audioToolsTopNav"
    property bool referenceWorkbench: false
    color: referenceWorkbench ? "#071925" : Theme.panel
    border.color: referenceWorkbench ? "#142b3a" : Theme.border
    border.width: 1
    radius: referenceWorkbench ? 0 : Theme.radiusMd
    implicitHeight: 55

    readonly property var toolNames: [
        qsTr("音频编辑"), qsTr("格式转换"),
        qsTr("元数据编辑"), qsTr("文件名处理")
    ]
    property int currentTool: 0
    property Window window
    signal toolSelected(int index)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: referenceWorkbench ? 20 : Theme.spacingLg
        anchors.rightMargin: referenceWorkbench ? 20 : Theme.spacingLg
        spacing: referenceWorkbench ? 0 : Theme.spacingSm

        Item { Layout.fillWidth: !navigation.referenceWorkbench }

        Repeater {
            model: [
                { name: qsTr("音频编辑"), icon: "equalizer-line" },
                { name: qsTr("格式转换"), icon: "briefcase-4-line" },
                { name: qsTr("元数据编辑"), icon: "information-line" },
                { name: qsTr("文件名处理"), icon: "file-copy-line" }
            ]

            Button {
                id: navButton
                objectName: "audioToolNavButton"
                Layout.preferredWidth: navigation.referenceWorkbench ? 164 : 154
                Layout.preferredHeight: 53
                Layout.maximumHeight: 53
                flat: true
                checked: navigation.currentTool === index
                focusPolicy: Qt.StrongFocus

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
                    radius: navigation.referenceWorkbench ? 0 : Theme.radiusMd
                    Rectangle {
                        visible: navigation.referenceWorkbench && navButton.checked
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: Theme.accent
                    }
                }

                onClicked: navigation.toolSelected(index)
            }
        }

        Item { Layout.fillWidth: true }
    }
}
