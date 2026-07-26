import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: root.emptyMode ? "#02091B" : Theme.background
    property bool emptyMode: false
    readonly property real wideGap: emptyMode
                                    ? Math.max(0, Math.min(55, (width - 1100) / 8))
                                    : 0

    signal toggleLyrics()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingXl
        anchors.rightMargin: Theme.spacingXl
        spacing: Theme.spacingLg

        ToolButton {
            objectName: "listWindowButton"
            icon.source: Theme.icon("playlist-2-fill")
            icon.color: WindowController.listWindowVisible ? Theme.cyan : Theme.primaryText
            icon.width: 24
            icon.height: 24
            Accessible.name: WindowController.listWindowVisible
                             ? qsTr("Hide playlist window")
                             : qsTr("Show playlist window")
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.toggleListWindow()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
        }

        Item {
            Layout.preferredWidth: Math.min(166, root.width * 0.12)
        }

        Item { Layout.fillWidth: true }

        ToolButton {
            objectName: "audioToolsButton"
            icon.source: Theme.icon("equalizer-fill")
            icon.color: Theme.primaryText
            icon.width: 24
            icon.height: 24
            Accessible.name: qsTr("Open audio tools")
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.showAudioTools()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
        }

        Item { Layout.preferredWidth: root.wideGap }

        ToolButton {
            objectName: "waveformModeButton"
            icon.source: Theme.icon("music-2-fill")
            icon.color: Theme.cyan
            icon.width: 24
            icon.height: 24
            Accessible.name: qsTr("Change waveform mode")
            focusPolicy: Qt.StrongFocus
            onClicked: SettingsController.setWaveformMode(
                           (SettingsController.waveformMode + 1) % 3)
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
        }

        Item { Layout.preferredWidth: root.wideGap }

        ToolButton {
            objectName: "previousButton"
            icon.source: Theme.icon("skip-back-fill")
            icon.color: Theme.primaryText
            icon.width: 22
            icon.height: 22
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

        Item { Layout.preferredWidth: root.wideGap }

        ToolButton {
            objectName: "playPauseButton"
            icon.source: PlaybackController.state === PlaybackController.Playing
                         ? Theme.icon("pause-fill")
                         : Theme.icon("play-fill")
            icon.color: Theme.primaryText
            icon.width: 34
            icon.height: 34
            Layout.preferredWidth: root.emptyMode ? 88 : 72
            Layout.preferredHeight: root.emptyMode ? 88 : 72
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
                color: Theme.panel
                border.color: parent.pressed ? Theme.waveformMagenta
                                             : parent.hovered ? Theme.waveformViolet
                                                              : Theme.cyan
                border.width: 2
                radius: width / 2

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 6
                    color: "transparent"
                    border.color: Theme.waveformViolet
                    border.width: 1
                    radius: width / 2
                    opacity: 0.75
                }
            }
        }

        Item { Layout.preferredWidth: root.wideGap }

        ToolButton {
            objectName: "nextButton"
            icon.source: Theme.icon("skip-forward-fill")
            icon.color: Theme.primaryText
            icon.width: 22
            icon.height: 22
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

        Item { Layout.preferredWidth: root.wideGap }

        ToolButton {
            objectName: "modeButton"
            icon.source: {
                switch (PlaybackController.mode) {
                case PlaybackController.RepeatOne:
                    return Theme.icon("repeat-one-fill")
                case PlaybackController.Shuffle:
                    return Theme.icon("shuffle-fill")
                default:
                    return Theme.icon("repeat-fill")
                }
            }
            icon.color: PlaybackController.mode === PlaybackController.Sequential
                        ? Theme.secondaryText
                        : Theme.cyan
            icon.width: 20
            icon.height: 20
            Accessible.name: {
                switch (PlaybackController.mode) {
                case PlaybackController.RepeatOne:
                    return qsTr("Repeat one")
                case PlaybackController.Shuffle:
                    return qsTr("Shuffle")
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
            objectName: "lyricsButton"
            visible: false
            icon.source: Theme.icon("music-2-fill")
            icon.color: Theme.secondaryText
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("Toggle lyrics")
            focusPolicy: Qt.StrongFocus
            onClicked: root.toggleLyrics()
            ToolTip.text: qsTr("Lyrics")
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

        Item { Layout.fillWidth: true }

        ToolButton {
            objectName: "muteButton"
            icon.source: PlaybackController.muted
                         ? Theme.icon("volume-mute-fill")
                         : Theme.icon("volume-up-fill")
            icon.color: Theme.secondaryText
            icon.width: 20
            icon.height: 20
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
            from: 0
            to: 1
            onMoved: PlaybackController.setVolume(value)
            Layout.preferredWidth: 160
            Accessible.name: qsTr("Volume")
            focusPolicy: Qt.StrongFocus

            Binding on value {
                value: PlaybackController.muted ? 0 : PlaybackController.volume
                restoreMode: Binding.RestoreBindingOrValue
            }
        }

        Item { Layout.preferredWidth: root.wideGap }

        ToolButton {
            objectName: "miniPlayerButton"
            icon.source: Theme.icon("restore-line")
            icon.color: Theme.primaryText
            icon.width: 24
            icon.height: 24
            Accessible.name: qsTr("Switch to mini player")
            focusPolicy: Qt.StrongFocus
            onClicked: WindowController.showMini()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered
        }
    }
}
