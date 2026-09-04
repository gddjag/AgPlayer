import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "playerPane"
    color: "transparent"
    property var waveformSession: null
    readonly property var frequencyWaveformSettings:
        SettingsController.frequencyColorWaveform
    property var rawWaveformLayers: ({})
    property var spectrumVisual: []
    property real waveformDurationMs: 0
    property real waveformGeneration: 0
    readonly property bool compactHeight: height < 230
    readonly property bool minimalHeight: height < 150
    readonly property real requestedWaveformHeight:
        SettingsController.waveformCanvasLocked
        ? Math.min(SettingsController.waveformCanvasHeight,
                   Math.max(minimalHeight ? 32 : 48, height * 0.32))
        : Math.max(minimalHeight ? 32 : 48,
                   Math.min(minimalHeight ? 42 : 84, height * 0.22))
    readonly property real headerHeight:
        Math.max(minimalHeight ? 54 : 86,
                 Math.min(136, height - requestedWaveformHeight
                          - (minimalHeight ? 12 : 16)))
    // Full PCM analysis is the exact waveform clock. Container metadata may
    // include encoder padding and would stretch beat positions across pixels.
    readonly property real effectiveDurationMs: waveformDurationMs > 0
                                                ? waveformDurationMs
                                                : PlaybackController.durationMs
    readonly property real visualPlaybackPositionMs: {
        if (effectiveDurationMs <= 0)
            return 0
        return Math.max(0, Math.min(effectiveDurationMs,
            PlaybackController.positionMs))
    }
    property int libraryRevision: 0


    function currentRow(): int {
        return LibraryModel.indexForTrackId(PlaybackController.currentTrackId)
    }

    function currentTrackValue(role): variant {
        var revision = root.libraryRevision
        var track = LibraryModel.trackForId(PlaybackController.currentTrackId)
        if (track) {
            if (role === LibraryModel.ArtistRole)
                return track.artist || ""
            if (role === LibraryModel.AlbumRole)
                return track.album || ""
            if (role === LibraryModel.TitleRole)
                return track.title || ""
            if (role === LibraryModel.TagsRole)
                return track.tags || []
        }
        var row = root.currentRow()
        if (row < 0)
            return ""
        return LibraryModel.data(LibraryModel.index(row, 0), role)
    }

    function currentTrackRating(): int {
        var value = parseInt(root.currentTrackValue(LibraryModel.RatingRole), 10)
        return isNaN(value) ? 0 : Math.max(0, Math.min(5, value))
    }

    function currentTrackTags(): string {
        var tags = root.currentTrackValue(LibraryModel.TagsRole)
        if (!tags || tags.length === 0)
            return ""
        return Array.isArray(tags) ? tags.join("、") : String(tags)
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

    function formatBpm(value): string {
        value = parseFloat(value)
        if (isNaN(value) || value <= 0)
            return ""
        var rounded = Math.round(value * 10) / 10
        return (Math.abs(rounded - Math.round(rounded)) < 0.001
                ? Math.round(rounded).toString() : rounded.toFixed(1)) + " BPM"
    }

    function currentTrackBpm(): string {
        return root.formatBpm(root.currentTrackValue(LibraryModel.BpmRole))
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
        var result = new Array(half * 2)
        for (var index = 0; index < half; ++index) {
            var sourceIndex = Math.min(
                source.length - 1,
                Math.floor(index * source.length / half))
            var target = Math.min(1, Math.max(0,
                              Math.sqrt(Number(source[sourceIndex]) || 0) * 1.35))
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
            playedWaveform.peaks = root.spectrumVisual
            return
        }
        waveform.layers = root.rawWaveformLayers || ({})
        playedWaveform.layers = root.rawWaveformLayers || ({})
    }

    function loadWaveform() {
        if (root.waveformSession) {
            root.rawWaveformLayers = root.waveformSession.layers || ({})
            root.waveformDurationMs = root.waveformSession.durationMs || 0
            root.applyWaveformMode()
            return
        }
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
        playedWaveform.layers = {}
        playedWaveform.peaks = []
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

                FallbackCoverImage {
                    id: playerCoverImage
                    objectName: "playerCoverImage"
                    requestedSource: root.coverSource()
                    anchors.fill: parent
                    anchors.margins: usingFallback ? Theme.spacingMd : 0
                    sourceSize.width: 180
                    sourceSize.height: 180
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                // At the native minimum height all metadata rows remain in
                // the layout; only their spacing and font scale contract.
                spacing: root.minimalHeight ? 1 : Theme.spacingSm

                Item {
                    id: titleRow
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.minimalHeight ? 24 : 36

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
                                  || qsTr("未载入歌曲")
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: root.minimalHeight
                                            ? Theme.fontSizeBodyStrong
                                            : Theme.fontSizePageTitle
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
                        width: root.minimalHeight ? 24 : 36
                        height: width
                        flat: true
                        icon.source: root.currentTrackFavorite()
                                     ? Theme.icon("heart-fill")
                                     : Theme.icon("heart-line")
                        icon.color: root.currentTrackFavorite()
                                    ? Theme.favoriteRed
                                    : Theme.secondaryText
                        icon.width: root.minimalHeight ? 15 : 22
                        icon.height: root.minimalHeight ? 15 : 22
                        Accessible.name: root.currentTrackFavorite()
                                         ? qsTr("取消收藏")
                                         : qsTr("添加收藏")
                        focusPolicy: Qt.StrongFocus
                        enabled: root.currentRow() >= 0
                        onClicked: root.toggleCurrentFavorite()
                        ToolTip.text: Accessible.name
                        ToolTip.visible: hovered
                        background: null
                    }
                }

                Item {
                    id: artistRatingRow
                    objectName: "trackArtistRatingRow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.minimalHeight ? 13 : 18
                    visible: true

                    Item {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: parent.height

                        Item {
                            id: artistAlbumClip
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(
                                       artistAlbumText.implicitWidth,
                                       Math.max(0, parent.width
                                                - trackRating.implicitWidth
                                                - Theme.spacingMd))
                            height: artistAlbumText.implicitHeight
                            clip: true
                            Text {
                                id: artistAlbumText
                                objectName: "trackArtistAlbum"
                                property string artist: root.currentTrackValue(
                                                            LibraryModel.ArtistRole)
                                property string album: root.currentTrackValue(
                                                           LibraryModel.AlbumRole)
                                property string tags: root.currentTrackTags()
                                text: (artist || qsTr("未知艺术家"))
                                      + " · "
                                      + (album || qsTr("未知专辑"))
                                      + (tags.length > 0 ? " · " + tags : "")
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: root.minimalHeight
                                                ? Theme.fontSizeCaption
                                                : Theme.fontSizeBody
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }

                        Row {
                            id: trackRating
                            objectName: "trackRating"
                            readonly property int iconSize:
                                root.minimalHeight ? 12 : 19
                            anchors.left: artistAlbumClip.right
                            anchors.leftMargin: Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            visible: true

                            Repeater {
                                model: 5
                                delegate: ThemedIcon {
                                    objectName: "playerRatingStar" + index
                                    source: index < root.currentTrackRating()
                                            ? Theme.icon("star-fill")
                                            : Theme.icon("star-line")
                                    tint: index < root.currentTrackRating()
                                          ? Theme.ratingColor(index) : Theme.iconSecondary
                                    sourceSize.width: trackRating.iconSize
                                    sourceSize.height: trackRating.iconSize
                                    width: trackRating.iconSize
                                    height: trackRating.iconSize
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    id: metadataBadges
                    objectName: "trackMetadataBadges"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.minimalHeight ? 14 : 22
                    spacing: root.minimalHeight ? 2 : Theme.spacingSm
                    visible: true

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
                                           + (root.minimalHeight ? 6 : Theme.spacingMd * 2)
                            implicitHeight: badgeText.implicitHeight
                                            + (root.minimalHeight ? 2 : Theme.spacingXs * 2)

                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: modelData
                                color: Theme.secondaryText
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeCaption
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
            readonly property real hoverPreviewMs: waveform.hoverPosition
            readonly property real playbackX: waveform.waveformCursorX

            WaveformItem {
                id: waveform
                objectName: "mainWaveform"
                anchors.fill: parent
                // The overlay owns pointer input. Keeping the renderer passive
                // prevents a click from being converted twice with different
                // item coordinates.
                pointerInteractionEnabled: false
                // Keep this base pass entirely unplayed. The played pass is
                // clipped below at the exact playback pixel, avoiding the
                // visible bucket-by-bucket progress jump of peak colouring.
                position: 0
                cursorPosition: root.visualPlaybackPositionMs
                duration: root.effectiveDurationMs
                analysisProgress: WaveformProvider.analysisProgress
                visualMode: SettingsController.waveformMode
                baseColor: SettingsController.waveformMode === 0
                           ? SettingsController.waveformSolidBaseColor
                           : SettingsController.waveformRgbBaseColor
                progressColor: SettingsController.waveformSolidProgressColor
                gradientStartColor: SettingsController.waveformMode === 2
                                    ? (SettingsController.spectrumColorMode === 0
                                       ? SettingsController.spectrumSolidColor
                                       : SettingsController.spectrumRgbStartColor)
                                    : SettingsController.waveformRgbStartColor
                gradientMiddleColor: SettingsController.waveformMode === 2
                                     ? (SettingsController.spectrumColorMode === 0
                                        ? SettingsController.spectrumSolidColor
                                        : SettingsController.spectrumRgbMiddleColor)
                                     : SettingsController.waveformRgbMiddleColor
                gradientEndColor: SettingsController.waveformMode === 2
                                  ? (SettingsController.spectrumColorMode === 0
                                     ? SettingsController.spectrumSolidColor
                                     : SettingsController.spectrumRgbEndColor)
                                  : SettingsController.waveformRgbEndColor
                lowColor: root.frequencyWaveformSettings.lowColor
                midColor: root.frequencyWaveformSettings.midColor
                highColor: root.frequencyWaveformSettings.highColor
                frequencyUnplayedOpacity: Theme.nonImmersiveSpectralUnplayedOpacity
                rgbProgress: SettingsController.waveformMode === 1
                             && SettingsController.waveformRgbProgress
                amplitudeScale: SettingsController.waveformMode === 2
                                ? 1.0 : SettingsController.waveformHeight
                density: SettingsController.waveformMode === 2
                         ? 1.0 : SettingsController.waveformDensity
                lineWidth: SettingsController.waveformMode === 2
                            ? 3.0 : SettingsController.waveformThickness
                onSeekRequested: positionMs => PlaybackController.seek(positionMs)
            }

            Item {
                id: playedWaveformClip
                objectName: "waveformPlayedClip"
                // Every mode uses the same continuous C++ time-to-pixel clip.
                // Frequency colour stays unchanged; the clipped pass only
                // restores full opacity across the exact playback boundary.
                visible: true
                width: waveformFrame.playbackX
                height: parent.height
                clip: true
                enabled: false

                WaveformItem {
                    id: playedWaveform
                    objectName: "playedWaveform"
                    enabled: false
                    width: waveformFrame.width
                    height: waveformFrame.height
                    duration: root.effectiveDurationMs
                    position: root.effectiveDurationMs
                    analysisProgress: WaveformProvider.analysisProgress
                    visualMode: SettingsController.waveformMode
                    baseColor: waveform.baseColor
                    progressColor: waveform.progressColor
                    gradientStartColor: waveform.gradientStartColor
                    gradientMiddleColor: waveform.gradientMiddleColor
                    gradientEndColor: waveform.gradientEndColor
                    lowColor: root.frequencyWaveformSettings.lowColor
                    midColor: root.frequencyWaveformSettings.midColor
                    highColor: root.frequencyWaveformSettings.highColor
                    frequencyUnplayedOpacity: Theme.nonImmersiveSpectralUnplayedOpacity
                    rgbProgress: waveform.rgbProgress
                    amplitudeScale: waveform.amplitudeScale
                    density: waveform.density
                    lineWidth: waveform.lineWidth
                }
            }

            // The renderer samples the first bucket exactly on x=0.  A loud
            // first bucket is clipped against the container edge and reads as
            // a solid border instead of waveform content.  Mask only that
            // clipped pixel; time/pixel mapping and the analysed peaks remain
            // unchanged.
            Rectangle {
                id: waveformLeftEdgeMask
                objectName: "waveformLeftEdgeMask"
                x: 0
                width: 1
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: Theme.isLight ? "#ffffff" : "#000000" // theme-color-allow: waveform edge mask
                z: 4
            }

            MouseArea {
                id: waveformInteractionSurface
                objectName: "waveformInteractionSurface"
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                z: 5

                function updatePreviewAt(x) {
                    waveform.setHoverPositionForInteraction(
                                waveform.timeForX(x))
                }

                function updatePreview(mouse) { updatePreviewAt(mouse.x) }

                onPositionChanged: mouse => updatePreview(mouse)
                onEntered: updatePreviewAt(mouseX)
                onPressed: mouse => updatePreview(mouse)
                onReleased: mouse => {
                    updatePreview(mouse)
                    PlaybackController.seek(waveform.timeForX(mouse.x))
                }
                onExited: waveform.setHoverPositionForInteraction(-1)
            }

            Rectangle {
                id: waveformHoverGuide
                objectName: "waveformHoverGuide"
                visible: SettingsController.waveformHoverTimePreview
                         && waveformFrame.hoverPreviewMs >= 0
                x: Math.max(0, Math.min(parent.width - width,
                                        waveform.pixelForTime(
                                            waveformFrame.hoverPreviewMs)))
                width: 1
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: "#54ff84" // theme-color-allow: waveform hover guide
                opacity: 0.96
            }

            Rectangle {
                objectName: "waveformHoverTimeCapsule"
                visible: SettingsController.waveformHoverTimePreview
                         && waveformFrame.hoverPreviewMs >= 0
                x: Math.max(0, Math.min(
                                waveformFrame.width - width,
                                 (root.effectiveDurationMs > 0
                                  ? waveform.pixelForTime(
                                        waveformFrame.hoverPreviewMs)
                                 : 0) - width / 2))
                y: 2
                width: hoverTime.implicitWidth + 12
                height: hoverTime.implicitHeight + 6
                radius: height / 2
                color: Theme.panel
                border.color: "#54ff84" // theme-color-allow: waveform hover guide
                z: 7

                Text {
                    id: hoverTime
                    anchors.centerIn: parent
                    text: root.formatTime(waveformFrame.hoverPreviewMs)
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
                }
            }

            Rectangle {
                id: waveformPlaybackGuide
                objectName: "waveformPlaybackGuide"
                visible: SettingsController.waveformPlaybackGuide
                x: waveform.waveformCursorX
                width: 1
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: "#002fa7" // theme-color-allow: waveform playback guide
                z: 10
            }

        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.minimalHeight ? 12 : 16

            Text {
                text: root.formatTime(PlaybackController.positionMs)
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.formatTime(PlaybackController.durationMs)
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeCaption
            }
        }
    }

    Connections {
        target: PlaybackController
        function onCurrentTrackIdChanged() {
            if (!root.waveformSession)
                root.loadWaveform()
        }
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
        enabled: !root.waveformSession
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
        target: root.waveformSession
        function onLayersChanged() { root.loadWaveform() }
        function onDurationMsChanged() { root.loadWaveform() }
    }

    Connections {
        target: SettingsController
        function onWaveformModeChanged() { root.applyWaveformMode() }
        function onWaveformPeakAlgorithmChanged() {
            if (!root.waveformSession)
                root.loadWaveform()
        }
    }

    Component.onCompleted: {
        root.loadWaveform()
    }
}
