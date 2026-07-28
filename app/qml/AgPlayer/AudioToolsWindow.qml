import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Window {
    id: window
    objectName: "audioToolsWindow"
    visible: false
    width: 1536
    height: 1024
    minimumWidth: 1180
    minimumHeight: 760
    flags: Qt.FramelessWindowHint
    color: Theme.background
    title: qsTr("AgPlayer 音频工具")
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.elevated
    palette.alternateBase: Theme.panel
    palette.text: Theme.primaryText
    palette.button: Theme.elevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.cyan
    palette.highlightedText: Theme.accentText
    palette.mid: Theme.border

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        border.color: Theme.border
        border.width: 1
        radius: Theme.radiusMd

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: titleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 16
                    spacing: 10

                    Image {
                        source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        text: "AgPlayer"
                        color: Theme.primaryText
                        font.family: Theme.fontFallback
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }
                    Text {
                        text: "·"
                        color: Theme.secondaryText
                        font.pixelSize: 18
                    }
                    Text {
                        text: qsTr("音频工具")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 20
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.iconPrimary
                        onClicked: window.showMinimized()
                    }
                    ToolButton {
                        icon.source: Theme.icon(window.visibility === Window.Maximized
                                                ? "fullscreen-exit-fill"
                                                : "checkbox-blank-line")
                        icon.color: Theme.iconPrimary
                        onClicked: window.visibility === Window.Maximized
                                   ? window.showNormal() : window.showMaximized()
                    }
                    ToolButton {
                        icon.source: Theme.icon("close-fill")
                        icon.color: Theme.iconPrimary
                        onClicked: WindowController.hideAudioTools()
                    }
                }

                DragHandler {
                    target: null
                    onActiveChanged: if (active) window.startSystemMove()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                spacing: 10

                ToolSidebar {
                    Layout.fillHeight: true
                    window: window
                    currentTool: AudioToolsController.currentTool
                    onToolSelected: function(index) {
                        AudioToolsController.selectTool(index)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.background
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm

                    StackLayout {
                        anchors.fill: parent
                        currentIndex: AudioToolsController.currentTool

                        FormatConvertPage {}
                        LightEditPage {}
                        SpeedAdjustPage {}
                        PitchShiftPage {}
                        InfoEditPage {}
                    }
                }
            }
        }
    }
}
