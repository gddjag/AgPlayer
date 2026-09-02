import QtQuick
import QtQuick.Controls
import AgPlayer

Item {
    id: root
    objectName: "mainVolumeControl"
    property var playback: PlaybackController
    property bool emptyMode: false
    property bool expanded: false
    // The host supplies the horizontal room before its right-side actions.
    // The transport remains centered while the slider grows only to the right.
    property real maximumExpandedWidth: 196
    property alias expandedForQa: root.expanded
    readonly property alias muteButton: muteButton
    readonly property bool showExpandedPercent: !emptyMode && expanded
                                                && maximumExpandedWidth >= 136
    readonly property real expandedSliderWidth: !emptyMode && expanded
        ? Math.max(0, Math.min(108, maximumExpandedWidth - 44
                              - (showExpandedPercent ? 44 : 0))) : 0
    width: emptyMode ? 44 : 44 + volumeSlider.width + volumePercent.width
                       + (volumePercent.width > 0 ? 6 : 0)
    height: 44
    z: 10
    clip: false

    Timer { id: volumeOpenTimer; interval: 120; onTriggered: root.expanded = true }
    Timer {
        id: volumeCloseTimer
        objectName: "mainVolumeCloseTimer"
        interval: 2000
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
        width: 44; height: 44; flat: true
        icon.source: root.playback.muted ? Theme.icon("volume-mute-line") : Theme.icon("volume-up-line")
        icon.color: Theme.iconPrimary; icon.width: 20; icon.height: 20
        Accessible.name: root.playback.muted ? qsTr("Unmute") : qsTr("Mute")
        onClicked: root.playback.toggleMuted()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: null
    }
    Slider {
        id: volumeSlider
        objectName: "volumeSlider"
        anchors.left: muteButton.right; anchors.verticalCenter: parent.verticalCenter
        width: root.expandedSliderWidth
        opacity: width > 0 ? 1 : 0; visible: !root.emptyMode
        from: 0; to: 1
        Accessible.name: qsTr("Volume")
        onPressedChanged: {
            if (pressed) { volumeCloseTimer.stop(); root.expanded = true }
            else volumeCloseTimer.restart()
        }
        onMoved: root.playback.setVolume(value)
        Binding on value { value: root.playback.muted ? 0 : root.playback.volume; restoreMode: Binding.RestoreBindingOrValue }
        Behavior on width { NumberAnimation { duration: root.expanded ? 160 : 220; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 140 } }
        background: Rectangle {
            x: volumeSlider.leftPadding; y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
            width: volumeSlider.availableWidth; height: 3; radius: 1.5; color: Theme.border
            Rectangle { width: volumeSlider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: Theme.cyan }
        }
        handle: Rectangle {
            objectName: "volumeSliderHandle"
            x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
            width: 10; height: 10; radius: 5; color: Theme.onBrandGradientText
            border.width: 1; border.color: Theme.border
        }
    }
    Label {
        id: volumePercent
        objectName: "volumePercentLabel"
        anchors.left: volumeSlider.right; anchors.leftMargin: width > 0 ? 6 : 0; anchors.verticalCenter: parent.verticalCenter
        width: root.showExpandedPercent ? 38 : 0
        opacity: width > 0 ? 1 : 0; visible: !root.emptyMode
        horizontalAlignment: Text.AlignRight
        text: Math.round(root.playback.volume * 100) + "%"
        color: Theme.primaryText; font.pixelSize: 12
        Behavior on width { NumberAnimation { duration: root.expanded ? 160 : 220; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }
}
