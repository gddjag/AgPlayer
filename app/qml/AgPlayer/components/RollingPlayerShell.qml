import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: "rollingPlayerShell"

    property var hostWindow: null
    property var playback: PlaybackController
    property var waveformSession: null
    property var libraryModel: LibraryModel
    property var filterModel: null
    property var playlistModel: PlaylistModel
    property var navigationModel: LibraryNavigationModel
    property var tagModel: TagModel
    property var visualFeatures: AudioVisualFeatureController
    property var currentTrack: null
    property real waveformPixelsPerSecond: 120
    property bool scratchGestureActive: false
    property real scratchVisualPositionMs: 0
    property real scratchAnchorPositionMs: 0
    property int libraryTrackRevision: 0
    // Derived from WaveformItem's own coordinate mapper after each viewport
    // update. This keeps the source position on the fixed needle even when
    // the item has a clipped lead-in/out segment at a track boundary.
    property real waveformContentX: 1
    property alias tagSearchText: rollingTagPanel.searchText

    readonly property real effectiveDurationMs:
        waveformSession && Number(waveformSession.durationMs) > 0
        ? Number(waveformSession.durationMs)
        : playback && Number(playback.durationMs) > 0
          ? Number(playback.durationMs) : 0
    readonly property real playbackPositionMs:
        playback ? Number(playback.positionMs) || 0 : 0
    readonly property real sourceBpmValue:
        playback && Number(playback.sourceBpm) > 0
        ? Number(playback.sourceBpm)
        : currentTrack && Number(currentTrack.bpm) > 0
          ? Number(currentTrack.bpm) : 0
    readonly property real viewportCenterMs:
        scratchGestureActive ? scratchVisualPositionMs : playbackPositionMs
    readonly property real viewportSpanMs: {
        if (effectiveDurationMs <= 0)
            return 0
        var widthForTime = Math.max(1, mainWaveformCanvas.width)
        return Math.min(effectiveDurationMs,
                        Math.max(250, widthForTime
                                 / waveformPixelsPerSecond * 1000))
    }
    // Unlike a conventional editor waveform, the rolling deck retains blank
    // lead-in / lead-out space.  This keeps source time under the immovable
    // centre needle even at 0 and at the track end.
    readonly property real viewportStartMs: {
        if (effectiveDurationMs <= 0)
            return 0
        return viewportCenterMs - viewportSpanMs / 2
    }
    readonly property real viewportEndMs:
        effectiveDurationMs <= 0 ? 0 : viewportStartMs + viewportSpanMs
    readonly property real waveformVisibleStartMs:
        Math.round(clamp(viewportStartMs, 0, effectiveDurationMs))
    readonly property real waveformVisibleEndMs:
        Math.round(clamp(viewportEndMs, 0, effectiveDurationMs))
    readonly property real waveformContentStartFraction:
        viewportSpanMs > 0
        ? (waveformVisibleStartMs - viewportStartMs) / viewportSpanMs : 0
    readonly property real waveformContentWidthFraction:
        viewportSpanMs > 0
        ? (waveformVisibleEndMs - waveformVisibleStartMs) / viewportSpanMs
        : 1
    readonly property bool rollingLightTheme:
        SettingsController.themeMode === 1
        || (SettingsController.themeMode === 2 && Theme.isLight)
    readonly property var frequencyWaveformSettings:
        SettingsController.frequencyColorWaveform
    readonly property var metadataBadges: {
        var revision = libraryTrackRevision
        var track = currentTrack
        var badges = []
        if (!track)
            return badges
        if (track.format)
            badges.push(String(track.format).toUpperCase())
        if (Number(track.bitDepth) > 0)
            badges.push(Math.round(Number(track.bitDepth)) + "-bit")
        if (Number(track.sampleRate) > 0)
            badges.push((Number(track.sampleRate) / 1000) + " kHz")
        if (Number(track.bitRate) > 0)
            badges.push(Math.round(Number(track.bitRate) / 1000) + " kbps")
        if (Number(track.bpm) > 0)
            badges.push(formatMetadataNumber(Number(track.bpm)) + " BPM")
        if (Number(track.fileSize) > 0)
            badges.push(formatFileSize(Number(track.fileSize)))
        return badges
    }
    signal openSettingsRequested()
    signal openEqualizerRequested()

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function formatSpeed(value) {
        return Number(value || 1).toFixed(2) + "x"
    }

    function formatBpm(value) {
        return Number(value) > 0 ? Number(value).toFixed(2) : "—"
    }

    function formatTime(milliseconds) {
        var totalSeconds = Math.max(0, Math.floor(Number(milliseconds) / 1000))
        var minutes = Math.floor(totalSeconds / 60)
        var seconds = totalSeconds % 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
                + (seconds < 10 ? "0" : "") + seconds
    }

    function formatMetadataNumber(value) {
        return Math.abs(value - Math.round(value)) < 0.01
                ? Math.round(value).toString() : value.toFixed(1)
    }

    function formatFileSize(bytes) {
        if (bytes < 1024)
            return Math.round(bytes) + " B"
        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) + " KB"
        return (bytes / (1024 * 1024)).toFixed(1) + " MB"
    }

    function signedRateForDrag(deltaX, elapsedMs) {
        var safeElapsed = Math.max(1, Number(elapsedMs) || 0)
        var rate = -Number(deltaX || 0) / safeElapsed * 1000
                   / waveformPixelsPerSecond
        return clamp(rate, -3, 3)
    }

    function deltaMsForPixels(deltaX) {
        return -Number(deltaX || 0) / waveformPixelsPerSecond * 1000
    }

    function alignWaveformToPlayhead() {
        if (!mainWaveform)
            return
        var sourceTime = Math.round(clamp(viewportCenterMs, 0,
                                          effectiveDurationMs))
        waveformContentX = mainWaveformCanvas.width / 2
                - mainWaveform.pixelForTime(sourceTime)
    }

    function syncWaveformViewport() {
        if (!mainWaveform)
            return
        mainWaveform.setVisibleRange(Math.round(waveformVisibleStartMs),
                                     Math.round(waveformVisibleEndMs))
        alignWaveformToPlayhead()
        // setVisibleRange changes the clipped waveform width through QML
        // bindings. Re-align once after those bindings settle.
        Qt.callLater(alignWaveformToPlayhead)
    }

    function adjustSpeed(delta) {
        if (!playback || playback.setSpeedRatio === undefined)
            return
        var next = Math.round(clamp(
                                  Number(playback.speedRatio || 1) + delta,
                                  0.75, 1.50) * 100) / 100
        playback.setSpeedRatio(next)
    }

    function commitTargetBpm(text) {
        if (!playback || playback.setTargetBpm === undefined
                || sourceBpmValue <= 0)
            return
        var value = Number(text)
        if (!isFinite(value) || value < 20 || value > 400)
            return
        playback.setTargetBpm(value)
    }

    function resetRollingTempo() {
        if (playback && playback.resetTempo !== undefined)
            playback.resetTempo()
    }

    function setZoom(value) {
        waveformPixelsPerSecond = clamp(Number(value) || 120, 60, 480)
        syncWaveformViewport()
    }

    function zoomIn() {
        setZoom(waveformPixelsPerSecond * 1.25)
    }

    function zoomOut() {
        setZoom(waveformPixelsPerSecond / 1.25)
    }

    function resetZoom() {
        setZoom(120)
    }

    function finishScratchGesture(cancelled) {
        scratchIdleTimer.stop()
        if (!scratchSurface.scratchStarted)
            return
        if (playback) {
            if (cancelled && playback.cancelScratch !== undefined)
                playback.cancelScratch()
            else if (!cancelled && playback.endScratch !== undefined)
                playback.endScratch()
        }
        scratchSurface.scratchStarted = false
        scratchGestureActive = false
        syncWaveformViewport()
    }

    function cancelScratchGesture() {
        finishScratchGesture(true)
    }

    function meterColor(index, count, active) {
        if (!active)
            return Theme.border
        var ratio = (index + 1) / count
        if (ratio > 0.84)
            return Theme.danger
        if (ratio > 0.62)
            return Theme.warning
        return Theme.success
    }

    function toggleFavorite() {
        if (!currentTrack || !libraryModel
                || libraryModel.indexForTrackId === undefined
                || libraryModel.setFavorite === undefined)
            return
        var row = libraryModel.indexForTrackId(playback.currentTrackId)
        if (row >= 0)
            libraryModel.setFavorite(row, !Boolean(currentTrack.favorite))
    }

    function refreshCurrentTrack() {
        if (!libraryModel || !playback
                || libraryModel.trackForId === undefined) {
            currentTrack = null
            return
        }
        currentTrack = libraryModel.trackForId(playback.currentTrackId)
    }

    onWaveformPixelsPerSecondChanged: syncWaveformViewport()
    onViewportCenterMsChanged: syncWaveformViewport()
    onViewportSpanMsChanged: Qt.callLater(syncWaveformViewport)
    onEffectiveDurationMsChanged: syncWaveformViewport()
    onWaveformSessionChanged: refreshCurrentTrack()
    onLibraryModelChanged: refreshCurrentTrack()
    onPlaybackChanged: refreshCurrentTrack()

    Component.onCompleted: {
        refreshCurrentTrack()
        Qt.callLater(syncWaveformViewport)
    }

    Connections {
        target: root.hostWindow
        ignoreUnknownSignals: true
        function onActiveChanged() {
            if (root.hostWindow && !root.hostWindow.active)
                root.cancelScratchGesture()
        }
    }

    Connections {
        target: root.libraryModel
        ignoreUnknownSignals: true
        function onDataChanged() {
            ++root.libraryTrackRevision
            root.refreshCurrentTrack()
        }
        function onModelReset() {
            ++root.libraryTrackRevision
            root.refreshCurrentTrack()
        }
    }

    Connections {
        target: root.playback
        ignoreUnknownSignals: true
        function onCurrentTrackIdChanged() { root.refreshCurrentTrack() }
    }

    Connections {
        target: root.waveformSession
        ignoreUnknownSignals: true
        function onLibraryRevisionChanged() { root.refreshCurrentTrack() }
    }

    Timer {
        id: scratchIdleTimer
        interval: 35
        repeat: false
        onTriggered: {
            if (scratchSurface.scratchStarted
                    && root.playback
                    && root.playback.updateScratch !== undefined)
                root.playback.updateScratch(0)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        radius: Theme.radiusMd
        border.color: Theme.border
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TitleBar {
            objectName: "rollingTitleBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            window: root.hostWindow
            showBrand: true
            onOpenSettings: root.openSettingsRequested()
        }

        Item {
            id: overviewRegion
            objectName: "rollingOverviewRegion"
            Layout.fillWidth: true
            Layout.preferredHeight: root.height < 460 ? 104 : 116
            Layout.leftMargin: 10
            Layout.rightMargin: 10

            Rectangle {
                id: coverFrame
                objectName: "rollingTrackCover"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: height
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSm
                clip: true

                FallbackCoverImage {
                    objectName: "rollingTrackCoverImage"
                    anchors.fill: parent
                    anchors.margins: usingFallback ? 10 : 0
                    requestedSource: String(root.currentTrack
                                            && root.currentTrack.coverUrl
                                            ? root.currentTrack.coverUrl : "")
                    fillMode: usingFallback ? Image.PreserveAspectFit
                                            : Image.PreserveAspectCrop
                }
            }

            Item {
                id: headerInfo
                anchors.left: coverFrame.right
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.top: parent.top
                height: 52

                Row {
                    anchors.left: parent.left
                    anchors.right: meters.left
                    anchors.rightMargin: 12
                    anchors.top: parent.top
                    height: 28
                    spacing: 8
                    clip: true

                    Text {
                        objectName: "rollingTrackTitle"
                        width: Math.min(implicitWidth, Math.max(
                                            120, parent.width - favoriteButton.width
                                            - ratingRow.width - 22))
                        text: root.currentTrack && root.currentTrack.title
                              ? root.currentTrack.title : qsTr("未选择歌曲")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    ToolButton {
                        id: favoriteButton
                        objectName: "rollingFavoriteButton"
                        width: 28
                        height: 28
                        flat: true
                        icon.source: root.currentTrack
                                     && root.currentTrack.favorite
                                     ? Theme.icon("heart-fill")
                                     : Theme.icon("heart-line")
                        icon.color: root.currentTrack
                                    && root.currentTrack.favorite
                                    ? Theme.danger : Theme.iconPrimary
                        icon.width: 19
                        icon.height: 19
                        onClicked: root.toggleFavorite()
                        background: null
                    }

                    Row {
                        id: ratingRow
                        height: 28
                        spacing: 1
                        Repeater {
                            model: 5
                            ThemedIcon {
                                width: 14
                                height: 14
                                y: (ratingRow.height - height) / 2
                                source: index < (root.currentTrack
                                                 ? Number(root.currentTrack.rating)
                                                 : 0)
                                        ? Theme.icon("star-fill")
                                        : Theme.icon("star-line")
                                tint: index < (root.currentTrack
                                               ? Number(root.currentTrack.rating)
                                               : 0)
                                      ? Theme.warning : Theme.iconSecondary
                            }
                        }
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: 30
                    anchors.right: meters.left
                    anchors.rightMargin: 12
                    text: {
                        var artist = root.currentTrack
                                     && root.currentTrack.artist
                                     ? root.currentTrack.artist : qsTr("未知艺术家")
                        var album = root.currentTrack && root.currentTrack.album
                                    ? root.currentTrack.album : qsTr("未知专辑")
                        return artist + " · " + album
                    }
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Column {
                    id: meters
                    objectName: "rollingStereoMeter"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: Math.min(154, Math.max(112, parent.width * 0.17))
                    spacing: 3

                    Item {
                        id: leftMeter
                        objectName: "rollingLeftMeter"
                        property real peak: root.visualFeatures
                                            ? Number(root.visualFeatures.leftPeak)
                                            : 0
                        property real rms: root.visualFeatures
                                           ? Number(root.visualFeatures.leftRms)
                                           : 0
                        width: parent.width
                        height: 9
                        Row {
                            anchors.fill: parent
                            spacing: 2
                            Repeater {
                                model: 14
                                Rectangle {
                                    width: (leftMeter.width - 26) / 14
                                    height: leftMeter.height
                                    radius: 1
                                    readonly property bool peakActive:
                                        (index + 1) / 14 <= leftMeter.peak
                                    readonly property bool rmsActive:
                                        (index + 1) / 14 <= leftMeter.rms
                                    color: root.meterColor(
                                               index, 14,
                                               peakActive || rmsActive)
                                    opacity: rmsActive ? 1
                                             : peakActive ? 0.72 : 0.28
                                }
                            }
                        }
                    }

                    Item {
                        id: rightMeter
                        objectName: "rollingRightMeter"
                        property real peak: root.visualFeatures
                                            ? Number(root.visualFeatures.rightPeak)
                                            : 0
                        property real rms: root.visualFeatures
                                           ? Number(root.visualFeatures.rightRms)
                                           : 0
                        width: parent.width
                        height: 9
                        Row {
                            anchors.fill: parent
                            spacing: 2
                            Repeater {
                                model: 14
                                Rectangle {
                                    width: (rightMeter.width - 26) / 14
                                    height: rightMeter.height
                                    radius: 1
                                    readonly property bool peakActive:
                                        (index + 1) / 14 <= rightMeter.peak
                                    readonly property bool rmsActive:
                                        (index + 1) / 14 <= rightMeter.rms
                                    color: root.meterColor(
                                               index, 14,
                                               peakActive || rmsActive)
                                    opacity: rmsActive ? 1
                                             : peakActive ? 0.72 : 0.28
                                }
                            }
                        }
                    }
                }
            }

            Row {
                id: badgeRow
                objectName: "rollingMetadataBadges"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 54
                height: 22
                spacing: 5
                Repeater {
                    model: root.metadataBadges
                    Rectangle {
                        width: badgeText.implicitWidth + 12
                        height: 22
                        color: Theme.panel
                        border.color: Theme.border
                        border.width: 1
                        radius: 4
                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.secondaryText
                            font.pixelSize: 10
                        }
                    }
                }
            }

            Item {
                id: overviewWaveformHost
                property real hoverTimeMs: -1
                anchors.left: coverFrame.right
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 76
                anchors.bottom: parent.bottom

                WaveformItem {
                    id: overviewWaveform
                    objectName: "rollingOverviewWaveform"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: overviewProgressTrack.top
                    anchors.bottomMargin: 3
                    layers: root.waveformSession
                            ? root.waveformSession.layers : ({})
                    duration: root.effectiveDurationMs
                    position: 0
                    cursorPosition: -1
                    pointerInteractionEnabled: false
                    visualMode: 3
                    baseColor: SettingsController.waveformRgbBaseColor
                    spectralPalette: root.frequencyWaveformSettings.palette
                    spectralUnplayedOpacity: root.frequencyWaveformSettings.unplayedOpacity
                    amplitudeScale: SettingsController.waveformHeight
                    density: SettingsController.waveformDensity
                    lineWidth: SettingsController.waveformThickness
                }

                Item {
                    id: overviewPlayedClip
                    objectName: "rollingOverviewPlayedClip"
                    anchors.left: overviewWaveform.left
                    anchors.top: overviewWaveform.top
                    width: root.effectiveDurationMs > 0
                           ? overviewWaveform.width * root.clamp(
                                 root.playbackPositionMs
                                 / root.effectiveDurationMs, 0, 1) : 0
                    height: overviewWaveform.height
                    clip: true
                    enabled: false

                    WaveformItem {
                        width: overviewWaveform.width
                        height: overviewWaveform.height
                        layers: overviewWaveform.layers
                        duration: overviewWaveform.duration
                        position: duration
                        cursorPosition: -1
                        pointerInteractionEnabled: false
                        visualMode: 3
                        baseColor: overviewWaveform.baseColor
                        spectralPalette: overviewWaveform.spectralPalette
                        spectralUnplayedOpacity:
                            overviewWaveform.spectralUnplayedOpacity
                        amplitudeScale: overviewWaveform.amplitudeScale
                        density: overviewWaveform.density
                        lineWidth: overviewWaveform.lineWidth
                    }
                }

                Rectangle {
                    id: overviewProgressTrack
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 3
                    radius: 1.5
                    color: Theme.border
                    Rectangle {
                        id: overviewProgress
                        objectName: "rollingOverviewProgress"
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: root.effectiveDurationMs > 0
                               ? parent.width * root.clamp(
                                     root.playbackPositionMs
                                     / root.effectiveDurationMs, 0, 1)
                               : 0
                        radius: parent.radius
                        color: Theme.waveformMagenta
                    }
                }

                MouseArea {
                    id: overviewInteraction
                    objectName: "rollingOverviewInteraction"
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    function updateHover(x) {
                        overviewWaveformHost.hoverTimeMs =
                                root.effectiveDurationMs > 0
                                ? root.clamp(x / Math.max(1, width), 0, 1)
                                  * root.effectiveDurationMs : -1
                    }
                    onPositionChanged: function(mouse) { updateHover(mouse.x) }
                    onEntered: updateHover(mouseX)
                    onExited: overviewWaveformHost.hoverTimeMs = -1
                    onClicked: function(mouse) {
                        if (!root.playback || root.effectiveDurationMs <= 0)
                            return
                        var target = root.clamp(
                                    mouse.x / Math.max(1, width), 0, 1)
                                    * root.effectiveDurationMs
                        if (root.playback.seek !== undefined)
                            root.playback.seek(Math.round(target))
                        if (root.playback.play !== undefined)
                            root.playback.play()
                    }
                }

                Rectangle {
                    id: overviewHoverCapsule
                    objectName: "rollingOverviewHoverCapsule"
                    visible: overviewWaveformHost.hoverTimeMs >= 0
                    x: Math.max(0, Math.min(
                                    parent.width - width,
                                    overviewWaveform.pixelForTime(
                                        overviewWaveformHost.hoverTimeMs)
                                    - width / 2))
                    y: 0
                    width: overviewHoverText.implicitWidth + 12
                    height: overviewHoverText.implicitHeight + 6
                    radius: height / 2
                    color: Theme.panel
                    border.color: Theme.accent
                    z: 8

                    Text {
                        id: overviewHoverText
                        objectName: "rollingOverviewHoverText"
                        anchors.centerIn: parent
                        text: root.formatTime(overviewWaveformHost.hoverTimeMs)
                        color: Theme.primaryText
                        font.pixelSize: 10
                    }
                }
            }
        }

        Rectangle {
            id: mainWaveformCanvas
            objectName: "rollingMainWaveformCanvas"
            Layout.fillWidth: true
            Layout.preferredHeight: root.height < 800 ? 160 : 190
            Layout.minimumHeight: 140
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
            clip: true

            WaveformItem {
                id: mainWaveform
                objectName: "rollingMainWaveform"
                x: root.waveformContentX
                y: 1
                width: Math.max(0, (parent.width - 2)
                                * root.waveformContentWidthFraction)
                height: Math.max(0, parent.height - 2)
                layers: root.waveformSession
                        ? root.waveformSession.layers : ({})
                duration: root.effectiveDurationMs
                position: root.viewportCenterMs
                cursorPosition: -1
                pointerInteractionEnabled: false
                visualMode: 3
                baseColor: SettingsController.waveformRgbBaseColor
                spectralPalette: root.frequencyWaveformSettings.palette
                spectralUnplayedOpacity: root.frequencyWaveformSettings.unplayedOpacity
                amplitudeScale: Math.max(
                                    0.9,
                                    SettingsController.waveformHeight * 1.25)
                density: SettingsController.waveformDensity
                lineWidth: SettingsController.waveformThickness
            }

            Rectangle {
                id: centerPlayhead
                objectName: "rollingCenterPlayhead"
                // The 1-DIP indicator must use a deterministic integer pixel
                // position on odd widths as well.
                x: Math.round((parent.width - width) / 2)
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                width: 1
                color: Theme.primaryText
                opacity: 0.95
                z: 5

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: -2
                    width: 5
                    height: 5
                    radius: 2.5
                    color: Theme.primaryText
                }
            }

            Label {
                id: scratchStatus
                objectName: "rollingScratchStatus"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                visible: root.playback
                         && Boolean(root.playback.scratchBuffering)
                text: qsTr("搓碟缓冲中")
                color: Theme.primaryText
                font.pixelSize: 11
                padding: 6
                background: Rectangle {
                    color: Theme.surfaceElevated
                    border.color: Theme.warning
                    border.width: 1
                    radius: Theme.radiusSm
                }
                z: 7
            }

            MouseArea {
                id: scratchSurface
                objectName: "rollingScratchSurface"
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                cursorShape: pressed ? Qt.ClosedHandCursor
                                     : Qt.OpenHandCursor
                property real pressX: 0
                property real lastX: 0
                property double lastTimestamp: 0
                property bool scratchStarted: false

                onPressed: function(mouse) {
                    scratchIdleTimer.stop()
                    pressX = mouse.x
                    lastX = mouse.x
                    lastTimestamp = Date.now()
                    scratchStarted = false
                    root.scratchGestureActive = false
                    root.scratchAnchorPositionMs = root.playbackPositionMs
                    root.scratchVisualPositionMs = root.playbackPositionMs
                }

                onPositionChanged: function(mouse) {
                    if (!pressed || !root.playback)
                        return
                    var totalDelta = mouse.x - pressX
                    if (!scratchStarted && Math.abs(totalDelta) > 4) {
                        if (root.playback.beginScratch === undefined
                                || !root.playback.beginScratch())
                            return
                        scratchStarted = true
                        root.scratchGestureActive = true
                        root.scratchAnchorPositionMs =
                                root.playbackPositionMs
                        root.scratchVisualPositionMs =
                                root.playbackPositionMs
                        // A motion event can be the last event while the
                        // pointer remains held. Start the stop-watch at the
                        // moment the deck enters Scratch as well.
                        scratchIdleTimer.restart()
                    }
                    if (!scratchStarted)
                        return

                    var now = Date.now()
                    var delta = mouse.x - lastX
                    var elapsed = Math.max(1, now - lastTimestamp)
                    root.scratchVisualPositionMs = root.clamp(
                                root.scratchVisualPositionMs
                                + root.deltaMsForPixels(delta),
                                0, root.effectiveDurationMs)
                    if (root.playback.updateScratch !== undefined)
                        root.playback.updateScratch(
                                    root.signedRateForDrag(delta, elapsed))
                    scratchIdleTimer.restart()
                    lastX = mouse.x
                    lastTimestamp = now
                    root.syncWaveformViewport()
                }

                onReleased: root.finishScratchGesture(false)
                onCanceled: root.cancelScratchGesture()
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "rollingBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 6
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 10
                spacing: 8

                PlayerControls {
                    objectName: "playerControls"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 520
                    Layout.fillHeight: true
                    shellMode: 2
                    centerTransport: false
                    showWaveformMode: false
                    onOpenEqualizerRequested:
                        root.openEqualizerRequested()
                }

                RowLayout {
                    id: rollingControls
                    objectName: "rollingTempoControls"
                    // At the 1000 DIP minimum, reserve the full width for
                    // the shared controls rather than silently hiding their
                    // waveform/EQ/skin/mini entry points. The rolling-only
                    // extension returns once both groups fit side by side.
                    visible: root.width >= 1210
                    Layout.preferredWidth: visible
                                           ? (root.width < 1360 ? 394 : 488)
                                           : 0
                    Layout.maximumWidth: Layout.preferredWidth
                    Layout.fillHeight: true
                    spacing: root.width < 1360 ? 4 : 7

                    ColumnLayout {
                        spacing: 2
                        Label {
                            text: qsTr("速度")
                            color: Theme.secondaryText
                            font.pixelSize: 10
                        }
                        RowLayout {
                            spacing: 2
                            ToolButton {
                                objectName: "rollingSpeedMinus"
                                text: "−"
                                implicitWidth: 28
                                implicitHeight: 30
                                onClicked: root.adjustSpeed(-0.05)
                            }
                            Label {
                                objectName: "rollingSpeedValue"
                                Layout.preferredWidth: 48
                                horizontalAlignment: Text.AlignHCenter
                                text: root.formatSpeed(
                                          root.playback
                                          ? root.playback.speedRatio : 1)
                                color: Theme.primaryText
                                font.pixelSize: 11
                            }
                            ToolButton {
                                objectName: "rollingSpeedPlus"
                                text: "+"
                                implicitWidth: 28
                                implicitHeight: 30
                                onClicked: root.adjustSpeed(0.05)
                            }
                        }
                    }

                    ColumnLayout {
                        spacing: 2
                        Label {
                            id: sourceBpm
                            objectName: "rollingSourceBpm"
                            text: root.sourceBpmValue > 0
                                  ? root.sourceBpmValue.toFixed(2) + " BPM"
                                  : "—"
                            color: Theme.secondaryText
                            font.pixelSize: 9
                        }
                        TextField {
                            id: targetBpm
                            objectName: "rollingTargetBpm"
                            Layout.preferredWidth: 68
                            Layout.preferredHeight: 30
                            horizontalAlignment: Text.AlignHCenter
                            text: root.formatBpm(
                                      root.playback
                                      ? root.playback.targetBpm : 0)
                            color: Theme.primaryText
                            font.pixelSize: 11
                            validator: DoubleValidator {
                                bottom: 20
                                top: 400
                                decimals: 2
                            }
                            onEditingFinished:
                                root.commitTargetBpm(text)
                        }
                    }

                    ToolButton {
                        objectName: "rollingTempoReset"
                        Layout.alignment: Qt.AlignBottom
                        implicitWidth: 30
                        implicitHeight: 30
                        icon.source: Theme.icon("restore-line")
                        icon.color: Theme.iconPrimary
                        icon.width: 16
                        icon.height: 16
                        onClicked: root.resetRollingTempo()
                    }

                    ColumnLayout {
                        spacing: 2
                        Label {
                            text: qsTr("保持音调")
                            color: Theme.secondaryText
                            font.pixelSize: 10
                        }
                        Switch {
                            id: keepPitchControl
                            objectName: "rollingKeepPitchControl"
                            Layout.preferredWidth: 42
                            Layout.preferredHeight: 30
                            checked: root.playback
                                     ? Boolean(root.playback.keepPitch) : true
                            onClicked: {
                                if (root.playback
                                        && root.playback.setKeepPitch
                                           !== undefined)
                                    root.playback.setKeepPitch(checked)
                            }
                        }
                    }

                    ColumnLayout {
                        spacing: 2
                        Label {
                            text: qsTr("波形缩放")
                            color: Theme.secondaryText
                            font.pixelSize: 10
                        }
                        RowLayout {
                            spacing: 1
                            ToolButton {
                                objectName: "rollingZoomMinus"
                                text: "−"
                                implicitWidth: 28
                                implicitHeight: 30
                                onClicked: root.zoomOut()
                            }
                            ToolButton {
                                objectName: "rollingZoomPlus"
                                text: "+"
                                implicitWidth: 28
                                implicitHeight: 30
                                onClicked: root.zoomIn()
                            }
                            ToolButton {
                                objectName: "rollingZoomReset"
                                implicitWidth: 28
                                implicitHeight: 30
                                icon.source: Theme.icon("restore-line")
                                icon.color: Theme.iconPrimary
                                icon.width: 14
                                icon.height: 14
                                onClicked: root.resetZoom()
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: rollingLibraryWorkspace
            objectName: "rollingLibraryWorkspace"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 250
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 8
            color: Theme.listWorkspaceSurface
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    SideNavigation {
                        id: rollingNavigation
                        objectName: "rollingLibraryNavigation"
                        Layout.preferredWidth: 188
                        Layout.minimumWidth: 188
                        Layout.maximumWidth: 188
                        Layout.fillHeight: true
                        navigationModel: root.navigationModel
                        playlistModel: root.playlistModel
                        selectedCategory: root.filterModel
                                          ? root.filterModel.category : "all"
                        selectedTagKey: root.filterModel
                                        ? root.filterModel.tagKey : ""
                        selectedResourceFolder: root.filterModel
                                                ? root.filterModel.resourceFolder
                                                : ""
                        showTagManagementEntry: false
                        onCategorySelected: function(category) {
                            if (!root.filterModel)
                                return
                            root.filterModel.category = category
                            root.filterModel.tagKey = ""
                            root.filterModel.resourceFolder = ""
                        }
                        onNavigationSelected: function(nodeType, nodeId,
                                                       resourceFolder) {
                            if (!root.filterModel)
                                return
                            root.filterModel.resourceFolder = resourceFolder || ""
                            if (nodeType !== "tags")
                                root.filterModel.tagKey = ""
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        color: Theme.listDivider
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 6
                        spacing: 4

                        TrackList {
                            objectName: "rollingTrackList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            trackModel: root.filterModel || root.libraryModel
                            playlistModel: root.playlistModel
                            selectedCategory: root.filterModel
                                              ? root.filterModel.category : "all"
                            tagFilterActive: root.filterModel
                                             ? root.filterModel.tagKey.length > 0
                                             : false
                            activeTagKey: root.filterModel
                                          ? root.filterModel.tagKey : ""
                            integratedCompact: true
                        }

                        SearchFilter {
                            id: rollingSearchFilter
                            objectName: "rollingSearchFilter"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 42
                            integratedStyle: true
                            searchText: root.filterModel
                                        ? root.filterModel.searchText : ""
                            exactRating: root.filterModel
                                         ? root.filterModel.exactRating : 0
                            minBpm: root.filterModel
                                    ? root.filterModel.minBpm : 60
                            maxBpm: root.filterModel
                                    ? root.filterModel.maxBpm : 160
                            onSearchTextChanged: if (root.filterModel)
                                root.filterModel.searchText = searchText
                            onExactRatingChanged: if (root.filterModel)
                                root.filterModel.exactRating = exactRating
                            onMinBpmChanged: if (root.filterModel)
                                root.filterModel.minBpm = minBpm
                            onMaxBpmChanged: if (root.filterModel)
                                root.filterModel.maxBpm = maxBpm
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        color: Theme.listDivider
                    }

                    TagManagementPanel {
                        id: rollingTagPanel
                        objectName: "rollingTagManagementPanel"
                        Layout.preferredWidth: 232
                        Layout.minimumWidth: 232
                        Layout.maximumWidth: 232
                        Layout.fillHeight: true
                        tagModel: root.tagModel
                        filterModel: root.filterModel
                        compact: true
                    }
                }

                LyricsPanel {
                    objectName: "rollingLyricsPanel"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 104 : 0
                    visible: PlayerExperienceController.lyricsVisible
                             && PlayerExperienceController.immersiveMode
                                === PlayerExperienceController.Off
                    service: LyricsService
                    spatialMode: false
                }
            }
        }
    }
}
