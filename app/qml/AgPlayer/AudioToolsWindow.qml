import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Window {
    id: window
    objectName: "audioToolsWindow"
    visible: false
    // Reference workbench baseline. Layouts still contract below this size.
    width: 1672
    height: 942
    minimumWidth: 880
    minimumHeight: 560
    flags: Qt.FramelessWindowHint
    color: "transparent"
    title: "AgPlayer · " + qsTr("音频工具")
    onClosing: function(close) {
        close.accepted = false
        WindowController.hideAudioTools()
    }
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
        radius: window.visibility === Window.Maximized ? 0 : Theme.windowRadius

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: titleBar
                objectName: "audioToolsTitleBar"
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: "transparent"

                RowLayout {
                    z: 1
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    spacing: 7

                    Image {
                        source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        text: "AgPlayer"
                        color: Theme.primaryText
                        font.family: Theme.fontFallback
                        font.pixelSize: 17
                        font.weight: Font.Medium
                    }
                    Text {
                        text: "·"
                        color: Theme.secondaryText
                        font.pixelSize: 14
                    }
                    Text {
                        text: qsTr("音频工具")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 16
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.iconPrimary
                        onClicked: window.showMinimized()
                    }
                    ToolButton {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        icon.source: Theme.icon(window.visibility === Window.Maximized
                                                ? "fullscreen-exit-fill"
                                                : "checkbox-blank-line")
                        icon.color: Theme.iconPrimary
                        onClicked: window.visibility === Window.Maximized
                                   ? window.showNormal() : window.showMaximized()
                    }
                    ToolButton {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        icon.source: Theme.icon("close-fill")
                        icon.color: Theme.iconPrimary
                        onClicked: WindowController.hideAudioTools()
                    }
                }

                MouseArea {
                    objectName: "audioToolsMoveArea"
                    property point lastGlobalPoint: Qt.point(0, 0)
                    property bool nativeMoveStarted: false
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.rightMargin: 104
                    z: 2
                    acceptedButtons: Qt.LeftButton
                    onPressed: function(mouse) {
                        lastGlobalPoint = mapToGlobal(mouse.x, mouse.y)
                        // The tools shell has custom docking/resizing. Moving it
                        // directly keeps that path deterministic on Windows and
                        // avoids startSystemMove swallowing drag delivery from
                        // QML, which made the title bar appear unresponsive.
                        nativeMoveStarted = false
                        mouse.accepted = true
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed || nativeMoveStarted
                                || window.visibility === Window.Maximized)
                            return
                        var globalPoint = mapToGlobal(mouse.x, mouse.y)
                        window.x += globalPoint.x - lastGlobalPoint.x
                        window.y += globalPoint.y - lastGlobalPoint.y
                        lastGlobalPoint = globalPoint
                    }
                    onReleased: nativeMoveStarted = false
                }
            }

            ToolSidebar {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                window: window
                currentTool: AudioToolsController.currentTool
                onToolSelected: function(index) {
                    AudioToolsController.selectTool(index)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.bottomMargin: 8
                color: Theme.background
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm

                StackLayout {
                    anchors.fill: parent
                    currentIndex: AudioToolsController.currentTool

                    LightEditPage { objectName: "lightEditorPage" }
                    FormatConvertPage { objectName: "formatConvertPage" }
                    MetadataEditPage {}
                    FilenameProcessPage {}
                }
            }
        }
    }

    WindowResizeHandles {
        objectName: "audioToolsResizeHandles"
        targetWindow: window
    }
}
