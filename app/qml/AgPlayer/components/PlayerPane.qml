import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.background
    property var rawWaveformLayers: ({})
    property int libraryRevision: 0

    function currentRow(): int {
        return LibraryModel.indexForTrackId(PlaybackController.currentTrackId)
    }

    function currentTrackValue(role): variant {
        var revision = root.libraryRevision
        var row = root.currentRow()
        if (row < 0)
            return ""
        return LibraryModel.data(LibraryModel.index(row, 0), role)
    }

    function currentTrackRating(): int {
        var value = parseInt(root.currentTrackValue(LibraryModel.RatingRole), 10)
        return isNaN(value) ? 0 : Math.max(0, Math.min(5, value))
    }

    function currentTrackFavorite(): bool {
        var revision = root.libraryRevision
        var row = root.currentRow()
        return row >= 0
                && LibraryModel.data(LibraryModel.index(row, 0),
                                     LibraryModel.FavoriteRole)
    }

    function toggleCurrentFavorite() {
        var row = root.currentRow()
        if (row >= 0)
            LibraryModel.setFavorite(row, !root.currentTrackFavorite())
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
        var value = parseFloat(root.currentTrackValue(LibraryModel.BpmRole))
        return isNaN(value) || value <= 0 ? "" : Math.round(value) + " BPM"
    }

    function coverUrlText(): string {
        var url = root.currentTrackValue(LibraryModel.CoverUrlRole)
        return url ? url.toString() : ""
    }

    function coverSource(): string {
        var url = root.coverUrlText()
        return url.length > 0
                ? url
                : "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
    }

    function applyWaveformMode() {
        var source = root.rawWaveformLayers || {}
        if (SettingsController.waveformMode === 2) {
            waveform.layers = {
                bass: source.bass || [],
                mid: source.mid || [],
                high: source.high || []
            }
        } else {
            waveform.layers = { mix: source.mix || [] }
        }
    }

    function loadWaveform() {
        var path = root.currentTrackValue(LibraryModel.PathRole)
        if (!path || path.length === 0) {
            root.rawWaveformLayers = {}
            waveform.layers = {}
            return
        }
        WaveformProvider.loadForTrack(path)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingXl
        anchors.rightMargin: Theme.spacingXl
        anchors.topMargin: Theme.spacingSm
        anchors.bottomMargin: Theme.spacingXs
        spacing: Theme.spacingSm

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            spacing: Theme.spacingXl

            Rectangle {
                objectName: "playerCover"
                Layout.preferredWidth: 150
                Layout.preferredHeight: 150
                color: Theme.panel
                radius: Theme.radiusMd
                border.color: Theme.border
                border.width: 1
                clip: true

                Image {
                    objectName: "playerCoverImage"
                    anchors.fill: parent
                    anchors.margins: root.coverUrlText().length > 0
                                     ? 0 : Theme.spacingMd
                    source: root.coverSource()
                    sourceSize.width: 180
                    sourceSize.height: 180
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: Theme.spacingSm
                spacing: Theme.spacingSm

                RowLayout {
                    id: titleRow
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        objectName: "trackTitle"
                        text: root.currentTrackValue(LibraryModel.TitleRole)
                              || qsTr("No track loaded")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.preferredWidth: Math.min(implicitWidth,
                                                        Math.max(0,
                                                                 titleRow.width
                                                                 - favoriteButton.implicitWidth
                                                                 - titleRow.spacing))
                        Layout.maximumWidth: Layout.preferredWidth
                    }

                    ToolButton {
                        id: favoriteButton
                        objectName: "favoriteButton"
                        flat: true
                        icon.source: root.currentTrackFavorite()
                                     ? Theme.icon("heart-fill")
                                     : Theme.icon("heart-line")
                        icon.color: root.currentTrackFavorite()
                                    ? Theme.favoriteRed
                                    : Theme.secondaryText
                        icon.width: 22
                        icon.height: 22
                        Accessible.name: root.currentTrackFavorite()
                                         ? qsTr("Remove from favorites")
                                         : qsTr("Add to favorites")
                        focusPolicy: Qt.StrongFocus
                        enabled: root.currentRow() >= 0
                        onClicked: root.toggleCurrentFavorite()
                        ToolTip.text: Accessible.name
                        ToolTip.visible: hovered
                        background: null
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMd

                    Text {
                        objectName: "trackArtistAlbum"
                        property string artist: root.currentTrackValue(
                                                    LibraryModel.ArtistRole)
                        property string album: root.currentTrackValue(
                                                   LibraryModel.AlbumRole)
                        text: {
                            var parts = []
                            if (artist)
                                parts.push(artist)
                            if (album)
                                parts.push(album)
                            return parts.length > 0
                                    ? parts.join("  ·  ")
                                    : qsTr("Unknown artist")
                        }
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        objectName: "trackRating"
                        spacing: 1
                        visible: root.currentRow() >= 0

                        Repeater {
                            model: 5
                            delegate: ThemedIcon {
                                source: index < root.currentTrackRating()
                                        ? Theme.icon("star-fill")
                                        : Theme.icon("star-line")
                                tint: index < root.currentTrackRating()
                                      ? Theme.ratingGold : Theme.iconSecondary
                                sourceSize.width: 14
                                sourceSize.height: 14
                                Layout.preferredWidth: 15
                                Layout.preferredHeight: 15
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Repeater {
                        model: {
                            var badges = []
                            var format = root.currentTrackValue(LibraryModel.FormatRole)
                            if (format)
                                badges.push(format.toUpperCase())
                            var depth = root.currentTrackValue(LibraryModel.BitDepthRole)
                            if (depth > 0)
                                badges.push(depth + "-bit")
                            var rate = root.currentTrackValue(LibraryModel.SampleRateRole)
                            if (rate > 0)
                                badges.push((rate / 1000) + " kHz")
                            var bitRate = root.currentTrackValue(LibraryModel.BitRateRole)
                            if (bitRate > 0)
                                badges.push(Math.round(bitRate / 1000) + " kbps")
                            var bpm = root.currentTrackBpm()
                            if (bpm)
                                badges.push(bpm)
                            var size = root.currentTrackValue(LibraryModel.FileSizeRole)
                            if (size > 0)
                                badges.push(root.formatFileSize(size))
                            return badges
                        }

                        Rectangle {
                            color: "transparent"
                            border.color: Theme.border
                            border.width: 1
                            radius: Theme.radiusSm
                            implicitWidth: badgeText.implicitWidth
                                           + Theme.spacingMd * 2
                            implicitHeight: badgeText.implicitHeight
                                            + Theme.spacingXs * 2

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

                Item { Layout.fillHeight: true }
            }
        }

        WaveformItem {
            id: waveform
            objectName: "mainWaveform"
            Layout.fillWidth: true
            Layout.preferredHeight: 94
            Layout.minimumHeight: 64
            position: PlaybackController.positionMs
            duration: PlaybackController.durationMs
            analysisProgress: WaveformProvider.analysisProgress
            clip: true
            onSeekRequested: positionMs => PlaybackController.seek(positionMs)

            Binding on waveformColor {
                value: Theme.cyan
                when: SettingsController.waveformMode === 0
                restoreMode: Binding.RestoreBindingOrValue
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 20

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

    Connections {
        target: PlaybackController
        function onCurrentTrackIdChanged() { root.loadWaveform() }
    }

    Connections {
        target: LibraryModel
        function onDataChanged() { ++root.libraryRevision }
        function onModelReset() { ++root.libraryRevision }
    }

    Connections {
        target: WaveformProvider
        function onWaveformReady(path, layers) {
            if (path === root.currentTrackValue(LibraryModel.PathRole)) {
                root.rawWaveformLayers = layers
                root.applyWaveformMode()
            }
        }
    }

    Connections {
        target: SettingsController
        function onWaveformModeChanged() { root.applyWaveformMode() }
    }

    Component.onCompleted: root.loadWaveform()
}
