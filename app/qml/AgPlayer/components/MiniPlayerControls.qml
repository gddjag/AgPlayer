import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

// Content panel for the mini player. Exposes the same `playback`/`windows`
// properties as MiniPlayerWindow so it can be reused or tested in isolation.
// All bindings route to the injected `playback` (defaulting to the production
// singleton) so the mini window shares state with the main window without
// creating a second controller/core handle.
Rectangle {
    id: root
    color: "transparent"

    property var playback: PlaybackController
    property var windows: WindowController

    // Aliases exposed so MiniPlayerWindow (and tests) can reach in by name.
    property alias playPauseButton: playPauseButton
    property alias previousButton: previousButton
    property alias nextButton: nextButton
    property alias modeButton: modeButton
    property alias favoriteButton: favoriteButton
    property alias muteButton: muteButton
    property alias volumeSlider: volumeSlider

    function currentTrackValue(role): variant {
        var row = playback.trackIndex
        if (row < 0 || row >= LibraryModel.rowCount())
            return ""
        var idx = LibraryModel.index(row, 0)
        return LibraryModel.data(idx, role)
    }

    function currentTrackFavorite(): bool {
        var row = playback.trackIndex
        if (row < 0 || row >= LibraryModel.rowCount())
            return false
        var idx = LibraryModel.index(row, 0)
        return LibraryModel.data(idx, LibraryModel.FavoriteRole)
    }

    function coverSource(): string {
        var url = root.currentTrackValue(LibraryModel.CoverUrlRole)
        if (url && url !== "")
            return url
        return "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
    }

    function currentTrackRating(): int {
        if (typeof LibraryModel.RatingRole === "undefined")
            return 0
        var raw = root.currentTrackValue(LibraryModel.RatingRole)
        var value = parseInt(raw, 10)
        if (isNaN(value))
            return 0
        return Math.max(0, Math.min(5, value))
    }

    function currentTrackBpm(): string {
        if (typeof LibraryModel.BpmRole === "undefined")
            return "--"
        var raw = root.currentTrackValue(LibraryModel.BpmRole)
        var value = parseFloat(raw)
        if (isNaN(value) || value <= 0)
            return "--"
        return Math.round(value) + " BPM"
    }

    function formatFileSize(bytes): string {
        if (bytes <= 0)
            return ""
        if (bytes >= 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB"
        if (bytes >= 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        if (bytes >= 1024)
            return Math.round(bytes / 1024) + " KB"
        return bytes + " B"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        spacing: Theme.spacingSm

        // --- Left: brand mark + cover ---
        Item {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            Layout.alignment: Qt.AlignVCenter

            Image {
                anchors.fill: parent
                source: root.coverSource()
                sourceSize.width: 44
                sourceSize.height: 44
                fillMode: Image.PreserveAspectFit
            }
        }

        // --- Favorite accent ---
        ToolButton {
            id: favoriteButton
            icon.source: root.currentTrackFavorite()
                         ? Theme.icon("heart-fill")
                         : Theme.icon("heart-line")
            icon.color: root.currentTrackFavorite()
                        ? Theme.favoriteRed
                        : Theme.secondaryText
            icon.width: 16
            icon.height: 16
            Accessible.name: root.currentTrackFavorite()
                             ? qsTr("Remove from favorites")
                             : qsTr("Add to favorites")
            focusPolicy: Qt.StrongFocus
            enabled: playback.trackIndex >= 0
            onClicked: playback.toggleFavorite()
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

        // --- Center: metadata + badges + waveform ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                RowLayout {
                    spacing: 1
                    Layout.alignment: Qt.AlignVCenter
                    visible: playback.trackIndex >= 0

                    Repeater {
                        model: 5
                        delegate: Image {
                            source: index < root.currentTrackRating()
                                    ? Theme.icon("star-fill")
                                    : Theme.icon("star-line")
                            sourceSize.width: 10
                            sourceSize.height: 10
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                }

                Text {
                    text: root.currentTrackValue(LibraryModel.TitleRole) || qsTr("No track loaded")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    ToolTip.text: text
                    ToolTip.visible: miniTitleHover.hovered && text !== qsTr("No track loaded")
                    ToolTip.delay: 500

                    HoverHandler {
                        id: miniTitleHover
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                Text {
                    property string artist: root.currentTrackValue(LibraryModel.ArtistRole)
                    text: artist && artist.length > 0 ? artist : qsTr("Unknown artist")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    ToolTip.text: text
                    ToolTip.visible: miniArtistHover.hovered
                    ToolTip.delay: 500

                    HoverHandler {
                        id: miniArtistHover
                    }
                }

                Repeater {
                    model: {
                        var badges = []
                        var fmt = root.currentTrackValue(LibraryModel.FormatRole)
                        if (fmt && fmt.length > 0)
                            badges.push(fmt.toUpperCase())
                        var bd = root.currentTrackValue(LibraryModel.BitDepthRole)
                        if (bd > 0)
                            badges.push(bd + "-bit")
                        var sr = root.currentTrackValue(LibraryModel.SampleRateRole)
                        if (sr > 0)
                            badges.push((sr / 1000) + " kHz")
                        var br = root.currentTrackValue(LibraryModel.BitRateRole)
                        if (br > 0)
                            badges.push(Math.round(br / 1000) + " kbps")
                        if (playback.trackIndex >= 0) {
                            var bpm = root.currentTrackBpm()
                            if (bpm.length > 0)
                                badges.push(bpm)
                        }
                        var size = root.currentTrackValue(LibraryModel.FileSizeRole)
                        if (size > 0)
                            badges.push(root.formatFileSize(size))
                        return badges
                    }

                    Rectangle {
                        color: Theme.panel
                        border.color: Theme.border
                        border.width: 1
                        radius: 4
                        implicitWidth: badgeText.implicitWidth + Theme.spacingSm * 2
                        implicitHeight: badgeText.implicitHeight + 2

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 9
                        }
                    }
                }
            }

            WaveformItem {
                id: waveform
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                Layout.minimumHeight: 20
                position: playback.positionMs
                duration: playback.durationMs
                clip: true
            }
        }

        // --- Transport: previous / circular play-pause / next ---
        ToolButton {
            id: previousButton
            icon.source: Theme.icon("skip-back-fill")
            icon.color: Theme.primaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: qsTr("Previous track")
            focusPolicy: Qt.StrongFocus
            onClicked: playback.previous()
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
            id: playPauseButton
            icon.source: playback.state === PlaybackController.Playing
                         ? Theme.icon("pause-fill")
                         : Theme.icon("play-fill")
            icon.color: Theme.cyan
            icon.width: 22
            icon.height: 22
            Accessible.name: playback.state === PlaybackController.Playing
                             ? qsTr("Pause")
                             : qsTr("Play")
            focusPolicy: Qt.StrongFocus
            onClicked: playback.togglePlayback()
            ToolTip.text: Accessible.name
            ToolTip.visible: hovered

            // Circular primary button: filled cyan disc with soft glow on hover.
            background: Rectangle {
                implicitWidth: 40
                implicitHeight: 40
                radius: width / 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Theme.cyan }
                    GradientStop { position: 1.0; color: Theme.violet }
                }
                border.color: parent.visualFocus ? Theme.cyan : "transparent"
                border.width: parent.visualFocus ? 2 : 0
                opacity: !parent.enabled ? 0.4
                       : parent.pressed ? 0.7
                       : parent.hovered ? 0.9
                       : 1.0
            }
        }

        ToolButton {
            id: nextButton
            icon.source: Theme.icon("skip-forward-fill")
            icon.color: Theme.primaryText
            icon.width: 18
            icon.height: 18
            Accessible.name: qsTr("Next track")
            focusPolicy: Qt.StrongFocus
            onClicked: playback.next()
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

        // --- Mode toggle ---
        ToolButton {
            id: modeButton
            icon.source: {
                switch (playback.mode) {
                case PlaybackController.RepeatOne:
                    return Theme.icon("repeat-one-fill")
                case PlaybackController.Shuffle:
                    return Theme.icon("shuffle-fill")
                default:
                    return Theme.icon("repeat-fill")
                }
            }
            icon.color: playback.mode === PlaybackController.Sequential
                        ? Theme.secondaryText
                        : Theme.cyan
            icon.width: 16
            icon.height: 16
            Accessible.name: {
                switch (playback.mode) {
                case PlaybackController.RepeatOne:
                    return qsTr("Repeat one")
                case PlaybackController.Shuffle:
                    return qsTr("Shuffle")
                default:
                    return qsTr("Sequential")
                }
            }
            focusPolicy: Qt.StrongFocus
            onClicked: playback.cycleMode()
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

        // --- Volume: mute toggle + compact slider ---
        ToolButton {
            id: muteButton
            icon.source: playback.muted
                         ? Theme.icon("volume-mute-fill")
                         : Theme.icon("volume-up-fill")
            icon.color: Theme.secondaryText
            icon.width: 16
            icon.height: 16
            Accessible.name: playback.muted ? qsTr("Unmute") : qsTr("Mute")
            focusPolicy: Qt.StrongFocus
            onClicked: playback.toggleMuted()
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
            id: volumeSlider
            from: 0
            to: 1
            onMoved: playback.setVolume(value)
            Layout.preferredWidth: 80
            Accessible.name: qsTr("Volume")
            focusPolicy: Qt.StrongFocus

            Binding on value {
                value: playback.muted ? 0 : playback.volume
                restoreMode: Binding.RestoreBindingOrValue
            }
        }
    }
}
