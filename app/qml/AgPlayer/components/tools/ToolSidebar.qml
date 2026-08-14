import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: navigation
    objectName: "audioToolsTopNav"
    color: Theme.panel
    border.color: Theme.border
    border.width: 1
    radius: Theme.radiusSm
    implicitHeight: 55

    readonly property var toolNames: [
        qsTr("音频编辑"), qsTr("格式转换"),
        qsTr("元数据修改"), qsTr("文件名处理"),
        qsTr("人声伴奏分离"), qsTr("人声克隆")
    ]
    property int currentTool: 0
    property Window window
    signal toolSelected(int index)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        spacing: 0

        Repeater {
            model: [
                { name: qsTr("音频编辑"), icon: "equalizer-line" },
                { name: qsTr("格式转换"), icon: "briefcase-4-line" },
                { name: qsTr("元数据修改"), icon: "information-line" },
                { name: qsTr("文件名处理"), icon: "file-copy-line" },
                { name: qsTr("人声伴奏分离"), icon: "waveform-switch" },
                { name: qsTr("人声克隆"), icon: "music-2-line" }
            ]

            Button {
                objectName: "audioToolNavButton"
                Layout.fillWidth: true
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
                        color: checked ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 15
                        font.weight: checked ? Font.DemiBold : Font.Normal
                    }
                    Item { Layout.fillWidth: true }
                }

                background: Rectangle {
                    color: checked ? Qt.rgba(Theme.accent.r,
                                             Theme.accent.g,
                                             Theme.accent.b, 0.14)
                                   : (parent.hovered ? Theme.hoverSurface : "transparent")
                    border.width: 0
                    radius: Theme.radiusSm

                    Rectangle {
                        visible: parent.parent.checked
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: Theme.cyan
                    }
                }

                onClicked: navigation.toolSelected(index)
            }
        }

        Item { Layout.fillWidth: true }
    }
}
