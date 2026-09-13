import QtQuick
import QtQuick.Controls
import AgPlayer

// QML controls for the existing frameless header, not native AppKit buttons.
// Keep each window's close/unsaved-work policy at the call site.
Row {
    id: controls
    property Window targetWindow
    property bool allowFullScreen: true
    property int previousVisibility: Window.Windowed
    readonly property bool groupHovered: hover.hovered
    signal closeRequested()

    visible: Qt.platform.os === "osx"
    spacing: 0
    HoverHandler { id: hover }

    Repeater {
        model: 3
        delegate: ThemedIconButton {
            id: button
            required property int index
            objectName: ["macCloseButton", "macMinimizeButton", "macFullScreenButton"][index]
            width: Theme.controlHeightCompact
            height: Theme.controlHeightCompact
            enabled: controls.targetWindow !== null && (index !== 2 || controls.allowFullScreen)
            accessibleName: index === 0 ? qsTr("关闭窗口")
                : index === 1 ? qsTr("最小化")
                : controls.targetWindow && controls.targetWindow.visibility === Window.FullScreen
                    ? qsTr("退出全屏") : qsTr("进入全屏")
            ToolTip.text: accessibleName
            ToolTip.visible: hovered || visualFocus
            ToolTip.delay: 500
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.macWindowDotExtent
                    height: width
                    radius: width / 2
                    color: !button.enabled || !(controls.targetWindow.active || controls.groupHovered)
                        ? Theme.macWindowInactive
                        : button.index === 0 ? Theme.macWindowClose
                        : button.index === 1 ? Theme.macWindowMinimize : Theme.macWindowFullScreen
                    opacity: button.down ? 0.7 : 1
                    border.width: 1
                    border.color: Qt.darker(color, 1.15)
                    ThemedIcon {
                        anchors.centerIn: parent
                        width: Theme.macWindowGlyphExtent
                        height: width
                        source: Theme.icon(button.index === 0 ? "close-fill"
                            : button.index === 1 ? "subtract-line"
                            : controls.targetWindow && controls.targetWindow.visibility === Window.FullScreen
                                ? "fullscreen-exit-fill" : "fullscreen-fill")
                        tint: Theme.macWindowGlyph
                        visible: button.enabled && (controls.groupHovered || button.visualFocus)
                    }
                }
            }
            background: Rectangle {
                color: "transparent"
                radius: Theme.radiusSm
                border.width: button.visualFocus ? 2 : 0
                border.color: Theme.focus
            }
            onClicked: {
                if (index === 0) controls.closeRequested()
                else if (index === 1) controls.targetWindow.showMinimized()
                else if (controls.targetWindow.visibility === Window.FullScreen) {
                    if (controls.previousVisibility === Window.Maximized)
                        controls.targetWindow.showMaximized()
                    else controls.targetWindow.showNormal()
                } else {
                    controls.previousVisibility = controls.targetWindow.visibility
                    controls.targetWindow.showFullScreen()
                }
            }
        }
    }
}
