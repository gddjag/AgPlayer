import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "playerPane"
    color: "transparent"
    property var rawWaveformLayers: ({})
    property var spectrumVisual: []
    property real waveformDurationMs: 0
    property real waveformGeneration: 0
    readonly property bool compactHeight: height < 230
    readonly property bool minimalHeight: height < 150
    readonly property real requestedWaveformHeight:
        SettingsController.waveformCanvasLocked
        ? SettingsController.waveformCanvasHeight
        : Math.max(minimalHeight ? 32 : 48,
                   Math.min(minimalHeight ? 42 : 84, height * 0.22))
    readonly property real headerHeight:
        Math.max(minimalHeight ? 54 : 86,
                 Math.min(136, height - requestedWaveformHeight
                          - (minimalHeight ? 12 : 16)))
    // The active decoder owns the playback clock.  Waveform analysis is only
    // a picture of that clock: using its duration for seeking can leave a VBR
    // tail after the final audible frame.
    readonly property real effectiveDurationMs: PlaybackController.durationMs > 0
                                                ? PlaybackController.durationMs
                                                : waveformDurationMs
    readonly property real visualPlaybackPositionMs: {
        var duration = effectiveDurationMs
        if (duration <= 0)
            return 0
        return Math.max(0, Math.min(duration, PlaybackController.positionMs))
    }
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

    function shapeSpectrum(values) {
        var source = values || []
        if (source.length === 0)
            return []
        var half = 64
        var sourcePeak = 0
        for (var sourceOffset = 0; sourceOffset < source.length; ++sourceOffset)
            sourcePeak = Math.max(sourcePeak, Number(source[sourceOffset]) || 0)
        var gain = sourcePeak > 0 ? Math.max(1, 1.0 / sourcePeak) : 0
        var result = new Array(half * 2)
        for (var index = 0; index < half; ++index) {
            var sourceIndex = Math.min(
                source.length - 1,
                Math.floor(index * source.length / half))
            var target = Math.min(1, Math.max(0,
                              (Number(source[sourceIndex]) || 0) * gain))
            result[index] = target
            result[half * 2 - 1 - index] = target
        }
        return result
    }

    function applyWaveformMode() {
        if (SettingsController.waveformMode === 2) {
            root.spectrumVisual = root.shapeSpectrum(
                PlaybackController.spectrum)
            waveform.peaks = root.spectrumVisual
            return
        }
        var source = root.rawWaveformLayers || {}
        waveform.layers = { mix: source.mix || [] }
    }

    function loadWaveform() {
        var path = root.currentTrackValue(LibraryModel.PathRole)
        var row = root.currentRow()
        var neighbors = []
        if (row > 0)
            neighbors.push(
                LibraryModel.data(LibraryModel.index(row - 1, 0),
                                  LibraryModel.PathRole))
        if (row >= 0 && row + 1 < LibraryModel.count)
            neighbors.push(
                LibraryModel.data(LibraryModel.index(row + 1, 0),
                                  LibraryModel.PathRole))
        root.rawWaveformLayers = {}
        root.waveformDurationMs = 0
        waveform.layers = {}
        waveform.peaks = []
        if (!path || path.length === 0) {
            return
        }
        root.waveformGeneration = WaveformProvider.loadForTrack(
                    PlaybackController.currentTrackId, path)
        WaveformProvider.prefetchTracks(neighbors)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Math.min(8, parent.width / 2)
        anchors.rightMargin: Math.min(8, parent.width / 2)
        anchors.topMargin: 0
        anchors.bottomMargin: 0
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.headerHeight
            spacing: root.minimalHeight ? 12 : root.compactHeight ? 18 : 30

            Rectangle {
                objectName: "playerCover"
                Layout.preferredWidth: Math.min(root.minimalHeight ? 82 : 136,
                                                root.headerHeight - 4)
                    Layout.preferredHeight: Math.min(root.minimalHeight ? 82 : 136, root.headerHeight - 4)
                color: Theme.panel
                radius: 14
                antialiasing: true
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
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.spacingSm

                Item {
                    id: titleRow
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.minimalHeight ? 30 : 36

                    Item {
                        id: titleViewport
                        objectName: "trackTitleViewport"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(titleText.implicitWidth,
                                        Math.max(0, parent.width
                                                 - favoriteButton.width
                                                 - Theme.spacingSm))
                        height: parent.height
                        clip: true

                        Text {
                            id: titleText
                            objectName: "trackTitle"
                            anchors.verticalCenter: parent.verticalCenter
                            x: 0
                            text: root.currentTrackValue(LibraryModel.TitleRole)
                                  || qsTr("No track loaded")
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: root.minimalHeight ? 18
                                            : root.compactHeight ? 22 : 26
                            font.weight: Font.DemiBold
                        }

                        HoverHandler { id: titleHover }

                        SequentialAnimation {
                            running: titleHover.hovered
                                     && titleText.implicitWidth > titleViewport.width
                            loops: Animation.Infinite
                            onRunningChanged: if (!running) titleText.x = 0
                            PauseAnimation { duration: 450 }
                            NumberAnimation {
                                target: titleText
                                property: "x"
                                from: 0
                                to: Math.min(0, titleViewport.width
                                                - titleText.implicitWidth)
                                duration: Math.max(700,
                                                   (titleText.implicitWidth
                                                    - titleViewport.width) * 18)
                                easing.type: Easing.Linear
                            }
                            PauseAnimation { duration: 650 }
                            NumberAnimation {
                                target: titleText
                                property: "x"
                                to: 0
                                duration: 300
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    ToolButton {
                        id: favoriteButton
                        objectName: "favoriteButton"
                        anchors.left: titleViewport.right
                        anchors.leftMargin: Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.minimalHeight ? 30 : 36
                        height: width
                        flat: true
                        icon.source: root.currentTrackFavorite()
                                     ? Theme.icon("heart-fill")
                                     : Theme.icon("heart-line")
                        icon.color: root.currentTrackFavorite()
                                    ? Theme.favoriteRed
                                    : Theme.secondaryText
                        icon.width: root.minimalHeight ? 18 : 22
                        icon.height: root.minimalHeight ? 18 : 22
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
                    visible: !root.minimalHeight

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
                        visible: SettingsController.autoReadRating
                                 && root.currentRow() >= 0

                        Repeater {
                            model: 5
                            delegate: ThemedIcon {
                                source: index < root.currentTrackRating()
                                        ? Theme.icon("star-fill")
                                        : Theme.icon("star-line")
                                tint: index < root.currentTrackRating()
                                      ? Theme.ratingColor(index) : Theme.iconSecondary
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
                    visible: !root.minimalHeight

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

            }
        }

        Item {
            id: waveformFrame
            Layout.fillWidth: true
            Layout.preferredHeight: root.requestedWaveformHeight
            Layout.minimumHeight: root.minimalHeight ? 32 : 48
            clip: true
            property real hoverPreviewMs: -1
            readonly property real playbackX:
                root.effectiveDurationMs > 0
                ? width * root.visualPlaybackPositionMs
                  / root.effectiveDurationMs : 0

            WaveformItem {
                id: waveform
                objectName: "mainWaveform"
                anchors.fill: parent
                position: root.visualPlaybackPositionMs
                duration: root.effectiveDurationMs
                analysisProgress: WaveformProvider.analysisProgress
                visualMode: SettingsController.waveformMode
                baseColor: SettingsController.waveformMode === 0
                           ? SettingsController.waveformSolidBaseColor
                           : (SettingsController.waveformMode === 2
                              ? SettingsController.spectrumSolidColor
                              : SettingsController.waveformRgbBaseColor)
                progressColor: SettingsController.waveformSolidProgressColor
                gradientStartColor: SettingsController.waveformMode === 2
                                    && SettingsController.spectrumColorMode === 0
                                    ? SettingsController.spectrumSolidColor
                                     : SettingsController.spectrumRgbStartColor
                gradientMiddleColor: SettingsController.waveformMode === 2
                                     && SettingsController.spectrumColorMode === 0
                                     ? SettingsController.spectrumSolidColor
                                      : SettingsController.spectrumRgbMiddleColor
                gradientEndColor: SettingsController.waveformMode === 2
                                  && SettingsController.spectrumColorMode === 0
                                  ? SettingsController.spectrumSolidColor
                                   : SettingsController.spectrumRgbEndColor
                rgbProgress: SettingsController.waveformRgbProgress
                amplitudeScale: SettingsController.waveformMode === 2
                                ? 1.0 : SettingsController.waveformHeight
                density: SettingsController.waveformMode === 2
                         ? 1.0 : SettingsController.waveformDensity
                lineWidth: SettingsController.waveformMode === 2
                            ? 3.0 : SettingsController.waveformThickness
                onSeekRequested: positionMs => PlaybackController.seek(positionMs)
            }

            Rectangle {
                objectName: "waveformProgressFeather"
                visible: waveformFrame.playbackX > 1
                         && waveformFrame.playbackX < waveformFrame.width
                x: Math.max(0, waveformFrame.playbackX - width / 2)
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: parent.height
                opacity: 0.38
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.5; color: "#ffffff" }
                    GradientStop { position: 1; color: "transparent" }
                }
            }

            Rectangle {
                id: waveformPlaybackGuide
                objectName: "waveformPlaybackGuide"
                visible: root.effectiveDurationMs > 0
                x: Math.max(0, Math.min(
                                waveformFrame.width - width,
                                Math.round(waveformFrame.playbackX - width / 2)))
                width: 1
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: "#002FA7"
                opacity: 0.96
            }

            Rectangle {
                id: waveformHoverGuide
                objectName: "waveformHoverGuide"
                visible: SettingsController.waveformHoverTimePreview
                         && waveformFrame.hoverPreviewMs >= 0
                x: root.effectiveDurationMs > 0
                   ? Math.round(waveformFrame.hoverPreviewMs
                                / root.effectiveDurationMs
                                * waveformFrame.width)
                   : 0
                width: 1
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: "#54ff84"
                opacity: 0.96
            }

            Rectangle {
                visible: SettingsController.waveformHoverTimePreview
                         && waveformFrame.hoverPreviewMs >= 0
                x: Math.max(0, Math.min(
                                waveformFrame.width - width,
                                (root.effectiveDurationMs > 0
                                 ? waveformFrame.hoverPreviewMs
                                   / root.effectiveDurationMs
                                   * waveformFrame.width
                                 : 0) - width / 2))
                y: 2
                width: hoverTime.implicitWidth + 12
                height: hoverTime.implicitHeight + 6
                radius: height / 2
                color: Theme.panel
                border.color: "#54ff84"

                Text {
                    id: hoverTime
                    anchors.centerIn: parent
                    text: root.formatTime(waveformFrame.hoverPreviewMs)
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 11
                }
            }

            MouseArea {
                id: waveformHoverSurface
                objectName: "waveformHoverSurface"
                anchors.fill: parent
                z: 20
                enabled: true
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.ArrowCursor

                function updatePreview(pointerX) {
                    if (root.effectiveDurationMs <= 0 || width <= 0) {
                        waveformFrame.hoverPreviewMs = -1
                        return
                    }
                    var ratio = Math.max(0, Math.min(1, pointerX / width))
                    waveformFrame.hoverPreviewMs = ratio * root.effectiveDurationMs
                }

                onPositionChanged: function(mouse) {
                    updatePreview(mouse.x)
                    if (pressed) {
                        PlaybackController.seek(waveformFrame.hoverPreviewMs)
                    }
                }
                onEntered: updatePreview(mouseX)
                onExited: waveformFrame.hoverPreviewMs = -1
                onClicked: function(mouse) {
                    updatePreview(mouse.x)
                    PlaybackController.seek(waveformFrame.hoverPreviewMs)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.minimalHeight ? 12 : 16

            Text {
                text: root.formatTime(PlaybackController.positionMs)
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: root.minimalHeight ? 10 : 12
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.formatTime(PlaybackController.durationMs)
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: root.minimalHeight ? 10 : 12
            }
        }
    }

    Connections {
        target: PlaybackController
        function onCurrentTrackIdChanged() { root.loadWaveform() }
        function onSpectrumChanged() {
            if (SettingsController.waveformMode === 2)
                root.applyWaveformMode()
        }
    }

    Connections {
        target: LibraryModel
        function onDataChanged() { ++root.libraryRevision }
        function onModelReset() { ++root.libraryRevision }
    }

    Connections {
        target: WaveformProvider
        function onWaveformReady(path, layers) {
            var responseTrack = String(layers._trackId || "")
            var responseGeneration = Number(layers._generation || 0)
            // Older cache/provider responses are untagged. Path equality is
            // still sufficient to reject a stale result after a track switch.
            var sameTrack = responseTrack.length === 0
                    || responseTrack === String(PlaybackController.currentTrackId)
            // Cache hits are emitted synchronously inside loadForTrack(),
            // before its return value can be assigned in QML. Compare against
            // the provider's authoritative active generation instead.
            var sameGeneration = responseGeneration === 0
                    || responseGeneration === Number(WaveformProvider.activeGeneration)
            if (sameTrack && sameGeneration
                    && path === root.currentTrackValue(LibraryModel.PathRole)) {
                root.rawWaveformLayers = layers
                root.waveformDurationMs = Math.max(
                    0, Number(layers._durationMs) || 0)
                root.applyWaveformMode()
            }
        }
    }

    Connections {
        target: SettingsController
        function onWaveformModeChanged() { root.applyWaveformMode() }
        function onWaveformPeakAlgorithmChanged() { root.loadWaveform() }
    }

    Component.onCompleted: {
        root.loadWaveform()
    }
}
