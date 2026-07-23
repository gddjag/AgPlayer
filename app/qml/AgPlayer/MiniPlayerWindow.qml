import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

// Frameless mini-player window. Shares the same controller/model singletons
// as the main window so playback state, queue and waveform survive the
// main <-> mini switch without re-creating a decoder or core handle.
//
// `playback` and `windows` default to the production singletons and can be
// overridden by tests (see tst_mini_player.qml) with fake QtObjects to verify
// shared state and window actions without touching real audio.
Window {
    id: miniWindow
    objectName: "miniPlayerWindow"
    visible: false
    width: 560
    height: 96
    minimumWidth: 480
    minimumHeight: 96
    maximumHeight: 96
    flags: Qt.FramelessWindowHint
    color: "transparent"
    title: "AgPlayer Mini"

    // Injected dependencies — defaults keep production wiring implicit.
    property var playback: PlaybackController
    property var windows: WindowController

    // Shared-state surface used by tests to verify main and mini mirror the
    // same playback position. Mirrors the same property added to Main.qml.
    property int positionMs: playback.positionMs

    // Aliases used by tests / WindowController to reach in by name.
    property alias playPauseButton: controls.playPauseButton
    property alias pinButton: pinButton
    property alias restoreButton: restoreButton
    property alias minimizeButton: minimizeButton
    property alias closeButton: closeButton

    // Wide translucent rounded surface. Blur is only used when the platform
    // supports it; the fallback is an opaque #0B111B surface with a thin
    // cool-gray border (spec 5.3 + section 8).
    Rectangle {
        id: surface
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.radiusLg
        color: "#0B111B"
        border.color: Theme.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 0
            spacing: 0

            // --- Title area: brand mark + window controls ---
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSm
                    anchors.rightMargin: Theme.spacingXs
                    spacing: Theme.spacingXs

                    Image {
                        source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        sourceSize.width: 16
                        sourceSize.height: 16
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        fillMode: Image.PreserveAspectFit
                    }

                    Text {
                        text: "AgPlayer"
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        id: pinButton
                        icon.source: Theme.icon("pushpin-fill")
                        icon.color: windows.alwaysOnTop ? Theme.cyan : Theme.secondaryText
                        icon.width: 14
                        icon.height: 14
                        Accessible.name: windows.alwaysOnTop
                                         ? qsTr("Disable always on top")
                                         : qsTr("Pin on top")
                        focusPolicy: Qt.StrongFocus
                        onClicked: windows.setAlwaysOnTop(!windows.alwaysOnTop)
                        ToolTip.text: Accessible.name
                        ToolTip.visible: hovered

                        background: Rectangle {
                            color: !parent.enabled ? "transparent"
                                  : parent.pressed ? Theme.cyan
                                  : parent.visualFocus ? Theme.border
                                  : parent.hovered ? Theme.border
                                  : "transparent"
                            border.color: parent.visualFocus ? Theme.cyan : "transparent"
                            border.width: parent.visualFocus ? 2 : 0
                            radius: Theme.radiusSm
                        }
                    }

                    ToolButton {
                        id: restoreButton
                        icon.source: Theme.icon("restore-line")
                        icon.color: Theme.secondaryText
                        icon.width: 14
                        icon.height: 14
                        Accessible.name: qsTr("Restore main window")
                        focusPolicy: Qt.StrongFocus
                        onClicked: windows.showMain()
                        ToolTip.text: qsTr("Restore")
                        ToolTip.visible: hovered

                        background: Rectangle {
                            color: !parent.enabled ? "transparent"
                                  : parent.pressed ? Theme.cyan
                                  : parent.visualFocus ? Theme.border
                                  : parent.hovered ? Theme.border
                                  : "transparent"
                            border.color: parent.visualFocus ? Theme.cyan : "transparent"
                            border.width: parent.visualFocus ? 2 : 0
                            radius: Theme.radiusSm
                        }
                    }

                    ToolButton {
                        id: minimizeButton
                        icon.source: Theme.icon("subtract-line")
                        icon.color: Theme.secondaryText
                        icon.width: 14
                        icon.height: 14
                        Accessible.name: qsTr("Minimize")
                        focusPolicy: Qt.StrongFocus
                        onClicked: miniWindow.showMinimized()
                        ToolTip.text: qsTr("Minimize")
                        ToolTip.visible: hovered

                        background: Rectangle {
                            color: !parent.enabled ? "transparent"
                                  : parent.pressed ? Theme.cyan
                                  : parent.visualFocus ? Theme.border
                                  : parent.hovered ? Theme.border
                                  : "transparent"
                            border.color: parent.visualFocus ? Theme.cyan : "transparent"
                            border.width: parent.visualFocus ? 2 : 0
                            radius: Theme.radiusSm
                        }
                    }

                    ToolButton {
                        id: closeButton
                        icon.source: Theme.icon("close-fill")
                        icon.color: Theme.secondaryText
                        icon.width: 14
                        icon.height: 14
                        Accessible.name: qsTr("Close")
                        focusPolicy: Qt.StrongFocus
                        onClicked: windows.requestClose()
                        ToolTip.text: qsTr("Close")
                        ToolTip.visible: hovered

                        background: Rectangle {
                            color: !parent.enabled ? "transparent"
                                  : parent.pressed ? Theme.favoriteRed
                                  : parent.visualFocus ? Theme.border
                                  : parent.hovered ? Theme.favoriteRed
                                  : "transparent"
                            border.color: parent.visualFocus ? Theme.cyan : "transparent"
                            border.width: parent.visualFocus ? 2 : 0
                            radius: Theme.radiusSm
                        }
                    }
                }
            }

            // --- Main content: cover / favorite / metadata / waveform / transport / volume ---
            MiniPlayerControls {
                id: controls
                Layout.fillWidth: true
                Layout.fillHeight: true
                playback: miniWindow.playback
                windows: miniWindow.windows
            }
        }

        // Drag the frameless window from anywhere not covered by a button.
        // `z: -1` keeps this MouseArea behind the RowLayout so ToolButtons
        // receive presses first; no event forwarding is required.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            z: -1
            onPressed: function(mouse) {
                miniWindow.startSystemMove()
            }
        }
    }
}
