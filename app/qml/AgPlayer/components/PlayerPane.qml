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

    function formatTime(ms): string {
        if (ms <= 0)
            return "00:00"
        var totalSec = Math.floor(ms / 1000)
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
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

            Text {
                text: root.currentTrackValue(LibraryModel.TitleRole) || qsTr("No track loaded")
                color: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 22
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

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
                    return parts.join("  -  ")
                }
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 14
                elide: Text.ElideRight
                Layout.fillWidth: true
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
                        var sr = root.currentTrackValue(LibraryModel.SampleRateRole)
                        if (sr > 0)
                            badges.push((sr / 1000) + " kHz")
                        var bd = root.currentTrackValue(LibraryModel.BitDepthRole)
                        if (bd > 0)
                            badges.push(bd + "-bit")
                        var br = root.currentTrackValue(LibraryModel.BitRateRole)
                        if (br > 0)
                            badges.push(Math.round(br / 1000) + " kbps")
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
