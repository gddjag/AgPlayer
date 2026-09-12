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
    property var waveformSession: null
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
        objectName: "miniWindowSurface"
        anchors.fill: parent
        anchors.margins: 2
        radius: Theme.windowRadius
        color: Theme.background
        border.width: 0
        clip: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: titleArea
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.controlHeight

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 5
                    spacing: 1

                    ThemedMacWindowControls {
                        targetWindow: miniWindow
                        allowFullScreen: false
                        onCloseRequested: windows.requestClose()
                    }

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
                        font.pixelSize: Theme.fontSizeCaption
                        font.weight: Font.Medium
                    }
                    Item { Layout.fillWidth: true }

                    ThemedIconButton {
                        id: pinButton
                        objectName: "miniPinButton"
                        Layout.preferredWidth: Theme.controlHeight
                        Layout.preferredHeight: Theme.controlHeight
                        iconSource: Theme.icon("pushpin-line")
                        iconSize: Theme.iconSizeSm
                        icon.source: iconSource
                        icon.width: iconSize
                        icon.height: iconSize
                        checkable: true
                        checked: windows && windows.alwaysOnTop
                        accessibleName: checked ? qsTr("Disable always on top") : qsTr("Pin on top")
                        ToolTip.text: accessibleName
                        ToolTip.visible: hovered
                        onClicked: if (windows) windows.setAlwaysOnTop(!windows.alwaysOnTop)
                    }
                    ThemedIconButton {
                        id: restoreButton
                        objectName: "miniRestoreButton"
                        Layout.preferredWidth: Theme.controlHeight
                        Layout.preferredHeight: Theme.controlHeight
                        iconSource: Theme.icon("restore-line")
                        iconSize: Theme.iconSizeSm
                        icon.source: iconSource
                        icon.width: iconSize
                        icon.height: iconSize
                        accessibleName: qsTr("Restore main window")
                        dangerOnHover: false
                        ToolTip.text: accessibleName
                        ToolTip.visible: hovered
                        onClicked: windows.showMain()
                    }
                    ThemedIconButton {
                        id: minimizeButton
                        objectName: "miniMinimizeButton"
                        visible: Qt.platform.os !== "osx"
                        Layout.preferredWidth: Theme.controlHeight
                        Layout.preferredHeight: Theme.controlHeight
                        iconSource: Theme.icon("subtract-line")
                        iconSize: Theme.iconSizeSm
                        icon.source: iconSource
                        icon.width: iconSize
                        icon.height: iconSize
                        accessibleName: qsTr("Minimize")
                        dangerOnHover: false
                        ToolTip.text: accessibleName
                        ToolTip.visible: hovered
                        onClicked: miniWindow.showMinimized()
                    }
                    ThemedIconButton {
                        id: closeButton
                        objectName: "miniCloseButton"
                        visible: Qt.platform.os !== "osx"
                        Layout.preferredWidth: Theme.controlHeight
                        Layout.preferredHeight: Theme.controlHeight
                        iconSource: Theme.icon("close-line")
                        iconSize: Theme.iconSizeSm
                        icon.source: iconSource
                        icon.width: iconSize
                        icon.height: iconSize
                        accessibleName: qsTr("Close")
                        dangerOnHover: true
                        ToolTip.text: accessibleName
                        ToolTip.visible: hovered
                        onClicked: windows.requestClose()
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
                waveformSession: miniWindow.waveformSession
                waveformActive: miniWindow.visible
                                && miniWindow.visibility !== Window.Minimized
            }
        }
    }

    WindowResizeHandles {
        objectName: "miniResizeHandles"
        targetWindow: miniWindow
    }
}
