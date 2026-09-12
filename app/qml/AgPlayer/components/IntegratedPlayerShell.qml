import QtQuick
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: "integratedPlayerShell"
    implicitWidth: defaultWindowWidth
    implicitHeight: defaultWindowHeight

    // The shell never creates a second model.  Its host injects the same
    // filter state owned by Classic; singleton defaults only preserve the
    // existing production bindings when the shell is loaded directly.
    property var filterModel: null
    property var libraryModel: LibraryModel
    property var playlistModel: PlaylistModel
    property var navigationModel: LibraryNavigationModel
    property alias libraryNavigation: sideNavigation
    property var tagModel: TagModel
    property var playbackController: PlaybackController
    property var lyricsService: LyricsService
    property var waveformProvider: WaveformProvider
    property var waveformLayers: ({})
    property real waveformDurationMs: 0
    property bool waveformFrequencyReady: false
    readonly property var frequencyWaveformSettings:
        SettingsController.frequencyColorWaveform

    property Component bottomBarComponent: null
    property alias tagSearchText: sidePanelColumn.tagSearchText
    property var hostWindow: null
    property int sidePanelPage: 0
    property bool sidePanelExpanded: true

    property int topBarHeight: Theme.titleBarHeight
    property int leftColumnWidth: width < 1300
                                  ? Theme.navigationWidthCompact
                                  : Theme.navigationWidth
    property int rightColumnWidth: Theme.playerInspectorWidth
    property int waveformHeight: 120
    property int waveformNavigatorHeight: 8
    property int bottomBarHeight: Theme.playerBottomBarHeight
    property int contentSpacing: Theme.spacingSm
    readonly property int defaultWindowWidth: 1386
    readonly property int defaultTrackListHeight:
        Theme.tableHeaderHeight + 10 * Theme.mediaListRowHeight
    readonly property int defaultWindowHeight:
        topBarHeight
        + 2 * contentSpacing + defaultTrackListHeight
        + Theme.spacingXs + 32
        + waveformHeight + waveformNavigatorHeight + 2 * Theme.spacingXs
        + bottomBarHeight + contentSpacing
    property bool _waveformViewportResetPending: false
    readonly property real effectiveDurationMs: waveformDurationMs > 0
                                                ? waveformDurationMs
                                                : (playbackController
                                                   ? playbackController.durationMs : 0)
    readonly property real playbackPositionMs: playbackController
                                              ? playbackController.positionMs : 0
    readonly property var displayedWaveformLayers: {
        if (SettingsController.waveformMode === 2)
            return ({})
        return waveformLayers || ({})
    }
    readonly property var displayedSpectrumPeaks:
        SettingsController.waveformMode === 2
        ? shapeSpectrum(playbackController ? playbackController.spectrum : [])
        : []

    signal seekRequested(int positionMs)
    signal selectionLoopRequested(int startMs, int endMs)
    signal dragClipRequested(int startMs, int endMs)
    signal waveformZoomRequested(real factor, real timeMs)
    signal openSettingsRequested()

    function syncFilterControls() {
        if (!filterModel)
            return
        searchFilter.searchText = filterModel.searchText
        searchFilter.exactRating = filterModel.exactRating
        searchFilter.minBpm = filterModel.minBpm
        searchFilter.maxBpm = filterModel.maxBpm
    }

    function formatScaleTime(timeMs) {
        var totalSeconds = Math.max(0, Math.floor(timeMs / 1000))
        var minutes = Math.floor(totalSeconds / 60)
        var seconds = totalSeconds % 60
        return (minutes < 10 ? "0" : "") + minutes + ":"
                + (seconds < 10 ? "0" : "") + seconds
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
            var target = Math.min(1, Math.max(
                                      0, Math.sqrt(Number(source[sourceIndex])
                                                   || 0) * 1.35))
            result[index] = target
            result[half * 2 - 1 - index] = target
        }
        return result
    }

    function applyWaveformMode() {
        if (!waveform || !playedWaveform)
            return
        if (SettingsController.waveformMode === 2) {
            var spectrum = root.displayedSpectrumPeaks
            waveform.peaks = spectrum
            playedWaveform.peaks = spectrum
        } else {
            var layers = root.displayedWaveformLayers
            waveform.layers = layers
            playedWaveform.layers = layers
        }
    }

    function resetWaveformViewport() {
        if (waveform)
            waveform.setVisibleRange(0, Math.max(0, root.effectiveDurationMs))
    }

    function syncSelection() {
        if (!playbackController
                || playbackController.selectionEndMs
                    <= playbackController.selectionStartMs) {
            selectionOverlay.clearSelection()
            return
        }
        selectionOverlay.setSelection(playbackController.selectionStartMs,
                                      playbackController.selectionEndMs)
    }

    Component.onCompleted: {
        syncFilterControls()
        syncSelection()
        applyWaveformMode()
    }

    onDisplayedWaveformLayersChanged: {
        if (SettingsController.waveformMode !== 2)
            applyWaveformMode()
    }

    onDisplayedSpectrumPeaksChanged: {
        if (SettingsController.waveformMode === 2)
            applyWaveformMode()
    }

    onWaveformDurationMsChanged: {
        if (_waveformViewportResetPending && waveformDurationMs > 0)
            Qt.callLater(function() {
                if (!root._waveformViewportResetPending)
                    return
                root.resetWaveformViewport()
                root._waveformViewportResetPending = false
            })
    }

    Connections {
        target: root.filterModel
        function onSearchTextChanged() { root.syncFilterControls() }
        function onExactRatingChanged() { root.syncFilterControls() }
        function onMinBpmChanged() { root.syncFilterControls() }
        function onMaxBpmChanged() { root.syncFilterControls() }
    }

    Connections {
        target: root.playbackController
        function onSelectionStartMsChanged() { root.syncSelection() }
        function onSelectionEndMsChanged() { root.syncSelection() }
        function onCurrentTrackIdChanged() {
            root._waveformViewportResetPending = true
            root.resetWaveformViewport()
        }
        function onDurationMsChanged() {
            if (root._waveformViewportResetPending)
                root.resetWaveformViewport()
        }
        function onSpectrumChanged() {
            if (SettingsController.waveformMode === 2)
                root.applyWaveformMode()
        }
    }

    Connections {
        target: SettingsController
        function onWaveformModeChanged() { root.applyWaveformMode() }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        radius: Theme.radiusMd
        border.color: Theme.integratedSoftOutline
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            objectName: "integratedTopBar"
            Layout.fillWidth: true
            Layout.preferredHeight: root.topBarHeight
            window: root.hostWindow
            showBrand: true
            onOpenSettings: root.openSettingsRequested()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: root.contentSpacing
                anchors.rightMargin: root.contentSpacing
                spacing: 1

                Rectangle {
                    objectName: "integratedLibraryColumn"
                    Layout.preferredWidth: root.leftColumnWidth
                    Layout.minimumWidth: root.leftColumnWidth
                    Layout.maximumWidth: root.leftColumnWidth
                    Layout.fillHeight: true
                    color: Theme.navigationSurface
                    border.width: 0
                    radius: 0

                    SideNavigation {
                        id: sideNavigation
                        objectName: "integratedLibraryNavigation"
                        anchors.fill: parent
                        navigationModel: root.navigationModel
                        playlistModel: root.playlistModel
                        selectedCategory: root.filterModel
                                          ? root.filterModel.category : "all"
                        selectedTagKey: root.filterModel
                                        ? root.filterModel.tagKey : ""
                        selectedResourceFolder: root.filterModel
                                                ? root.filterModel.resourceFolder : ""
                        showTagManagementEntry: false
                        onCategorySelected: function(category) {
                            if (root.filterModel)
                                root.filterModel.category = category
                        }
                        onNavigationSelected: function(nodeType, nodeId,
                                                       resourceFolder) {
                            if (!root.filterModel)
                                return
                            if (nodeType === "resourceRoot" || nodeType === "resourceFolder")
                                root.filterModel.category = "all"
                            root.filterModel.resourceFolder = resourceFolder
                            if (nodeType !== "tags") {
                                root.filterModel.tagKey = ""
                                TagModel.selectedKey = ""
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "integratedTrackColumn"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.contentSurface
                    border.width: 0
                    radius: 0

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        TrackList {
                            objectName: "integratedTrackList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: root.defaultTrackListHeight
                            trackModel: root.filterModel || root.libraryModel
                            playlistModel: root.playlistModel
                            selectedCategory: root.filterModel
                                              ? root.filterModel.category : "all"
                            tagFilterActive: root.filterModel
                                             ? root.filterModel.tagKey.length > 0 : false
                            activeTagKey: root.filterModel
                                          ? root.filterModel.tagKey : ""
                            layoutProfile: "integrated"
                            thumbnailVisibilityFollowsSetting: false
                        }

                        SearchFilter {
                            id: searchFilter
                            objectName: "integratedSearchFilter"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            integratedStyle: true
                            onSearchTextChanged: {
                                if (root.filterModel
                                        && root.filterModel.searchText !== searchText)
                                    root.filterModel.searchText = searchText
                            }
                            onExactRatingChanged: {
                                if (root.filterModel
                                        && root.filterModel.exactRating !== exactRating)
                                    root.filterModel.exactRating = exactRating
                            }
                            onMinBpmChanged: {
                                if (root.filterModel
                                        && root.filterModel.minBpm !== minBpm)
                                    root.filterModel.minBpm = minBpm
                            }
                            onMaxBpmChanged: {
                                if (root.filterModel
                                        && root.filterModel.maxBpm !== maxBpm)
                                    root.filterModel.maxBpm = maxBpm
                            }
                        }
                    }
                }

                LibrarySidePanel {
                    id: sidePanelColumn
                    objectNamePrefix: "integrated"
                    expandedWidth: root.rightColumnWidth
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

        Rectangle {
            id: waveformFrame
            objectName: "integratedWaveformFrame"
            Layout.fillWidth: true
            Layout.preferredHeight: root.waveformHeight
                                    + root.waveformNavigatorHeight
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            Layout.leftMargin: root.contentSpacing
            Layout.rightMargin: root.contentSpacing
            color: Theme.panel
            border.color: Theme.integratedSoftOutline
            border.width: 1
            radius: Theme.radiusSm
            clip: true

            WaveformItem {
                id: waveform
                objectName: "integratedWaveform"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: waveformNavigator.top
                anchors.topMargin: 20
                pointerInteractionEnabled: false
                duration: root.effectiveDurationMs
                // The base pass remains entirely unplayed. A fully played
                // duplicate below is clipped to this cursor's exact pixel.
                position: 0
                cursorPosition: root.playbackPositionMs
                analysisProgress: root.waveformProvider
                                  ? root.waveformProvider.analysisProgress : 0
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
                amplitudeScale: SettingsController.waveformHeight
                density: SettingsController.waveformDensity
                lineWidth: SettingsController.waveformThickness
            }

            Item {
                id: playedWaveformClip
                objectName: "integratedWaveformPlayedClip"
                anchors.left: waveform.left
                anchors.top: waveform.top
                width: waveform.waveformCursorX
                height: waveform.height
                clip: true
                enabled: false

                WaveformItem {
                    id: playedWaveform
                    objectName: "integratedPlayedWaveform"
                    enabled: false
                    width: waveform.width
                    height: waveform.height
                    duration: waveform.duration
                    position: duration
                    visibleStartMs: waveform.visibleStartMs
                    visibleEndMs: waveform.visibleEndMs
                    analysisProgress: waveform.analysisProgress
                    visualMode: waveform.visualMode
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

            Rectangle {
                id: integratedPlaybackGuide
                objectName: "integratedWaveformPlaybackGuide"
                visible: SettingsController.waveformPlaybackGuide
                x: waveform.x + waveform.waveformCursorX
                y: waveform.y
                width: 1
                height: waveform.height
                color: Theme.playbackGuide
                z: 10
            }

            Item {
                id: timeRuler
                objectName: "integratedWaveformTimeRuler"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 20
                z: 6
                Repeater {
                    model: 7
                    delegate: Item {
                        required property int index
                        x: index * (timeRuler.width - 1) / 6
                        width: 1
                        height: timeRuler.height
                        Rectangle {
                            width: 1
                            height: 5
                            color: Theme.border
                        }
                        Text {
                            anchors.top: parent.top
                            anchors.topMargin: 6
                            x: index === 0 ? 4 : index === 6
                               ? -implicitWidth - 4 : -implicitWidth / 2
                            text: root.formatScaleTime(waveform.visibleStartMs
                                  + (waveform.visibleEndMs
                                     - waveform.visibleStartMs) * index / 6)
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                    }
                }
            }

            WaveSelectionOverlay {
                id: selectionOverlay
                objectName: "integratedWaveSelectionOverlay"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: waveform.top
                anchors.bottom: waveform.bottom
                z: 4
                durationMs: root.effectiveDurationMs
                visibleStartMs: waveform.visibleStartMs
                visibleEndMs: waveform.visibleEndMs
                onZoomRequested: function(x, factor) {
                    waveform.zoomAt(x, factor)
                    root.waveformZoomRequested(factor, waveform.timeForX(x))
                }
                onSeekRequested: function(positionMs) {
                    if (root.playbackController) {
                        var outside = selectionOverlay.selectionActive
                                && (positionMs < selectionOverlay.selectionStartMs
                                    || positionMs > selectionOverlay.selectionEndMs)
                        if (outside)
                            root.playbackController.disableSelectionLoopAndSeek(
                                        positionMs)
                        else
                            root.playbackController.seek(positionMs)
                        root.playbackController.play()
                    }
                    root.seekRequested(positionMs)
                }
                onHoverPositionMsChanged: {
                    waveform.setHoverPositionForInteraction(hoverPositionMs)
                }
                dragAdapter: PlaybackClipDragAdapter
                currentTrackId: root.playbackController
                                ? root.playbackController.currentTrackId : ""
                onSelectionCommitted: function(startMs, endMs) {
                    if (root.playbackController)
                        root.playbackController.commitSelection(startMs, endMs)
                }
                onSelectionAdjusted: function(startMs, endMs) {
                    if (root.playbackController)
                        root.playbackController.adjustSelection(startMs, endMs)
                }
                onSelectionClearRequested: {
                    if (root.playbackController)
                        root.playbackController.clearSelection()
                }
                onSelectionLoopRequested: function(startMs, endMs) {
                    root.selectionLoopRequested(startMs, endMs)
                }
                onDragClipRequested: function(startMs, endMs) {
                    root.dragClipRequested(startMs, endMs)
                }
            }

            Rectangle {
                objectName: "integratedWaveformHoverGuide"
                visible: SettingsController.waveformHoverTimePreview
                         && selectionOverlay.hoverPositionMs >= 0
                x: Math.max(0, Math.min(parent.width - width,
                                        waveform.pixelForTime(
                                            selectionOverlay.hoverPositionMs)))
                y: waveform.y
                width: 1
                height: waveform.height
                color: Theme.accent
                opacity: 0.96
                z: 7
            }

            Rectangle {
                objectName: "integratedWaveformHoverTime"
                visible: SettingsController.waveformHoverTimePreview
                         && selectionOverlay.hoverPositionMs >= 0
                x: Math.max(0, Math.min(
                                waveformFrame.width - width,
                                waveform.pixelForTime(
                                    selectionOverlay.hoverPositionMs)
                                - width / 2))
                y: waveform.y + 2
                width: integratedHoverTime.implicitWidth + 12
                height: integratedHoverTime.implicitHeight + 6
                radius: height / 2
                color: Theme.panel
                border.color: Theme.accent
                z: 8

                Text {
                    id: integratedHoverTime
                    anchors.centerIn: parent
                    text: root.formatScaleTime(selectionOverlay.hoverPositionMs)
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
                }
            }

            Item {
                id: waveformNavigator
                objectName: "integratedWaveformNavigator"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: root.waveformNavigatorHeight
                z: 9

                Rectangle {
                    objectName: "integratedWaveformNavigatorTrack"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    height: 3
                    radius: 1.5
                    color: Theme.navigatorGlassTrack
                }

                Rectangle {
                    id: navigatorThumb
                    objectName: "integratedWaveformNavigatorThumb"
                    readonly property real rangeFraction:
                        root.effectiveDurationMs > 0
                        ? (waveform.visibleEndMs - waveform.visibleStartMs)
                          / root.effectiveDurationMs : 1
                    x: root.effectiveDurationMs > 0
                       ? waveform.visibleStartMs / root.effectiveDurationMs
                         * waveformNavigator.width : 0
                    width: Math.max(18, Math.min(waveformNavigator.width,
                                                 waveformNavigator.width
                                                 * rangeFraction))
                    height: 6
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 3
                    color: Theme.navigatorGlassThumb
                    border.color: Theme.integratedSoftOutline
                    border.width: 1
                    opacity: rangeFraction < 0.999 ? 1 : 0.72

                    Rectangle {
                        objectName: "integratedWaveformNavigatorHighlight"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        height: 1
                        radius: 0.5
                        color: Theme.integratedGlassHighlight
                    }

                    MouseArea {
                        id: navigatorPointer
                        anchors.fill: parent
                        enabled: navigatorThumb.rangeFraction < 0.999
                        cursorShape: enabled ? Qt.SizeHorCursor : Qt.ArrowCursor
                        property real pressOffset: 0
                        onPressed: function(mouse) {
                            pressOffset = mouse.x
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed || root.effectiveDurationMs <= 0)
                                return
                            var point = mapToItem(waveformNavigator,
                                                  mouse.x, mouse.y)
                            var travel = Math.max(0, waveformNavigator.width
                                                    - navigatorThumb.width)
                            if (travel <= 0)
                                return
                            var desiredX = Math.max(
                                        0, Math.min(travel,
                                                    point.x - pressOffset))
                            var span = waveform.visibleEndMs
                                     - waveform.visibleStartMs
                            var maxStart = Math.max(
                                        0, root.effectiveDurationMs - span)
                            var nextStart = Math.round(
                                        desiredX / travel * maxStart)
                            waveform.setVisibleRange(nextStart,
                                                     nextStart + span)
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "integratedBottomBar"
            Layout.fillWidth: true
            Layout.preferredHeight: root.bottomBarHeight
            Layout.leftMargin: root.contentSpacing
            Layout.rightMargin: root.contentSpacing
            Layout.bottomMargin: root.contentSpacing
            color: Theme.elevated
            border.color: Theme.integratedSoftOutline
            border.width: 1
            radius: Theme.radiusSm

            Item {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10

                RowLayout {
                    id: trackSummary
                    objectName: "integratedTrackSummary"
                    z: 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    height: 56
                    // Keep the summary clear of the transport cluster at the
                    // 1180 px minimum window while still letting it grow on
                    // wider layouts.
                    width: Math.min(420, Math.max(160,
                                                  parent.width * 0.25 - 6))
                    spacing: 12
                    readonly property var track: {
                        var count = root.libraryModel ? root.libraryModel.count : 0
                        return count > 0 && root.playbackController
                            ? root.libraryModel.trackForId(
                                  root.playbackController.currentTrackId) : null
                    }
                    Rectangle {
                        objectName: "integratedTrackCover"
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 56
                        color: Theme.panel
                        border.color: Theme.integratedSoftOutline
                        border.width: 1
                        radius: Theme.radiusSm
                        clip: true

                        FallbackCoverImage {
                            id: integratedTrackCoverImage
                            requestedSource: String(trackSummary.track
                                                    && trackSummary.track.coverUrl
                                                    ? trackSummary.track.coverUrl : "")
                            anchors.fill: parent
                            fillMode: !usingFallback
                                      ? Image.PreserveAspectCrop
                                      : Image.PreserveAspectFit
                            anchors.margins: usingFallback ? 8 : 0
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: trackSummary.track
                                  && trackSummary.track.title
                                  ? trackSummary.track.title : qsTr("未选择歌曲")
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeSection
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        TrackSubtitle {
                            objectName: "integratedTrackSubtitle"
                            Layout.fillWidth: true
                            artist: trackSummary.track
                                    && trackSummary.track.artist
                                    ? trackSummary.track.artist : ""
                            album: trackSummary.track
                                   && trackSummary.track.album
                                   ? trackSummary.track.album : ""
                            tags: trackSummary.track && trackSummary.track.tags
                                  ? trackSummary.track.tags : []
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        RowLayout {
                            id: integratedTrackMetadata
                            objectName: "integratedTrackMetadata"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 18
                            spacing: 4
                            clip: true

                            Repeater {
                                model: {
                                    var track = trackSummary.track
                                    if (!track)
                                        return []
                                    var badges = []
                                    if (track.format)
                                        badges.push(track.format.toUpperCase())
                                    if (track.bitDepth > 0)
                                        badges.push(track.bitDepth + "-bit")
                                    if (track.sampleRate > 0)
                                        badges.push((track.sampleRate / 1000)
                                                    + " kHz")
                                    if (track.bitRate > 0)
                                        badges.push(Math.round(track.bitRate
                                                               / 1000)
                                                    + " kbps")
                                    if (track.bpm > 0)
                                        badges.push((Math.round(track.bpm * 10)
                                                     / 10) + " BPM")
                                    if (track.fileSize > 0)
                                        badges.push((track.fileSize / 1048576)
                                                    .toFixed(1) + " MB")
                                    return badges
                                }

                                Rectangle {
                                    implicitWidth: integratedBadgeText.implicitWidth
                                                   + 10
                                    implicitHeight: 18
                                    color: Theme.subtleGlassFill
                                    border.color: Theme.integratedSoftOutline
                                    border.width: 1
                                    radius: 4

                                    Text {
                                        id: integratedBadgeText
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

                Loader {
                    id: bottomBarLoader
                    z: 1
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    sourceComponent: root.bottomBarComponent
                                     ? root.bottomBarComponent : defaultBottomBar
                    onLoaded: {
                        item.width = Qt.binding(function() {
                            return bottomBarLoader.width
                        })
                        item.height = Qt.binding(function() {
                            return bottomBarLoader.height
                        })
                        if (item["leftReservedWidth"] !== undefined) {
                            item.leftReservedWidth = Qt.binding(function() {
                                return trackSummary.width
                            })
                        }
                    }
                }
            }
        }
    }

    Component {
        id: defaultBottomBar
        PlayerControls {}
    }
}
