import QtQuick
import QtQuick.Controls
import AgPlayer

Rectangle {
    id: root
    color: "transparent"
    property bool emptyMode: false
    property bool showListWindowButton: true
    property int shellMode: SettingsController.playerShellMode
    property bool volumeExpanded: false
    readonly property bool compactTransport: width < 760
    readonly property int transportSpacing: emptyMode ? 28 : 16
    // Keep the transport clear of the right-aligned volume flyout.  The
    // offset only grows after the first 60 DIP of expansion, which is the
    // collapsed control's existing breathing room at the 1000 DIP shell.
    readonly property real volumeExpansionTransportOffset: emptyMode ? 0
        : Math.max(0, volumeControl.width - 104)
    signal openEqualizerRequested()
    signal toggleEmbeddedPlaylistRequested()

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
        anchors.verticalCenterOffset: 0
        flat: true
        icon.source: Theme.icon("list-unordered")
        icon.color: WindowController.listWindowVisible
                    ? Theme.iconAccent : Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        Accessible.name: WindowController.listWindowVisible
                         ? qsTr("Hide playlist window")
                         : qsTr("Show playlist window")
        onClicked: {
            if (root.shellMode === 1)
                root.toggleEmbeddedPlaylistRequested()
            else
                WindowController.toggleListWindow()
        }
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: null
        visible: root.showListWindowButton
    }

    Item {
        id: centerControls
        objectName: "centerPlaybackControls"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset: -root.volumeExpansionTransportOffset
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 0
        width: previousButton.width + root.transportSpacing
               + playPauseButton.width + root.transportSpacing
               + nextButton.width + root.transportSpacing
               + modeButton.width + (waveformModeButton.visible
                                     ? root.transportSpacing
                                     : 0)
               + waveformModeButton.width + (equalizerButton.visible
                                              ? root.transportSpacing
                                              : 0)
               + equalizerButton.width
        height: playPauseButton.height

        ToolButton {
            id: previousButton
            objectName: "previousButton"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
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
            anchors.left: previousButton.right
            anchors.leftMargin: root.transportSpacing
            anchors.verticalCenter: parent.verticalCenter
            width: 52
            height: 52
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
            id: nextButton
            objectName: "nextButton"
            anchors.left: playPauseButton.right
            anchors.leftMargin: root.transportSpacing
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
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
            id: modeButton
            objectName: "modeButton"
            anchors.left: nextButton.right
            anchors.leftMargin: root.transportSpacing
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
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

        ToolButton {
            id: waveformModeButton
            objectName: "waveformModeButton"
            visible: !root.compactTransport
            anchors.left: modeButton.right
            anchors.leftMargin: visible ? root.transportSpacing : 0
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? 32 : 0
            height: visible ? 32 : 0
            flat: true
            icon.source: Theme.icon("waveform-switch")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("Change waveform mode")
            onClicked: SettingsController.cycleWaveformMode()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ToolButton {
            id: equalizerButton
            objectName: "equalizerButton"
            visible: !root.compactTransport
            anchors.left: waveformModeButton.right
            anchors.leftMargin: visible ? root.transportSpacing : 0
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? 32 : 0
            height: visible ? 32 : 0
            flat: true
            icon.source: Theme.icon("equalizer-line")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            contentItem.rotation: 90
            Accessible.name: qsTr("十八段图形均衡器")
            onClicked: root.openEqualizerRequested()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

    }

    Item {
        id: volumeControl
        objectName: "mainVolumeControl"
        property alias expandedForQa: root.volumeExpanded
        anchors.right: parent.right
        anchors.rightMargin: 24
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

    Item {
        id: secondaryActions
        objectName: "playerSecondaryActions"
        anchors.right: volumeControl.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 0
        width: root.compactTransport ? 88 : 204
        height: 44

        ToolButton {
            id: audioToolsButton
            objectName: "audioToolsButton"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
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
            id: playerShellModeButton
            objectName: "playerShellModeButton"
            visible: !root.compactTransport
            anchors.left: audioToolsButton.right
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? 32 : 0
            height: 32
            flat: true
            icon.source: Theme.icon("player-shell-mode")
            icon.color: Theme.iconPrimary
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("切换播放器皮肤")
            onClicked: playerShellMenu.open()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ExperienceActions {
            id: experienceActions
            objectName: "experienceActions"
            anchors.left: playerShellModeButton.visible
                          ? playerShellModeButton.right : audioToolsButton.right
            anchors.leftMargin: root.compactTransport ? 4 : 14
            anchors.verticalCenter: parent.verticalCenter
            compact: root.compactTransport
        }

        ToolButton {
            objectName: "miniPlayerButton"
            visible: !root.emptyMode && !root.compactTransport
            anchors.left: experienceActions.right
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: visible ? 32 : 0
            height: 32
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

    Menu {
        id: playerShellMenu
        objectName: "playerShellMenu"
        y: Math.max(0, playerShellModeButton.y - height)

        MenuItem {
            objectName: "classicShellMenuItem"
            text: qsTr("经典模式")
            checkable: true
            checked: SettingsController.playerShellMode === 0
            onTriggered: SettingsController.playerShellMode = 0
        }
        MenuItem {
            objectName: "integratedShellMenuItem"
            text: qsTr("一体化模式")
            checkable: true
            checked: SettingsController.playerShellMode === 1
            onTriggered: SettingsController.playerShellMode = 1
        }
        MenuItem {
            objectName: "rollingShellMenuItem"
            text: qsTr("滚动播放模式")
            checkable: true
            checked: SettingsController.playerShellMode === 2
            onTriggered: SettingsController.playerShellMode = 2
        }
    }

}
