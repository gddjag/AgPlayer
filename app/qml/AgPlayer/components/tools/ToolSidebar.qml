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
    radius: Theme.radiusMd
    implicitHeight: 55

    property string currentToolId: "audio-editor"
    property Window window
    signal toolSelected(string toolId)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingSm

        Item { Layout.fillWidth: true }

        Repeater {
            model: [
                { id: "audio-editor", name: qsTr("音频编辑"), icon: "equalizer-line" },
                { id: "format-converter", name: qsTr("格式转换"), icon: "briefcase-4-line" },
                { id: "voice-clone", name: qsTr("人声克隆"), icon: "music-2-line" },
                { id: "metadata-editor", name: qsTr("元数据修改"), icon: "information-line" },
                { id: "filename-processor", name: qsTr("文件名处理"), icon: "file-copy-line" }
            ]

            Button {
                objectName: "audioToolNavButton"
                Layout.preferredWidth: modelData.id === "metadata-editor" ? 156 : 154
                Layout.maximumWidth: Layout.preferredWidth
                Layout.preferredHeight: 53
                Layout.maximumHeight: 53
                flat: true
                checked: navigation.currentToolId === modelData.id
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
                    radius: Theme.radiusMd
                }

                onClicked: navigation.toolSelected(modelData.id)
            }
        }

        Item { Layout.fillWidth: true }
    }
}
