import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: "rollingPlayerShell"
    implicitWidth: defaultWindowWidth
    implicitHeight: defaultWindowHeight

    component DeckToolButton: ToolButton {
        flat: true
        background: Rectangle {
            color: "transparent"
            border.width: parent.activeFocus
                          && (parent.focusReason === Qt.TabFocusReason
                              || parent.focusReason === Qt.BacktabFocusReason)
                          ? 1 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }

    property var hostWindow: null
    property alias libraryNavigation: rollingNavigation
    property var playback: PlaybackController
    RollingKeyboardHandler {
        objectName: "rollingKeyboardHandler"
        shortcuts: SettingsController.rollingKeyboardShortcuts
        enabled: root.visible
        onActionPressed: function(action) {
            if (!root.playback) return
            if (action === "cue") root.playback.cuePress()
            else if (action === "cueJump") root.playback.jumpToCue()
            else if (action === "cueDelete") root.playback.clearCue()
            else if (action === "gridOrigin") root.playback.setBeatGridFirstBeat()
            else if (action === "gridLeft") root.playback.nudgeBeatGrid(-1)
            else if (action === "gridRight") root.playback.nudgeBeatGrid(1)
            else if (action.indexOf("hotCueDelete") === 0)
                root.playback.clearHotCue(Number(action.slice(12)) - 1)
            else if (action.indexOf("hotCue") === 0)
                root.playback.activateHotCue(Number(action.slice(6)) - 1)
        }
        onActionReleased: function(action) {
            if (action === "cue" && root.playback) root.playback.cueRelease()
        }
        onCancelled: { if (root.playback) root.playback.cancelCue() }
    }
    property var waveformSession: null
    property var libraryModel: LibraryModel
    property var filterModel: null
    property var playlistModel: PlaylistModel
    property var navigationModel: LibraryNavigationModel
    property var tagModel: TagModel
    property var lyricsService: LyricsService
    property var visualFeatures: AudioVisualFeatureController
    property var currentTrack: null
    // Rolling viewport state. Waveform samples/layers remain owned and
    // rendered by WaveformItem. A bounded two-viewport render window keeps
    // the same pixels-per-millisecond density while ordinary playback only
    // translates already-built scene-graph geometry.
    property var viewportBeatOptions: [2, 4, 8, 16, 32, 64]
    // Viewport zoom is deliberately local to a shell instance: re-entering
    // rolling mode starts wide, while track changes keep the user's choice.
    property real visibleBeats: 32.0
    property bool scratchGestureActive: false
    property real scratchVisualPositionMs: 0
    property real scratchAnchorPositionMs: 0
    property int libraryTrackRevision: 0
    property real waveformRenderStartMs: -1
    property real waveformRenderEndMs: -1
    property alias tagSearchText: rollingSidePanel.tagSearchText
    property int sidePanelPage: 0
    property bool sidePanelExpanded: true
    readonly property int defaultWindowWidth: 1386
    readonly property int defaultTrackListHeight:
        Theme.tableHeaderHeight + 10 * Theme.mediaListRowHeight
    readonly property int defaultWindowHeight:
        Theme.titleBarHeight + Theme.rollingOverviewHeight + 24
        + Theme.rollingWaveformHeight + 64 + 2
        + 4 * 4
        + 2 * 6 + defaultTrackListHeight + 4 + 32 + 8

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
    readonly property real beatGridBpmValue:
        playback && Number(playback.beatGridBpm) > 0
        ? Number(playback.beatGridBpm)
        : sourceBpmValue > 0 ? sourceBpmValue : 120.0
    readonly property bool beatGridFallback:
        playback && Boolean(playback.beatGridEstimatedBpm)
    readonly property real beatGridFirstBeatMs:
        playback ? Number(playback.beatGridOffsetMs) || 0 : 0
    readonly property real cuePositionValue:
        playback ? Number(playback.cuePositionMs) : -1
    readonly property real effectiveBpm: beatGridBpmValue
    readonly property real beatSec: 60.0 / effectiveBpm
    readonly property real viewTimeSpanSec: visibleBeats * beatSec
    readonly property real canvasWidth:
        Math.max(1, mainWaveformCanvas.width)
    readonly property real pxPerSec:
        canvasWidth / Math.max(0.001, viewTimeSpanSec)
    readonly property real viewportCenterMs:
        scratchGestureActive ? scratchVisualPositionMs : playbackPositionMs
    readonly property real viewportSpanMs:
        effectiveDurationMs > 0 ? viewTimeSpanSec * 1000.0 : 0
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
    readonly property real viewStartTimeSec: viewportStartMs / 1000.0
    readonly property real viewEndTimeSec: viewportEndMs / 1000.0
    readonly property real waveformRenderSpanMs:
        Math.max(0, waveformRenderEndMs - waveformRenderStartMs)
    readonly property real waveformContentWidthFraction:
        viewportSpanMs > 0
        ? waveformRenderSpanMs / viewportSpanMs
        : 1
    // This is the same mapping used by WaveformItem::pixelForTime: the item's
    // width is renderSpan / viewportSpan canvases, so renderSpan cancels out.
    // Keeping it declarative avoids a transient old-width/new-range frame.
    readonly property real waveformContentX: {
        if (waveformRenderSpanMs <= 0 || viewportSpanMs <= 0)
            return 1
        var sourceTime = Math.round(clamp(viewportCenterMs, 0,
                                          effectiveDurationMs))
        return mainWaveformCanvas.width / 2
                - (sourceTime - waveformRenderStartMs) / viewportSpanMs
                  * Math.max(0, mainWaveformCanvas.width - 2)
    }
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

    function formatPreciseTime(milliseconds) {
        var value = Number(milliseconds)
        var bounded = isFinite(value) ? Math.max(0, value) : 0
        var centiseconds = Math.floor(bounded / 10) % 100
        return formatTime(bounded) + ":"
                + (centiseconds < 10 ? "0" : "") + centiseconds
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
                   / pxPerSec
        return clamp(rate, -3, 3)
    }

    function deltaMsForPixels(deltaX) {
        return -Number(deltaX || 0) / pxPerSec * 1000
    }

    function timeToX(timeSec) {
        return (Number(timeSec) - viewStartTimeSec) * pxPerSec
    }

    function waveformRenderWindowNeedsRebase() {
        var duration = Math.max(0, Math.round(effectiveDurationMs))
        if (duration <= 0 || viewportSpanMs <= 0)
            return waveformRenderStartMs !== 0 || waveformRenderEndMs !== 0
        var targetSpan = Math.min(duration,
                                  Math.max(1, Math.round(viewportSpanMs * 2)))
        if (waveformRenderStartMs < 0 || waveformRenderEndMs < 0
                || Math.abs(waveformRenderSpanMs - targetSpan) > 1)
            return true

        var sourceTime = Math.round(clamp(viewportCenterMs, 0, duration))
        var guardMs = viewportSpanMs / 2
        return (waveformRenderStartMs > 0
                && sourceTime < waveformRenderStartMs + guardMs)
                || (waveformRenderEndMs < duration
                    && sourceTime > waveformRenderEndMs - guardMs)
    }

    function rebaseWaveformRenderWindow() {
        if (!mainWaveform)
            return
        var duration = Math.max(0, Math.round(effectiveDurationMs))
        if (duration <= 0 || viewportSpanMs <= 0) {
            waveformRenderStartMs = 0
            waveformRenderEndMs = 0
            mainWaveform.setVisibleRange(0, 0)
            return
        }
        var targetSpan = Math.min(duration,
                                  Math.max(1, Math.round(viewportSpanMs * 2)))
        var sourceTime = Math.round(clamp(viewportCenterMs, 0, duration))
        var startMs = Math.round(clamp(sourceTime - targetSpan / 2,
                                       0, duration - targetSpan))
        var endMs = startMs + targetSpan
        waveformRenderStartMs = startMs
        waveformRenderEndMs = endMs
        mainWaveform.setVisibleRange(startMs, endMs)
    }

    function syncWaveformViewport(forceRebase) {
        if (!mainWaveform)
            return
        // `playback` is injected as a var in tests and in the shell loader.
        // Write the derived centre explicitly so a late controller swap can
        // never leave WaveformItem on its construction-time position.
        mainWaveform.position = Math.round(viewportCenterMs)
        if (forceRebase || waveformRenderWindowNeedsRebase())
            rebaseWaveformRenderWindow()
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

    function viewportIndex() {
        var bestIndex = 0
        var bestDistance = Number.POSITIVE_INFINITY
        for (var index = 0; index < viewportBeatOptions.length; ++index) {
            if (viewportBeatOptions[index] === visibleBeats)
                return index
            var distance = Math.abs(viewportBeatOptions[index] - visibleBeats)
            if (distance < bestDistance) {
                bestDistance = distance
                bestIndex = index
            }
        }
        return bestIndex
    }

    function setViewportIndex(index) {
        var bounded = Math.max(0, Math.min(viewportBeatOptions.length - 1,
                                          Math.round(index)))
        visibleBeats = viewportBeatOptions[bounded]
    }

    function zoomIn() {
        setViewportIndex(viewportIndex() - 1)
    }

    function zoomOut() {
        setViewportIndex(viewportIndex() + 1)
    }

    function resetZoom() {
        visibleBeats = 32.0
    }

    function commitGridBpm(text) {
        if (!playback || playback.setBeatGridBpm === undefined)
            return
        var value = Number(text)
        if (!isFinite(value) || value < 20 || value > 400)
            return
        playback.setBeatGridBpm(value)
    }

    function beatGridStatusText() {
        if (beatGridFallback)
            return qsTr("估算 · 回退 120 BPM")
        var value = beatGridBpmValue.toFixed(3) + " BPM"
        if (playback && Boolean(playback.beatGridEstimatedBpm))
            return qsTr("估算 ") + value
        return playback && Boolean(playback.beatGridCalibrated)
                ? qsTr("已校准 ") + value : value
    }

    function cancelCueHold() {
        if (playerControls.cancelCueHold !== undefined)
            playerControls.cancelCueHold()
    }

    function finishScratchGesture(cancelled) {
        scratchIdleTimer.stop()
        if (!scratchSurface.scratchStarted)
            return
        var releasePositionMs = Math.round(clamp(scratchVisualPositionMs,
                                                 0, effectiveDurationMs))
        if (playback) {
            if (cancelled && playback.cancelScratch !== undefined)
                playback.cancelScratch()
            else if (!cancelled && playback.endScratch !== undefined)
                playback.endScratch()
            // Scratch audio follows rate samples; the visible deck follows
            // pointer distance. Commit its center exactly when the drag ends.
            if (!cancelled) {
                if (playback.seek !== undefined)
                    playback.seek(releasePositionMs)
                if (playback.play !== undefined)
                    playback.play()
            }
        }
        scratchSurface.scratchStarted = false
        scratchGestureActive = false
        syncWaveformViewport(false)
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

    onVisibleBeatsChanged: {
        var normalizedBeats = viewportBeatOptions[viewportIndex()]
        if (visibleBeats !== normalizedBeats) {
            visibleBeats = normalizedBeats
            return
        }
        syncWaveformViewport(true)
    }
    onViewportCenterMsChanged: syncWaveformViewport(false)
    onViewportSpanMsChanged: syncWaveformViewport(true)
    onEffectiveDurationMsChanged: syncWaveformViewport(true)
    onWaveformSessionChanged: {
        refreshCurrentTrack()
        syncWaveformViewport(true)
    }
    onLibraryModelChanged: refreshCurrentTrack()
    onPlaybackChanged: {
        refreshCurrentTrack()
        syncWaveformViewport(true)
    }

    Component.onCompleted: {
        refreshCurrentTrack()
        Qt.callLater(function() { syncWaveformViewport(true) })
    }

    Connections {
        target: root.hostWindow
        ignoreUnknownSignals: true
        function onActiveChanged() {
            if (root.hostWindow && !root.hostWindow.active) {
                root.cancelScratchGesture()
                root.cancelCueHold()
            }
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
        spacing: 4

        TitleBar {
            objectName: "rollingTitleBar"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.titleBarHeight
            window: root.hostWindow
            showBrand: true
            onOpenSettings: root.openSettingsRequested()
        }

        Item {
            id: overviewRegion
            objectName: "rollingOverviewRegion"
            Layout.fillWidth: true
            Layout.preferredHeight: coverFrame.height + 16
            Layout.leftMargin: 10
            Layout.rightMargin: 10

            Rectangle {
                id: coverFrame
                objectName: "rollingTrackCover"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.topMargin: 8
                height: root.height < 700 ? 100 : 116
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
                objectName: "rollingHeaderInfo"
                readonly property int lineHeight: root.height < 700 ? 20 : 24
                readonly property int lineSpacing: root.height < 700 ? 4 : 3
                anchors.left: coverFrame.right
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.verticalCenter: coverFrame.verticalCenter
                height: titleRow.height + subtitleRow.height + badgeRow.height
                        + overviewWaveformHost.height + 2 * lineSpacing + Theme.spacingXs

                Row {
                    id: titleRow
                    objectName: "rollingTitleRow"
                    anchors.left: parent.left
                    anchors.right: meters.left
                    anchors.rightMargin: 12
                    anchors.top: parent.top
                    height: headerInfo.lineHeight
                    spacing: 8
                    clip: true

                    Text {
                        objectName: "rollingTrackTitle"
                        width: Math.min(implicitWidth, Math.max(0, parent.width - 36))
                        height: parent.height
                        text: root.currentTrack && root.currentTrack.title
                              ? root.currentTrack.title : qsTr("未选择歌曲")
                        color: Theme.primaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeSection
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    DeckToolButton {
                        objectName: "rollingFavoriteButton"
                        width: 28
                        height: parent.height
                        padding: 0
                        icon.source: root.currentTrack && root.currentTrack.favorite
                                     ? Theme.icon("heart-fill") : Theme.icon("heart-line")
                        icon.color: root.currentTrack && root.currentTrack.favorite
                                    ? Theme.favoriteRed : Theme.secondaryText
                        icon.width: 20
                        icon.height: 20
                        Accessible.name: qsTr("收藏歌曲")
                        onClicked: root.toggleFavorite()
                    }
                }

                Row {
                    id: subtitleRow
                    objectName: "rollingSubtitleRow"
                    anchors.left: parent.left
                    anchors.top: titleRow.bottom
                    anchors.topMargin: headerInfo.lineSpacing
                    anchors.right: meters.left
                    anchors.rightMargin: 12
                    height: headerInfo.lineHeight
                    spacing: 6

                    TrackSubtitle {
                        objectName: "rollingTrackSubtitle"
                        verticalAlignment: Text.AlignVCenter
                        width: Math.min(implicitWidth,
                                        Math.max(0, parent.width - headerRating.width - 6))
                        height: parent.height
                        artist: root.currentTrack && root.currentTrack.artist
                                ? root.currentTrack.artist : ""
                        album: root.currentTrack && root.currentTrack.album
                               ? root.currentTrack.album : ""
                        tags: root.currentTrack && root.currentTrack.tags
                              ? root.currentTrack.tags : []
                        font.pixelSize: Theme.fontSizeCaption
                    }

                    Row {
                        id: headerRating
                        objectName: "rollingTrackRating"
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        Repeater {
                            model: 5
                            ThemedIcon {
                                width: 16
                                height: 16
                                source: index < Number(root.currentTrack && root.currentTrack.rating || 0)
                                        ? Theme.icon("star-fill") : Theme.icon("star-line")
                                tint: index < Number(root.currentTrack && root.currentTrack.rating || 0)
                                      ? Theme.ratingColor(index) : Theme.iconSecondary
                            }
                        }
                    }
                }

                Column {
                    id: meters
                    objectName: "rollingStereoMeter"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: Math.min(118, Math.max(90, parent.width * 0.13))
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
                        height: 7
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
                        height: 7
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
                anchors.left: coverFrame.right
                anchors.leftMargin: 16
                anchors.top: headerInfo.top
                anchors.topMargin: titleRow.height + subtitleRow.height
                                   + 2 * headerInfo.lineSpacing
                height: headerInfo.lineHeight
                spacing: 5
                Repeater {
                    model: root.metadataBadges
                    Rectangle {
                        width: badgeText.implicitWidth + 12
                        height: badgeRow.height
                        color: Theme.panel
                        border.color: Theme.border
                        border.width: 1
                        radius: 4
                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.secondaryText
                            font.pixelSize: Theme.fontSizeCaption
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
                anchors.top: badgeRow.bottom
                anchors.topMargin: Theme.spacingXs
                height: root.height < 700 ? 24 : 30

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
                    lowColor: root.frequencyWaveformSettings.lowColor
                    midColor: root.frequencyWaveformSettings.midColor
                    highColor: root.frequencyWaveformSettings.highColor
                    frequencyUnplayedOpacity: 1.0
                    amplitudeScale: SettingsController.waveformHeight
                    density: SettingsController.waveformDensity
                    lineWidth: SettingsController.waveformThickness
                }

                Item {
                    id: overviewCueMarker
                    objectName: "rollingOverviewCueMarker"
                    visible: root.cuePositionValue >= 0
                             && root.cuePositionValue <= root.effectiveDurationMs
                    x: root.effectiveDurationMs > 0
                       ? root.clamp(
                             overviewWaveform.pixelForTime(
                                 root.cuePositionValue) - width / 2,
                             0, Math.max(0, overviewWaveform.width - width))
                       : 0
                    anchors.top: overviewWaveform.top
                    width: 28
                    height: overviewWaveform.height
                    enabled: false
                    z: 7

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        text: "▼"
                        color: Theme.warning
                        font.pixelSize: 9 // typography-size-allow: overview waveform position annotation
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        text: "CUE"
                        color: Theme.warning
                        font.family: Theme.fontPrimary
                        font.pixelSize: 8 // typography-size-allow: overview waveform position annotation
                        font.weight: Font.Bold
                    }
                }

                Rectangle {
                    id: overviewPlayhead
                    objectName: "rollingOverviewPlayhead"
                    x: root.effectiveDurationMs > 0
                       ? root.clamp(
                             overviewWaveform.pixelForTime(
                                 root.playbackPositionMs) - width / 2,
                             0, Math.max(0, overviewWaveform.width - width))
                       : 0
                    anchors.top: overviewWaveform.top
                    anchors.bottom: overviewWaveform.bottom
                    width: 1
                    color: Theme.onBrandGradientText
                    enabled: false
                    z: 6
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
                               ? overviewWaveform.pixelForTime(
                                     root.playbackPositionMs)
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
                    cursorShape: Qt.ArrowCursor
                    function updateHover(x) {
                        overviewWaveformHost.hoverTimeMs =
                                root.effectiveDurationMs > 0
                                ? overviewWaveform.timeForX(x) : -1
                    }
                    onPositionChanged: function(mouse) { updateHover(mouse.x) }
                    onEntered: updateHover(mouseX)
                    onExited: overviewWaveformHost.hoverTimeMs = -1
                    onClicked: function(mouse) {
                        if (!root.playback || root.effectiveDurationMs <= 0)
                            return
                        var target = overviewWaveform.timeForX(mouse.x)
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
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
            }
        }

        Rectangle {
            id: mainWaveformCanvas
            objectName: "rollingMainWaveformCanvas"
            Layout.fillWidth: true
            Layout.preferredHeight: root.height < 800
                                    ? Theme.rollingWaveformHeightCompact
                                    : Theme.rollingWaveformHeight
            Layout.minimumHeight: Theme.rollingWaveformHeightCompact
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            color: Theme.isLight ? Theme.panel : Qt.darker(Theme.panel, 1.12)
            border.color: Qt.rgba(Theme.border.r, Theme.border.g,
                                  Theme.border.b, 0.3)
            border.width: 1
            radius: Theme.radiusSm
            clip: true

            WaveformItem {
                id: mainWaveform
                objectName: "rollingMainWaveform"
                x: root.waveformContentX
                y: 4
                width: Math.max(0, (parent.width - 2)
                                * root.waveformContentWidthFraction)
                height: Math.max(0, parent.height - 8)
                layers: root.waveformSession
                        ? root.waveformSession.layers : ({})
                duration: root.effectiveDurationMs
                position: root.viewportCenterMs
                cursorPosition: -1
                pointerInteractionEnabled: false
                visualMode: 3
                baseColor: SettingsController.waveformRgbBaseColor
                lowColor: root.frequencyWaveformSettings.lowColor
                midColor: root.frequencyWaveformSettings.midColor
                highColor: root.frequencyWaveformSettings.highColor
                frequencyUnplayedOpacity: 1.0
                amplitudeScale: SettingsController.waveformHeight
                density: SettingsController.waveformDensity
                // The analyser already provides a bounded, high-detail source.
                // Fill the physical-pixel budget from the visible time slice so
                // a short 8-beat viewport never turns into enlarged, widely
                // spaced source bars. Max-preserving downsampling retains
                // transients when the user zooms out.
                preserveSourcePeakDensity: false
                sourceAnchoredSampling: true
                lineWidth: SettingsController.waveformThickness
            }

            BeatGridOverlay {
                id: beatGrid
                objectName: "rollingBeatGrid"
                anchors.fill: parent
                visible: SettingsController.rollingBeatGridEnabled
                viewStartMs: root.viewportStartMs
                viewEndMs: root.viewportEndMs
                firstBeatMs: root.beatGridFirstBeatMs
                bpm: root.beatGridBpmValue
                grouping: SettingsController.rollingBeatGridGrouping
                lineColor: Qt.rgba(Theme.primaryText.r, Theme.primaryText.g,
                                   Theme.primaryText.b,
                                   root.rollingLightTheme ? 0.34 : 0.25)
                fourBeatColor: Theme.success
                eightBeatColor: Theme.danger
                enabled: false
                z: 2
            }

            Repeater {
                model: root.playback && root.playback.hotCuePositions !== undefined
                       ? root.playback.hotCuePositions : []
                delegate: Text {
                    required property int index
                    required property var modelData
                    objectName: "rollingHotCueMarker" + (index + 1)
                    visible: Number(modelData) >= 0
                             && Number(modelData) >= root.viewportStartMs
                             && Number(modelData) <= root.viewportEndMs
                    x: root.timeToX(Number(modelData) / 1000) - width / 2
                    y: parent.height - height - 2
                    text: "▲" + (index + 1)
                    color: Theme.warning
                    font.pixelSize: Theme.fontSizeCaption
                    font.weight: Font.Bold
                    enabled: false
                    z: 6
                }
            }

            Item {
                id: mainCueMarker
                objectName: "rollingMainCueMarker"
                visible: root.cuePositionValue >= 0
                         && root.cuePositionValue >= root.viewportStartMs
                         && root.cuePositionValue <= root.viewportEndMs
                x: root.timeToX(root.cuePositionValue / 1000) - width / 2
                anchors.top: parent.top
                anchors.topMargin: 2
                width: 32
                height: 28
                enabled: false
                z: 6

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    text: "▼"
                    color: Theme.warning
                    font.pixelSize: Theme.fontSizeTagCapsule
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    text: "CUE"
                    color: Theme.warning
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    font.weight: Font.Bold
                }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.accent
                opacity: root.scratchGestureActive ? 0.07 : 0
                visible: opacity > 0
                z: 4
                Behavior on opacity {
                    NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                }
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 10
                visible: root.scratchGestureActive
                text: {
                    var delta = (root.scratchVisualPositionMs
                                 - root.scratchAnchorPositionMs) / 1000
                    return (delta >= 0 ? "+" : "") + delta.toFixed(2) + " s"
                }
                color: Theme.primaryText
                font.pixelSize: Theme.fontSizeCaption
                padding: 6
                background: Rectangle {
                    color: Theme.surfaceElevated
                    border.color: Theme.accent
                    border.width: 1
                    radius: Theme.radiusSm
                }
                z: 7
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
                width: 2
                color: Theme.accent
                opacity: 0.95
                z: 5

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: -2
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.accent
                }
            }

            Label {
                id: currentTimeCapsule
                objectName: "rollingCurrentTimeCapsule"
                anchors.right: centerPlayhead.left
                anchors.rightMargin: 6
                anchors.top: parent.top
                anchors.topMargin: 8
                text: root.formatPreciseTime(root.viewportCenterMs)
                color: Theme.primaryText
                font.pixelSize: Theme.fontSizeCaption
                padding: 0
                background: null
                z: 7
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
                font.pixelSize: Theme.fontSizeCaption
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
                    root.syncWaveformViewport(false)
                }

                onReleased: root.finishScratchGesture(false)
                onCanceled: root.cancelScratchGesture()
            }
        }

        Rectangle {
            id: bottomBar
            objectName: "rollingBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(
                                        64,
                                        rollingControls.childrenRect.height + 8)
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 2
            color: Theme.panel
            border.color: Qt.rgba(Theme.border.r, Theme.border.g,
                                  Theme.border.b, 0.3)
            border.width: 1
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                PlayerControls {
                    id: playerControls
                    objectName: "playerControls"
                    Layout.preferredWidth: 430
                    Layout.minimumWidth: 430
                    Layout.fillHeight: true
                    playback: root.playback
                    shellMode: 2
                    centerTransport: false
                    showWaveformMode: true
                    showCueButton: true
                    secondaryActionHost: rollingShellActions
                    onOpenEqualizerRequested:
                        root.openEqualizerRequested()
                }

                Flow {
                    id: rollingControls
                    objectName: "rollingTempoControls"
                    visible: true
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: childrenRect.height
                    Layout.alignment: Qt.AlignVCenter
                    spacing: root.width < 1180 ? Theme.spacingXs : Theme.spacingSm

                    readonly property real groupHeight:
                        sourceBpm.implicitHeight + Theme.controlHeightCompact + 2

                    readonly property real singleRowContentWidth:
                        gridToggleGroup.width + gridGroupingGroup.width
                        + viewportGroup.width + speedGroup.width
                        + targetBpmGroup.width + tempoResetButton.width
                        + keepPitchGroup.width + calibrationGroup.width
                        + rollingShellActions.width + spacing * 8

                    Item {
                        width: Math.max(0, rollingControls.width
                                        - rollingControls.singleRowContentWidth
                                        - rollingControls.spacing)
                        height: 1
                        visible: width > 0
                    }

                    ColumnLayout {
                        id: gridToggleGroup
                        width: implicitWidth
                        height: rollingControls.groupHeight
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("网格")
                            color: Theme.secondaryText
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        ThemedSwitch {
                            id: beatGridSwitch
                            objectName: "rollingBeatGridSwitch"
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: Theme.controlHeightCompact
                            checked: SettingsController.rollingBeatGridEnabled
                            onClicked:
                                SettingsController.rollingBeatGridEnabled = checked
                        }
                    }

                    ColumnLayout {
                        id: calibrationGroup
                        width: implicitWidth
                        height: rollingControls.groupHeight
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("网格校准")
                            color: Theme.secondaryText
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        DeckToolButton {
                            id: calibrationButton
                            objectName: "rollingGridCalibrationButton"
                            implicitWidth: 68
                            implicitHeight: Theme.controlHeightCompact
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                            text: root.playback
                                  && Boolean(root.playback.beatGridCalibrated)
                                  ? qsTr("校准…")
                                  : root.beatGridFallback
                                    ? qsTr("估算 120") : qsTr("估算…")
                            onClicked: calibrationPopup.open()
                        }
                    }

                    ColumnLayout {
                        id: gridGroupingGroup
                        width: implicitWidth
                        height: rollingControls.groupHeight
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("分组")
                            color: Theme.secondaryText
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        ThemedComboBox {
                            id: beatGridGrouping
                            objectName: "rollingBeatGridGrouping"
                            Layout.preferredWidth: 62
                            Layout.preferredHeight: 24
                            Layout.maximumHeight: 24
                            Layout.topMargin: 2
                            Layout.bottomMargin: 2
                            model: ["4", "8"]
                            currentIndex:
                                SettingsController.rollingBeatGridGrouping === 8
                                ? 1 : 0
                            onActivated:
                                SettingsController.rollingBeatGridGrouping =
                                    currentIndex === 1 ? 8 : 4
                        }
                    }

                    ColumnLayout {
                        id: viewportGroup
                        width: implicitWidth
                        height: rollingControls.groupHeight
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("视窗")
                            color: Theme.secondaryText
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        RowLayout {
                            spacing: 1
                            DeckToolButton {
                                objectName: "rollingZoomMinus"
                                text: "−"
                                implicitWidth: 28
                                implicitHeight: Theme.controlHeightCompact
                                enabled: root.viewportIndex()
                                         < root.viewportBeatOptions.length - 1
                                onClicked: root.zoomOut()
                            }
                            ThemedComboBox {
                                id: viewportBeats
                                objectName: "rollingViewportBeats"
                                Layout.preferredWidth: 62
                                Layout.preferredHeight: 24
                                Layout.maximumHeight: 24
                                Layout.alignment: Qt.AlignVCenter
                                leftPadding: 0
                                rightPadding: 20
                                textLeftPadding: 6
                                textRightPadding: 0
                                model: ["2", "4", "8", "16", "32", "64"]
                                currentIndex: root.viewportIndex()
                                // Do not index ComboBox.model here: the native
                                // model wrapper can expose currentText to
                                // accessibility while yielding no paint text.
                                displayText: String(root.visibleBeats)
                                onActivated: root.setViewportIndex(currentIndex)
                            }
                            DeckToolButton {
                                objectName: "rollingZoomPlus"
                                text: "+"
                                implicitWidth: 28
                                implicitHeight: Theme.controlHeightCompact
                                enabled: root.viewportIndex() > 0
                                onClicked: root.zoomIn()
                            }
                            DeckToolButton {
                                objectName: "rollingZoomReset"
                                implicitWidth: 28
                                implicitHeight: Theme.controlHeightCompact
                                icon.source: Theme.icon("restore-line")
                                icon.color: Theme.iconPrimary
                                icon.width: 18
                                icon.height: 18
                                onClicked: root.resetZoom()
                            }
                        }
                    }

                    ColumnLayout {
                        id: speedGroup
                        width: implicitWidth
                        height: rollingControls.groupHeight
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("速度")
                            color: Theme.secondaryText
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        RowLayout {
                            spacing: 2
                            DeckToolButton {
                                objectName: "rollingSpeedMinus"
                                text: "−"
                                implicitWidth: 28
                                implicitHeight: Theme.controlHeightCompact
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
                                font.family: Theme.fontPrimary
                                font.pixelSize: Theme.fontSizeBody
                            }
                            DeckToolButton {
                                objectName: "rollingSpeedPlus"
                                text: "+"
                                implicitWidth: 28
                                implicitHeight: Theme.controlHeightCompact
                                onClicked: root.adjustSpeed(0.05)
                            }
                        }
                    }

                    ColumnLayout {
                        id: targetBpmGroup
                        width: implicitWidth
                        height: rollingControls.groupHeight
                        spacing: 2
                        Label {
                            id: sourceBpm
                            objectName: "rollingSourceBpm"
                            text: qsTr("BPM")
                            color: Theme.secondaryText
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        ThemedTextField {
                            id: targetBpm
                            objectName: "rollingTargetBpm"
                            leftPadding: 4
                            rightPadding: 4
                            Layout.preferredWidth: 62
                            Layout.preferredHeight: 24
                            Layout.maximumHeight: 24
                            Layout.topMargin: 2
                            Layout.bottomMargin: 2
                            Layout.alignment: Qt.AlignVCenter
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: TextInput.AlignVCenter
                            text: root.formatBpm(
                                      root.playback
                                      ? root.playback.targetBpm : 0)
                            color: Theme.primaryText
                            font.pixelSize: Theme.fontSizeBody
                            font.family: Theme.fontPrimary
                            validator: DoubleValidator {
                                bottom: 20
                                top: 400
                                decimals: 2
                            }
                            onEditingFinished:
                                root.commitTargetBpm(text)
                        }
                    }

                    ColumnLayout {
                        width: implicitWidth
                        height: rollingControls.groupHeight
                        spacing: 2
                        Item {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: sourceBpm.implicitHeight
                        }
                        DeckToolButton {
                            id: tempoResetButton
                            objectName: "rollingTempoReset"
                            implicitWidth: 28
                            implicitHeight: Theme.controlHeightCompact
                            icon.source: Theme.icon("restore-line")
                            icon.color: Theme.iconPrimary
                            icon.width: 18
                            icon.height: 18
                            onClicked: root.resetRollingTempo()
                        }
                    }

                    ColumnLayout {
                        id: keepPitchGroup
                        width: implicitWidth
                        height: rollingControls.groupHeight
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("保持音调")
                            color: Theme.secondaryText
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        ThemedSwitch {
                            id: keepPitchControl
                            objectName: "rollingKeepPitchControl"
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: Theme.controlHeightCompact
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

                    Item {
                        id: rollingShellActions
                        objectName: "rollingShellActions"
                        width: 104
                        height: rollingControls.groupHeight
                    }
                }
            }

            Popup {
                id: calibrationPopup
                objectName: "rollingGridCalibrationPopup"
                parent: Overlay.overlay
                function setBoundedPosition(nextX, nextY) {
                    x = Math.max(8, Math.min(parent.width - width - 8, nextX))
                    y = Math.max(8, Math.min(parent.height - height - 8, nextY))
                }
                function positionAboveButton() {
                    var point = calibrationButton.mapToItem(parent, 0, 0)
                    setBoundedPosition(point.x + calibrationButton.width - width,
                                       point.y - height - 4)
                }
                onAboutToShow: positionAboveButton()
                onOpened: positionAboveButton()
                Connections {
                    target: calibrationPopup.parent
                    function onWidthChanged() {
                        if (calibrationPopup.visible)
                            calibrationPopup.setBoundedPosition(calibrationPopup.x, calibrationPopup.y)
                    }
                    function onHeightChanged() {
                        if (calibrationPopup.visible)
                            calibrationPopup.setBoundedPosition(calibrationPopup.x, calibrationPopup.y)
                    }
                }
                width: 228
                padding: 10
                modal: false
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle {
                    color: Theme.surfaceElevated
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm
                }
                contentItem: ColumnLayout {
                    spacing: 6
                    Label {
                        objectName: "rollingGridStatusLabel"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 20
                        text: root.beatGridStatusText()
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: Theme.fontSizeCaption
                        MouseArea {
                            objectName: "rollingGridCalibrationDragHandle"
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            property point pressPoint
                            property real initialX: 0
                            property real initialY: 0
                            onPressed: function(mouse) {
                                pressPoint = mapToItem(calibrationPopup.parent, mouse.x, mouse.y)
                                initialX = calibrationPopup.x
                                initialY = calibrationPopup.y
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed) return
                                var point = mapToItem(calibrationPopup.parent, mouse.x, mouse.y)
                                calibrationPopup.setBoundedPosition(initialX + point.x - pressPoint.x,
                                                                     initialY + point.y - pressPoint.y)
                            }
                        }
                    }
                    ThemedButton {
                        objectName: "rollingGridSetFirstBeat"
                        Layout.fillWidth: true
                        text: qsTr("将播放位置设为第一拍")
                        onClicked: {
                            if (root.playback
                                    && root.playback.setBeatGridFirstBeat
                                       !== undefined)
                                root.playback.setBeatGridFirstBeat()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        ThemedButton {
                            objectName: "rollingGridNudgeLeft"
                            Layout.fillWidth: true
                            text: qsTr("−1 ms")
                            onClicked: {
                                if (root.playback
                                        && root.playback.nudgeBeatGrid
                                           !== undefined)
                                    root.playback.nudgeBeatGrid(-1)
                            }
                        }
                        ThemedButton {
                            objectName: "rollingGridNudgeRight"
                            Layout.fillWidth: true
                            text: qsTr("+1 ms")
                            onClicked: {
                                if (root.playback
                                        && root.playback.nudgeBeatGrid
                                           !== undefined)
                                    root.playback.nudgeBeatGrid(1)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        ThemedTextField {
                            id: gridBpmField
                            objectName: "rollingGridBpmField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.controlHeightCompact
                            text: root.beatGridBpmValue.toFixed(3)
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeBody
                            validator: DoubleValidator {
                                bottom: 20
                                top: 400
                                decimals: 3
                            }
                            onEditingFinished: root.commitGridBpm(text)
                        }
                        Label {
                            text: "BPM"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                    }
                    ThemedButton {
                        objectName: "rollingGridReset"
                        Layout.fillWidth: true
                        text: qsTr("重置为估算值")
                        onClicked: {
                            if (root.playback
                                    && root.playback.resetBeatGrid !== undefined)
                                root.playback.resetBeatGrid()
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
            Layout.leftMargin: Theme.spacingSm
            Layout.rightMargin: Theme.spacingSm
            Layout.bottomMargin: 8
            color: Theme.listWorkspaceSurface
            border.color: Qt.rgba(Theme.border.r, Theme.border.g,
                                  Theme.border.b, 0.3)
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
                        Layout.preferredWidth: Theme.navigationWidth
                        Layout.minimumWidth: Theme.navigationWidth
                        Layout.maximumWidth: Theme.navigationWidth
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
                            if (nodeType === "resourceRoot" || nodeType === "resourceFolder")
                                root.filterModel.category = "all"
                            root.filterModel.resourceFolder = resourceFolder || ""
                            if (nodeType !== "tags") {
                                root.filterModel.tagKey = ""
                                TagModel.selectedKey = ""
                            }
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
                        Layout.leftMargin: Theme.spacingSm
                        Layout.rightMargin: Theme.spacingSm
                        Layout.topMargin: 6
                        Layout.bottomMargin: 6
                        spacing: 4

                        TrackList {
                            objectName: "rollingTrackList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: root.defaultTrackListHeight
                            trackModel: root.filterModel || root.libraryModel
                            playlistModel: root.playlistModel
                            selectedCategory: root.filterModel
                                              ? root.filterModel.category : "all"
                            tagFilterActive: root.filterModel
                                             ? root.filterModel.tagKey.length > 0
                                             : false
                            activeTagKey: root.filterModel
                                          ? root.filterModel.tagKey : ""
                            layoutProfile: "rolling"
                            thumbnailVisibilityFollowsSetting: false
                        }

                        SearchFilter {
                            id: rollingSearchFilter
                            objectName: "rollingSearchFilter"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
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

                    LibrarySidePanel {
                        id: rollingSidePanel
                        objectNamePrefix: "rolling"
                        Layout.leftMargin: 1
                        expandedWidth: Theme.playerInspectorWidth
                        tagModel: root.tagModel
                        filterModel: root.filterModel
                        lyricsService: root.lyricsService
                        currentPage: root.sidePanelPage
                        expanded: root.sidePanelExpanded
                        onPageRequested: function(page) {
                            root.sidePanelPage = page
                        }
                        onExpandedRequested: function(value) {
                            root.sidePanelExpanded = value
                        }
                    }
                }
            }
        }
    }
}
