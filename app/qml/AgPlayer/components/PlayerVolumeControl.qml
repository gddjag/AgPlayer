import QtQuick
import QtQuick.Controls
import AgPlayer

Item {
    id: root
    objectName: "mainVolumeControl"
    property var playback: PlaybackController
    property bool emptyMode: false
    property bool compact: false
    property bool expanded: false
    // The host supplies the horizontal room before its right-side actions.
    // The transport remains centered while the slider grows only to the right.
    property real maximumExpandedWidth: 196
    property alias expandedForQa: root.expanded
    readonly property alias muteButton: muteButton
    readonly property bool showExpandedPercent: !emptyMode && expanded
                                                && maximumExpandedWidth >= 136
    readonly property real buttonExtent: compact ? 32 : 44
    readonly property real expandedSliderWidth: !emptyMode && expanded
        ? Math.max(0, Math.min(108, maximumExpandedWidth - buttonExtent
                              - (showExpandedPercent ? 42 : 0))) : 0
    width: emptyMode ? buttonExtent
                     : buttonExtent + volumeSlider.width + volumePercent.width
                       + (volumePercent.width > 0 ? 4 : 0)
    height: buttonExtent
    z: 10
    clip: false

    Timer { id: volumeOpenTimer; interval: 120; onTriggered: root.expanded = true }
    Timer {
        id: volumeCloseTimer
        objectName: "mainVolumeCloseTimer"
        interval: 250
        onTriggered: if (!volumeSlider.pressed && !volumeHover.hovered) root.expanded = false
    }
    HoverHandler {
        id: volumeHover
        onHoveredChanged: {
            if (hovered) { volumeCloseTimer.stop(); volumeOpenTimer.restart() }
            else { volumeOpenTimer.stop(); volumeCloseTimer.restart() }
        }
    }
    ToolButton {
        id: muteButton
        objectName: "muteButton"
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        width: root.buttonExtent; height: root.buttonExtent; flat: true
        enabled: root.playback !== null
        icon.source: root.playback && root.playback.muted
                     ? Theme.icon("volume-mute-line") : Theme.icon("volume-up-line")
        icon.color: Theme.iconPrimary; icon.width: 20; icon.height: 20
        Accessible.name: root.playback && root.playback.muted
                         ? qsTr("取消静音") : qsTr("静音")
        onClicked: if (root.playback) root.playback.toggleMuted()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: Rectangle {
            color: "transparent"
            border.width: parent.activeFocus
                          && (parent.focusReason === Qt.TabFocusReason
                              || parent.focusReason === Qt.BacktabFocusReason)
                          ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }
    Slider {
        id: volumeSlider
        objectName: "volumeSlider"
        anchors.left: muteButton.right; anchors.verticalCenter: parent.verticalCenter
        property real animatedWidth: root.expandedSliderWidth
        // Resizing must clamp the animated flyout immediately to the new room.
        width: Math.max(0, Math.min(animatedWidth,
            root.maximumExpandedWidth - root.buttonExtent
            - volumePercent.width - (volumePercent.width > 0 ? 4 : 0)))
        leftPadding: 0
        rightPadding: 0
        opacity: width > 0 ? 1 : 0; visible: !root.emptyMode
        from: 0; to: 1
        Accessible.name: qsTr("音量")
        onPressedChanged: {
            if (pressed) { volumeCloseTimer.stop(); root.expanded = true }
            else volumeCloseTimer.restart()
        }
        enabled: root.playback !== null
        onMoved: if (root.playback) root.playback.setVolume(value)
        Binding on value {
            value: !root.playback ? 0
                : root.playback.muted ? 0 : root.playback.volume
            restoreMode: Binding.RestoreBindingOrValue
        }
        Behavior on animatedWidth { NumberAnimation { duration: root.expanded ? 160 : 220; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 140 } }
        background: Rectangle {
            x: volumeSlider.leftPadding; y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
            width: volumeSlider.availableWidth
            height: Theme.sliderTrackHeight
            radius: height / 2
            color: Theme.border
            Rectangle { width: volumeSlider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: Theme.accent }
        }
        handle: Rectangle {
            objectName: "volumeSliderHandle"
            x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
            width: Theme.sliderHandleExtent
            height: Theme.sliderHandleExtent
            radius: width / 2
            color: Theme.onBrandGradientText
            border.width: 1; border.color: Theme.border
        }
    }
    Label {
        id: volumePercent
        objectName: "volumePercentLabel"
        anchors.left: volumeSlider.right; anchors.leftMargin: width > 0 ? 4 : 0; anchors.verticalCenter: parent.verticalCenter
        property real animatedWidth: root.showExpandedPercent ? 38 : 0
        width: Math.max(0, Math.min(animatedWidth,
            root.maximumExpandedWidth - root.buttonExtent - 4))
        opacity: width > 0 ? 1 : 0; visible: !root.emptyMode
        horizontalAlignment: Text.AlignLeft
        text: Math.round((root.playback ? root.playback.volume : 0) * 100) + "%"
        color: Theme.primaryText
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeCaption
        Behavior on animatedWidth { NumberAnimation { duration: root.expanded ? 160 : 220; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }
}
