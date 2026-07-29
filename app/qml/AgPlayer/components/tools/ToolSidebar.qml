import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: sidebar
    color: Theme.panel
    implicitWidth: 200
    radius: Theme.radiusSm
    border.color: Theme.border
    border.width: 1

    property int currentTool: 1
    property Window window
    signal toolSelected(int index)

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        spacing: 4

        Repeater {
            model: [
                { name: qsTr("格式转换"), icon: "equalizer-line" },
                { name: qsTr("轻度剪辑"), icon: "scissors-cut-line" },
                { name: qsTr("调整速度"), icon: "speed-up-line" },
                { name: qsTr("升调降调"), icon: "music-2-line" },
                { name: qsTr("信息修改"), icon: "information-line" }
            ]

            Button {
                Layout.fillWidth: true
                Layout.leftMargin: 0
                Layout.rightMargin: 10
                Layout.preferredHeight: 62
                flat: true
                checked: sidebar.currentTool === index
                focusPolicy: Qt.StrongFocus

                contentItem: RowLayout {
                    spacing: 12

                    ToolButton {
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        enabled: false
                        flat: true
                        icon.source: Theme.icon(modelData.icon)
                        icon.color: checked ? Theme.cyan : Theme.iconSecondary
                        icon.width: 24
                        icon.height: 24
                        opacity: checked ? 1.0 : 0.78
                    }

                    Text {
                        text: modelData.name
                        color: checked ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 15
                        font.weight: checked ? Font.DemiBold : Font.Normal
                        Layout.fillWidth: true
                    }
                }

                background: Rectangle {
                    color: checked ? Qt.rgba(0.05, 0.35, 0.95, 0.26)
                                   : (parent.hovered ? Theme.hoverSurface : "transparent")
                    border.color: checked ? Theme.cyan : "transparent"
                    border.width: checked ? 1 : 0
                    radius: Theme.radiusSm

                    Rectangle {
                        visible: parent.parent.checked
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 3
                        color: Theme.cyan
                    }
                }

                onClicked: sidebar.toolSelected(index)
            }
        }

        Item { Layout.fillHeight: true }
    }
}
