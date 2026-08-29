import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Window {
    id: miniWindow
    objectName: "miniPlayerWindow"
    visible: false
    width: 588
    height: 186
    minimumWidth: 588
    minimumHeight: 186
    maximumHeight: 186
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    title: "AgPlayer Mini"
    palette.window: Theme.background
    palette.windowText: Theme.primaryText
    palette.base: Theme.surfaceElevated
    palette.alternateBase: Theme.surface
    palette.text: Theme.primaryText
    palette.button: Theme.surfaceElevated
    palette.buttonText: Theme.primaryText
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightText
    palette.mid: Theme.opaqueBorder

    property var playback: PlaybackController
    property var windows: WindowController
    property int positionMs: playback ? playback.positionMs : 0

    property alias playPauseButton: controls.playPauseButton
    property alias pinButton: pinButton
    property alias restoreButton: restoreButton
    property alias minimizeButton: minimizeButton
    property alias closeButton: closeButton

    onClosing: function(close) {
        close.accepted = false
        if (windows)
            windows.requestClose()
    }

    Rectangle {
        id: surface
        anchors.fill: parent
        anchors.margins: 2
        radius: Theme.windowRadius
        color: Theme.background
        border.color: Theme.border
        border.width: 1
        clip: true

        SkinBackdrop {
            anchors.fill: parent
            anchors.margins: surface.border.width
            radius: Math.max(0, surface.radius - surface.border.width)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: titleArea
                Layout.fillWidth: true
                Layout.preferredHeight: 30

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 5
                    spacing: 1

                    Image {
                        source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        sourceSize.width: 18
                        sourceSize.height: 18
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        text: "AgPlayer"
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    Item { Layout.fillWidth: true }

                    ToolButton {
                        id: pinButton
                        objectName: "miniPinButton"
                        Layout.preferredWidth: 32; Layout.preferredHeight: 32
                        icon.source: Theme.icon("pushpin-line")
                        icon.color: windows && windows.alwaysOnTop ? Theme.cyan
                                                                   : Theme.secondaryText
                        icon.width: 16
                        icon.height: 16
                        Accessible.name: windows && windows.alwaysOnTop
                                         ? qsTr("Disable always on top")
                                         : qsTr("Pin on top")
                        onClicked: {
                            if (windows)
                                windows.setAlwaysOnTop(!windows.alwaysOnTop)
                        }
                        background: Rectangle {
                            color: parent.hovered ? Theme.hoverSurface
                                                  : "transparent"
                            radius: Theme.radiusSm
                        }
                    }
                    ToolButton {
                        id: restoreButton
                        objectName: "miniRestoreButton"
                        Layout.preferredWidth: 32; Layout.preferredHeight: 32
                        icon.source: Theme.icon("restore-line")
                        icon.color: Theme.secondaryText
                        icon.width: 16
                        icon.height: 16
                        Accessible.name: qsTr("Restore main window")
                        onClicked: windows.showMain()
                        background: Rectangle {
                            color: parent.hovered ? Theme.hoverSurface
                                                  : "transparent"
                            radius: Theme.radiusSm
                        }
                    }
                    ToolButton {
                        id: minimizeButton
                        objectName: "miniMinimizeButton"
                        Layout.preferredWidth: 32; Layout.preferredHeight: 32
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.secondaryText
                        icon.width: 16
                        icon.height: 16
                        Accessible.name: qsTr("Minimize")
                        onClicked: miniWindow.showMinimized()
                        background: Rectangle {
                            color: parent.hovered ? Theme.hoverSurface
                                                  : "transparent"
                            radius: Theme.radiusSm
                        }
                    }
                    ToolButton {
                        id: closeButton
                        objectName: "miniCloseButton"
                        Layout.preferredWidth: 32; Layout.preferredHeight: 32
                        icon.source: Theme.icon("close-line")
                        icon.color: Theme.secondaryText
                        icon.width: 16
                        icon.height: 16
                        Accessible.name: qsTr("Close")
                        onClicked: windows.requestClose()
                        background: Rectangle {
                            color: parent.hovered ? Theme.danger
                                                  : "transparent"
                            radius: Theme.radiusSm
                        }
                    }
                }

                DragHandler {
                    target: null
                    acceptedButtons: Qt.LeftButton
                    onActiveChanged: if (active) miniWindow.startSystemMove()
                }
            }

            MiniPlayerControls {
                id: controls
                Layout.fillWidth: true
                Layout.fillHeight: true
                playback: miniWindow.playback
                windows: miniWindow.windows
            }
        }
    }

    WindowResizeHandles {
        objectName: "miniResizeHandles"
        targetWindow: miniWindow
    }
}
