import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

// Left navigation sidebar for the AudioToolsWindow. Shows the app logo at the
// top, 5 tool entries below, and window controls (minimize/close) at the
// bottom. The current tool is highlighted; clicking an entry emits
// toolSelected(index) so the parent can update AudioToolsController.
Rectangle {
    id: sidebar
    color: Theme.panel
    implicitWidth: 240

    property int currentTool: 4
    property Window window

    signal toolSelected(int index)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingXs

        // Logo at top
        Image {
            source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
            sourceSize.width: 32
            sourceSize.height: 32
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingMd
            Layout.bottomMargin: Theme.spacingLg
            fillMode: Image.PreserveAspectFit
        }

        // 5 tool entries
        Repeater {
            model: [
                { name: qsTr("Format Convert"), icon: "music-2-fill" },
                { name: qsTr("Light Edit"), icon: "music-2-fill" },
                { name: qsTr("Speed Adjust"), icon: "music-2-fill" },
                { name: qsTr("Pitch Shift"), icon: "music-2-fill" },
                { name: qsTr("Info Edit"), icon: "music-2-fill" }
            ]

            delegate: Button {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                flat: true
                checked: sidebar.currentTool === index
                focusPolicy: Qt.StrongFocus

                contentItem: RowLayout {
                    spacing: Theme.spacingSm

                    Image {
                        source: Theme.icon(modelData.icon)
                        sourceSize.width: 18
                        sourceSize.height: 18
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        opacity: checked ? 1.0 : 0.7
                    }

                    Text {
                        text: modelData.name
                        color: checked ? Theme.cyan : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 13
                        font.weight: checked ? Font.Medium : Font.Normal
                        Layout.fillWidth: true
                    }
                }

                background: Rectangle {
                    color: checked ? Theme.border
                                  : (parent.hovered ? Qt.rgba(1, 1, 1, 0.04)
                                                    : "transparent")
                    radius: Theme.radiusSm
                }

                onClicked: sidebar.toolSelected(index)
            }
        }

        Item { Layout.fillHeight: true }

        // Window controls at bottom
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            ToolButton {
                icon.source: Theme.icon("subtract-line")
                icon.color: Theme.secondaryText
                icon.width: 16
                icon.height: 16
                focusPolicy: Qt.StrongFocus
                onClicked: if (sidebar.window) sidebar.window.showMinimized()
                ToolTip.text: qsTr("Minimize")
                ToolTip.visible: hovered
            }

            ToolButton {
                icon.source: Theme.icon("close-fill")
                icon.color: Theme.secondaryText
                icon.width: 16
                icon.height: 16
                focusPolicy: Qt.StrongFocus
                onClicked: WindowController.hideAudioTools()
                ToolTip.text: qsTr("Close")
                ToolTip.visible: hovered
            }
        }
    }
}
