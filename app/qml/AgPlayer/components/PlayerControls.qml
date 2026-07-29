import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.background
    property bool emptyMode: false

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: root.emptyMode ? 24 : 0
        anchors.leftMargin: root.emptyMode ? 24 : Theme.spacingXl
        anchors.rightMargin: root.emptyMode ? 32 : Theme.spacingXl
        spacing: Theme.spacingLg

        ToolButton {
            id: listWindowButton
            objectName: "listWindowButton"
            flat: true
            icon.source: Theme.icon("list-unordered")
            icon.color: WindowController.listWindowVisible ? Theme.iconAccent : Theme.iconPrimary
            icon.width: 32
            icon.height: 32
            Accessible.name: WindowController.listWindowVisible
                             ? qsTr("Hide playlist window")
                             : qsTr("Show playlist window")
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.toggleListWindow()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        Item { Layout.fillWidth: true }

        Item {
            Layout.preferredWidth: centerControls.implicitWidth
            Layout.fillHeight: true

        RowLayout {
            id: centerControls
            anchors.fill: parent
            spacing: root.emptyMode ? 44 : 24

        ToolButton {
            objectName: "audioToolsButton"
            flat: true
            icon.source: Theme.icon("briefcase-4-line")
            icon.color: Theme.iconPrimary
            icon.width: 34
            icon.height: 34
            Accessible.name: qsTr("Open audio tools")
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.showAudioTools()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ToolButton {
            objectName: "waveformModeButton"
            flat: true
            icon.source: Theme.icon("voiceprint-line")
            icon.color: Theme.iconAccent
            icon.width: 34
            icon.height: 34
            Accessible.name: qsTr("Change waveform mode")
            focusPolicy: Qt.StrongFocus
            onClicked: SettingsController.setWaveformMode(
                           (SettingsController.waveformMode + 1) % 3)
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
            background: null
        }

        ToolButton {
            objectName: "previousButton"
            icon.source: Theme.icon("skip-back-fill")
            icon.color: Theme.iconPrimary
            icon.width: 30
            icon.height: 30
            Accessible.name: qsTr("Previous track")
            focusPolicy: Qt.StrongFocus
            onClicked: PlaybackController.previous()
            ToolTip.text: qsTr("Previous")
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
            objectName: "playPauseButton"
            icon.source: PlaybackController.state === PlaybackController.Playing
                         ? Theme.icon("pause-fill")
                         : Theme.icon("play-fill")
            icon.color: Theme.iconPrimary
            icon.width: 34
            icon.height: 34
            Layout.preferredWidth: root.emptyMode ? 104 : 72
            Layout.preferredHeight: root.emptyMode ? 104 : 72
            Layout.topMargin: -8
            Accessible.name: PlaybackController.state === PlaybackController.Playing
                             ? qsTr("Pause")
                             : qsTr("Play")
            focusPolicy: Qt.StrongFocus
            onClicked: PlaybackController.togglePlayback()
            ToolTip.text: PlaybackController.state === PlaybackController.Playing
                          ? qsTr("Pause")
                          : qsTr("Play")
            ToolTip.visible: hovered

            background: Rectangle {
                color: "transparent"
                radius: width / 2

                Rectangle {
                    id: cyanGlow
                    anchors.fill: parent
                    anchors.margins: -10
                    opacity: SettingsController.playButtonRgbGlow ? 0.08 : 0
                    color: Theme.cyan
                    radius: width / 2

                    SequentialAnimation on opacity {
                        running: SettingsController.playButtonRgbGlow
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.05; to: 0.18; duration: 1100; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 0.18; to: 0.05; duration: 1100; easing.type: Easing.InOutSine }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -5
                    opacity: SettingsController.playButtonRgbGlow ? 0.12 : 0
                    color: Theme.waveformViolet
                    radius: width / 2

                    SequentialAnimation on scale {
                        running: SettingsController.playButtonRgbGlow
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.96; to: 1.08; duration: 1100; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.08; to: 0.96; duration: 1100; easing.type: Easing.InOutSine }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.waveformBlue }
                        GradientStop { position: 1.0; color: Theme.waveformMagenta }
                    }
                    radius: width / 2
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 6
                    color: Theme.panel
                    border.color: Theme.waveformViolet
                    border.width: 1
                    radius: width / 2
                }
            }
        }

        ToolButton {
            objectName: "nextButton"
            icon.source: Theme.icon("skip-forward-fill")
            icon.color: Theme.iconPrimary
            icon.width: 30
            icon.height: 30
            Accessible.name: qsTr("Next track")
            focusPolicy: Qt.StrongFocus
            onClicked: PlaybackController.next()
            ToolTip.text: qsTr("Next")
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
            objectName: "modeButton"
            icon.source: {
                switch (PlaybackController.mode) {
                case PlaybackController.RepeatOne:
                    return Theme.icon("repeat-one-fill")
                case PlaybackController.Shuffle:
                    return Theme.icon("shuffle-fill")
                case PlaybackController.RepeatAll:
                    return Theme.icon("repeat-fill")
                default:
                    return Theme.icon("repeat-fill")
                }
            }
            icon.color: PlaybackController.mode === PlaybackController.Sequential
                        ? Theme.iconSecondary
                        : Theme.iconAccent
            icon.width: 30
            icon.height: 30
            Accessible.name: {
                switch (PlaybackController.mode) {
                case PlaybackController.RepeatOne:
                    return qsTr("Repeat one")
                case PlaybackController.Shuffle:
                    return qsTr("Shuffle")
                case PlaybackController.RepeatAll:
                    return qsTr("Repeat all")
                default:
                    return qsTr("Sequential")
                }
            }
            focusPolicy: Qt.StrongFocus
            onClicked: PlaybackController.cycleMode()
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
            id: muteButton
            objectName: "muteButton"
            icon.source: PlaybackController.muted
                         ? Theme.icon("volume-mute-fill")
                         : Theme.icon("volume-up-fill")
            icon.color: Theme.iconSecondary
            icon.width: root.emptyMode ? 30 : 20
            icon.height: root.emptyMode ? 30 : 20
            Accessible.name: PlaybackController.muted
                             ? qsTr("Unmute")
                             : qsTr("Mute")
            focusPolicy: Qt.StrongFocus
            onClicked: PlaybackController.toggleMuted()
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

        Slider {
            objectName: "volumeSlider"
            visible: !root.emptyMode
                     && (muteButton.hovered || hovered || activeFocus || pressed)
            from: 0
            to: 1
            onMoved: PlaybackController.setVolume(value)
            Layout.preferredWidth: visible ? 140 : 0
            Accessible.name: qsTr("Volume")
            focusPolicy: Qt.StrongFocus

            Binding on value {
                value: PlaybackController.muted ? 0 : PlaybackController.volume
                restoreMode: Binding.RestoreBindingOrValue
            }
        }

        }
        }

        Item { Layout.fillWidth: true }

        Item {
            Layout.preferredWidth: listWindowButton.implicitWidth
            Layout.fillHeight: true

            ToolButton {
                objectName: "miniPlayerButton"
                anchors.centerIn: parent
                visible: !root.emptyMode
                flat: true
                icon.source: Theme.icon("picture-in-picture-2-line")
                icon.color: Theme.iconPrimary
                icon.width: 30
                icon.height: 30
                Accessible.name: qsTr("Switch to mini player")
                focusPolicy: Qt.StrongFocus
                onClicked: WindowController.showMini()
                ToolTip.text: Accessible.name
                ToolTip.visible: hovered
                background: null
            }
        }
    }
}
