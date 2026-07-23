import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.background

    function currentTrackValue(role): variant {
        var row = PlaybackController.trackIndex
        if (row < 0 || row >= LibraryModel.rowCount())
            return ""
        var idx = LibraryModel.index(row, 0)
        return LibraryModel.data(idx, role)
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

    function currentTrackFavorite(): bool {
        var row = PlaybackController.trackIndex
        if (row < 0 || row >= LibraryModel.rowCount())
            return false
        var idx = LibraryModel.index(row, 0)
        return LibraryModel.data(idx, LibraryModel.FavoriteRole)
    }

    function toggleCurrentFavorite() {
        var row = PlaybackController.trackIndex
        if (row >= 0 && row < LibraryModel.rowCount()) {
            LibraryModel.setFavorite(row, !root.currentTrackFavorite())
        }
    }

    function formatTime(ms): string {
        if (ms <= 0)
            return "00:00"
        var totalSec = Math.floor(ms / 1000)
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
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

    function currentTrackBpm(): string {
        if (typeof LibraryModel.BpmRole === "undefined")
            return ""
        var raw = root.currentTrackValue(LibraryModel.BpmRole)
        var value = parseFloat(raw)
        if (isNaN(value) || value <= 0)
            return ""
        return Math.round(value) + " BPM"
    }

    function coverSource(): string {
        var url = root.currentTrackValue(LibraryModel.CoverUrlRole)
        if (url && url !== "")
            return url
        return "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingXl

        Rectangle {
            Layout.preferredWidth: 220
            Layout.preferredHeight: 220
            Layout.alignment: Qt.AlignVCenter
            color: Theme.panel
            radius: Theme.radiusLg
            border.color: Theme.border
            border.width: 1

            Image {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                source: root.coverSource()
                sourceSize.width: 200
                sourceSize.height: 200
                fillMode: Image.PreserveAspectFit
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingMd

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: root.currentTrackValue(LibraryModel.TitleRole) || qsTr("No track loaded")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    ToolTip.text: text
                    ToolTip.visible: titleHover.hovered && text !== qsTr("No track loaded")
                    ToolTip.delay: 500

                    HoverHandler {
                        id: titleHover
                    }
                }

                ToolButton {
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
                    onClicked: root.toggleCurrentFavorite()
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
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    property string artist: root.currentTrackValue(LibraryModel.ArtistRole)
                    property string album: root.currentTrackValue(LibraryModel.AlbumRole)
                    text: {
                        var parts = []
                        if (artist && artist.length > 0)
                            parts.push(artist)
                        if (album && album.length > 0)
                            parts.push(album)
                        if (parts.length === 0)
                            return qsTr("Unknown artist")
                        return parts.join("  ·  ")
                    }
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    ToolTip.text: text
                    ToolTip.visible: artistAlbumHover.hovered
                    ToolTip.delay: 500

                    HoverHandler {
                        id: artistAlbumHover
                    }
                }

                RowLayout {
                    spacing: 1
                    Layout.alignment: Qt.AlignVCenter
                    visible: PlaybackController.trackIndex >= 0

                    Repeater {
                        model: 5
                        delegate: Image {
                            source: index < root.currentTrackRating()
                                    ? Theme.icon("star-fill")
                                    : Theme.icon("star-line")
                            sourceSize.width: 12
                            sourceSize.height: 12
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                }
            }

            RowLayout {
                spacing: Theme.spacingSm
                Layout.fillWidth: true

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
                        if (PlaybackController.trackIndex >= 0) {
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
                        radius: Theme.radiusSm
                        implicitWidth: badgeText.implicitWidth + Theme.spacingMd * 2
                        implicitHeight: badgeText.implicitHeight + Theme.spacingXs * 2

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            WaveformItem {
                id: waveform
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                Layout.minimumHeight: 48
                position: PlaybackController.positionMs
                duration: PlaybackController.durationMs
                clip: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: root.formatTime(PlaybackController.positionMs)
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: root.formatTime(PlaybackController.durationMs)
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }
            }
        }
    }
}
