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
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    title: qsTr("AgPlayer · 音频工具")
    function requestHide() {
        if (AudioToolsController.currentTool === 0
                && AudioEditorController.modified) {
            unsavedCloseDialog.open()
            return
        }
        AudioEditorController.deactivate()
        WindowController.hideAudioTools()
    }
    onClosing: function(close) {
        close.accepted = false
        requestHide()
    }
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.elevated
    palette.alternateBase: Theme.panel
    palette.text: Theme.primaryText
    palette.button: Theme.elevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightText
    palette.mid: Theme.border

    Dialog {
        id: unsavedCloseDialog
        parent: window.contentItem
        anchors.centerIn: parent
        title: qsTr("舍弃未保存更改？")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            AudioEditorController.deactivate()
            WindowController.hideAudioTools()
        }
        Label {
            text: qsTr("当前音频尚未保存。关闭窗口将舍弃这些更改。")
            color: Theme.primaryText
        }
    }

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
                Layout.preferredHeight: 49
                color: Theme.panel

                RowLayout {
                    z: 1
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    spacing: 10

                    Image {
                        objectName: "audioToolsBrandMark"
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }
                    Text {
                        objectName: "audioToolsWindowTitle"
                        text: qsTr("AgPlayer · 音频工具")
                        color: Theme.primaryText
                        font.family: Theme.fontFallback
                        font.pixelSize: 18
                        font.weight: Font.Medium
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        objectName: "audioToolsMinimizeButton"
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 32
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.iconPrimary
                        Accessible.name: qsTr("最小化")
                        Accessible.role: Accessible.Button
                        onClicked: window.showMinimized()
                        background: Rectangle {
                            color: parent.hovered ? Theme.hoverSurface : "transparent"
                            radius: 3
                        }
                    }
                    ToolButton {
                        objectName: "audioToolsMaximizeButton"
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 32
                        icon.source: Theme.icon(window.visibility === Window.Maximized
                                                ? "fullscreen-exit-fill"
                                                : "checkbox-blank-line")
                        icon.color: Theme.iconPrimary
                        Accessible.name: window.visibility === Window.Maximized
                            ? qsTr("还原") : qsTr("最大化")
                        Accessible.role: Accessible.Button
                        onClicked: window.visibility === Window.Maximized
                                   ? window.showNormal() : window.showMaximized()
                        background: Rectangle {
                            color: parent.hovered ? Theme.hoverSurface : "transparent"
                            radius: 3
                        }
                    }
                    ToolButton {
                        objectName: "audioToolsCloseButton"
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 32
                        icon.source: Theme.icon("close-fill")
                        icon.color: hovered ? Theme.onBrandGradientText
                                            : Theme.iconPrimary
                        Accessible.name: qsTr("关闭")
                        Accessible.role: Accessible.Button
                        onClicked: window.requestHide()
                        background: Rectangle {
                            color: parent.hovered ? Theme.danger : "transparent"
                            radius: 3
                        }
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
                    anchors.rightMargin: 182
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
                Layout.preferredHeight: 43
                window: window
                currentTool: AudioToolsController.currentTool
                onToolSelected: function(index) {
                    AudioToolsController.selectTool(index)
                }
            }

            Rectangle {
                objectName: "audioToolsContentStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.background
                border.color: "transparent"
                border.width: 0
                radius: 0

                StackLayout {
                    anchors.fill: parent
                    currentIndex: AudioToolsController.currentTool

                    AudioEditorPage { objectName: "audioEditorPage" }
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
