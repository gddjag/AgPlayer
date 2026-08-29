import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: "transparent"
    property bool emptyMode: false
    property bool showListWindowButton: true
    property bool volumeExpanded: false
    signal openEqualizerRequested()

    function playbackModeName() {
        switch (PlaybackController.mode) {
        case PlaybackController.Sequential:
            return qsTr("顺序播放")
        case PlaybackController.Shuffle:
            return qsTr("随机播放")
        case PlaybackController.RepeatOne:
            return qsTr("单曲循环")
        default:
            return qsTr("列表循环")
        }
    }

    Timer {
        id: volumeOpenTimer
        interval: 120
        onTriggered: root.volumeExpanded = true
    }

    Timer {
        id: volumeCloseTimer
        objectName: "mainVolumeCloseTimer"
        interval: 2000
        onTriggered: {
            if (!volumeSlider.pressed && !volumeHover.hovered)
                root.volumeExpanded = false
        }
    }

    ToolButton {
        id: listWindowButton
        objectName: "listWindowButton"
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root.emptyMode ? -4 : -8
        flat: true
        icon.source: Theme.icon("list-unordered")
        icon.color: WindowController.listWindowVisible
                    ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        Accessible.name: WindowController.listWindowVisible
                         ? qsTr("Hide playlist window")
                         : qsTr("Show playlist window")
        onClicked: WindowController.toggleListWindow()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
        visible: root.showListWindowButton
    }

    RowLayout {
        id: centerControls
        objectName: "centerPlaybackControls"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: root.emptyMode ? -4 : -8
        spacing: root.emptyMode ? 28 : 16

        ToolButton {
            objectName: "audioToolsButton"
            flat: true
            icon.source: Theme.icon("briefcase-4-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("Open audio tools")
            onClicked: WindowController.showAudioTools()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ToolButton {
            objectName: "equalizerButton"
            flat: true
            icon.source: Theme.icon("equalizer-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            contentItem.rotation: 90
            Accessible.name: qsTr("十段图形均衡器")
            onClicked: root.openEqualizerRequested()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ToolButton {
            objectName: "waveformModeButton"
            flat: true
            icon.source: Theme.icon("waveform-switch")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("Change waveform mode")
            onClicked: SettingsController.waveformMode =
                       (SettingsController.waveformMode + 1) % 3
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ToolButton {
            objectName: "previousButton"
            flat: true
            icon.source: Theme.icon("skip-back-fill")
            icon.color: Theme.iconPrimary
            icon.width: 24
            icon.height: 24
            Accessible.name: qsTr("Previous track")
            onClicked: PlaybackController.previous()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ToolButton {
            id: playPauseButton
            objectName: "playPauseButton"
            Layout.preferredWidth: 52
            Layout.preferredHeight: 52
            flat: true
            icon.source: PlaybackController.state === PlaybackController.Playing
                         ? Theme.icon("pause-fill")
                         : Theme.icon("play-fill")
            icon.color: Theme.iconPrimary
            icon.width: 24
            icon.height: 24
            scale: down ? 0.95 : hovered ? 1.05 : 1.0
            Accessible.name: PlaybackController.state === PlaybackController.Playing
                             ? qsTr("Pause") : qsTr("Play")
            onClicked: PlaybackController.togglePlayback()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered

            Behavior on scale {
                NumberAnimation {
                    duration: playPauseButton.down ? 150 : 200
                    easing.type: Easing.OutCubic
                }
            }

            background: Rectangle {
                id: playButtonBody
                objectName: "playButtonBody"
                radius: width / 2
                color: playPauseButton.hovered ? Theme.hoverSurface : Theme.panel
                border.width: 3
                border.color: PlaybackController.state === PlaybackController.Playing
                              ? Theme.playRingPlaying : Theme.playRingPaused
                Behavior on color { ColorAnimation { duration: 120 } }
            }
        }

        ToolButton {
            objectName: "nextButton"
            flat: true
            icon.source: Theme.icon("skip-forward-fill")
            icon.color: Theme.iconPrimary
            icon.width: 24
            icon.height: 24
            Accessible.name: qsTr("Next track")
            onClicked: PlaybackController.next()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ToolButton {
            objectName: "modeButton"
            flat: true
            icon.source: {
                switch (PlaybackController.mode) {
                case PlaybackController.Sequential:
                    return Theme.icon("play-order-line")
                case PlaybackController.RepeatOne:
                    return Theme.icon("repeat-one-line-alt")
                case PlaybackController.Shuffle:
                    return Theme.icon("shuffle-arrows-line")
                default:
                    return Theme.icon("repeat-list-line")
                }
            }
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: root.playbackModeName()
            onClicked: PlaybackController.cycleMode()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ExperienceActions {
            objectName: "experienceActions"
            compact: false
        }
    }

    Item {
        id: volumeControl
        objectName: "mainVolumeControl"
        property alias expandedForQa: root.volumeExpanded
        anchors.left: centerControls.right
        anchors.leftMargin: 12
        anchors.verticalCenter: centerControls.verticalCenter
        width: root.emptyMode ? 44 : 44 + volumeSlider.width
                                  + volumePercent.width
                                  + (volumePercent.width > 0 ? 6 : 0)
        height: 44
        z: 10
        clip: false

            HoverHandler {
                id: volumeHover
                onHoveredChanged: {
                    if (hovered) {
                        volumeCloseTimer.stop()
                        volumeOpenTimer.restart()
                    } else {
                        volumeOpenTimer.stop()
                        volumeCloseTimer.restart()
                    }
                }
            }

            ToolButton {
                id: muteButton
                objectName: "muteButton"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 44
                height: 44
                flat: true
                icon.source: PlaybackController.muted
                             ? Theme.icon("volume-mute-line")
                             : Theme.icon("volume-up-line")
                icon.color: Theme.iconPrimary
                icon.width: 20
                icon.height: 20
                Accessible.name: PlaybackController.muted
                                 ? qsTr("Unmute") : qsTr("Mute")
                onClicked: PlaybackController.toggleMuted()
                ToolTip.text: Accessible.name
                ToolTip.visible: hovered
                background: null
            }

            Slider {
                id: volumeSlider
                objectName: "volumeSlider"
                anchors.left: muteButton.right
                anchors.verticalCenter: parent.verticalCenter
                width: !root.emptyMode && root.volumeExpanded ? 108 : 0
                opacity: width > 0 ? 1 : 0
                visible: !root.emptyMode
                from: 0
                to: 1
                onPressedChanged: {
                    if (pressed) {
                        volumeCloseTimer.stop()
                        root.volumeExpanded = true
                    } else {
                        volumeCloseTimer.restart()
                    }
                }
                onMoved: PlaybackController.setVolume(value)
                Binding on value {
                    value: PlaybackController.muted
                           ? 0 : PlaybackController.volume
                    restoreMode: Binding.RestoreBindingOrValue
                }
                Behavior on width {
                    NumberAnimation {
                        duration: root.volumeExpanded ? 160 : 220
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on opacity {
                    NumberAnimation { duration: 140 }
                }

                background: Rectangle {
                    x: volumeSlider.leftPadding
                    y: volumeSlider.topPadding
                       + volumeSlider.availableHeight / 2 - height / 2
                    width: volumeSlider.availableWidth
                    height: 3
                    radius: 1.5
                    color: Theme.border

                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: Theme.cyan
                    }
                }

                handle: Rectangle {
                    objectName: "volumeSliderHandle"
                    x: volumeSlider.leftPadding
                       + volumeSlider.visualPosition
                         * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding
                       + volumeSlider.availableHeight / 2 - height / 2
                    width: 10
                    height: 10
                    radius: 5
                    color: Theme.onBrandGradientText
                    border.width: 1
                    border.color: Theme.border
                }
            }

            Label {
                id: volumePercent
                objectName: "volumePercentLabel"
                anchors.left: volumeSlider.right
                anchors.leftMargin: width > 0 ? 6 : 0
                anchors.verticalCenter: parent.verticalCenter
                width: !root.emptyMode && root.volumeExpanded ? 38 : 0
                opacity: width > 0 ? 1 : 0
                visible: !root.emptyMode
                horizontalAlignment: Text.AlignRight
                text: Math.round(PlaybackController.volume * 100) + "%"
                color: Theme.primaryText
                font.pixelSize: 12

                Behavior on width {
                    NumberAnimation {
                        duration: root.volumeExpanded ? 160 : 220
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on opacity {
                    NumberAnimation { duration: 140 }
                }
            }
        }

    ToolButton {
        objectName: "miniPlayerButton"
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -8
        visible: !root.emptyMode
        flat: true
        icon.source: Theme.icon("picture-in-picture-2-line")
        icon.color: Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        Accessible.name: qsTr("Switch to mini player")
        onClicked: WindowController.showMini()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
    }
}
