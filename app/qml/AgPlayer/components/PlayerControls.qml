import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel

    signal importRequested()

    function currentTrackFavorite(): bool {
        var row = PlaybackController.trackIndex
        if (row < 0 || row >= LibraryModel.rowCount())
            return false
        var idx = LibraryModel.index(row, 0)
        return LibraryModel.data(idx, LibraryModel.FavoriteRole)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingXl
        anchors.rightMargin: Theme.spacingXl
        spacing: Theme.spacingLg

        ToolButton {
            objectName: "importButton"
            icon.source: Theme.icon("folder-open-fill")
            icon.color: Theme.secondaryText
            icon.width: 20
            icon.height: 20
            Accessible.name: qsTr("Import audio files")
            focusPolicy: Qt.StrongFocus
            onClicked: root.importRequested()
            ToolTip.text: qsTr("Import")
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

        Item { Layout.preferredWidth: Theme.spacingMd }

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

        ToolButton {
            objectName: "playPauseButton"
            icon.source: PlaybackController.state === PlaybackController.Playing
                         ? Theme.icon("pause-fill")
                         : Theme.icon("play-fill")
            icon.color: Theme.cyan
            icon.width: 28
            icon.height: 28
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
            objectName: "favoriteButton"
            icon.source: root.currentTrackFavorite()
                         ? Theme.icon("heart-fill")
                         : Theme.icon("heart-line")
            icon.color: root.currentTrackFavorite()
                        ? Theme.favoriteRed
                        : Theme.secondaryText
            icon.width: 20
            icon.height: 20
            Accessible.name: root.currentTrackFavorite()
                             ? qsTr("Remove from favorites")
                             : qsTr("Add to favorites")
            focusPolicy: Qt.StrongFocus
            enabled: PlaybackController.trackIndex >= 0
            onClicked: PlaybackController.toggleFavorite()
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
            Layout.preferredWidth: 120
            Accessible.name: qsTr("Volume")
            focusPolicy: Qt.StrongFocus

            Binding on value {
                value: PlaybackController.muted ? 0 : PlaybackController.volume
                restoreMode: Binding.RestoreBindingOrValue
            }
        }
    }
}
