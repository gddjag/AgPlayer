import QtQuick
import QtQuick.Layouts
import AgPlayer

Item {
    id: root
    objectName: "integratedPlayerShell"
    implicitWidth: 1672
    implicitHeight: 941

    // The shell never creates a second model.  Its host injects the same
    // filter state owned by Classic; singleton defaults only preserve the
    // existing production bindings when the shell is loaded directly.
    property var filterModel: null
    property var libraryModel: LibraryModel
    property var playlistModel: PlaylistModel
    property var navigationModel: LibraryNavigationModel
    property var tagModel: TagModel
    property var playbackController: PlaybackController
    property var waveformProvider: WaveformProvider
    property var waveformLayers: ({})
    property real waveformDurationMs: 0
    property Component bottomBarComponent: null
    property alias tagSearchText: tagPanel.searchText
    property var hostWindow: null

    property int topBarHeight: 52
    property int leftColumnWidth: 248
    property int rightColumnWidth: 312
    property int waveformHeight: 120
    property int bottomBarHeight: 83
    property int contentSpacing: 8
    readonly property real effectiveDurationMs: waveformDurationMs > 0
                                                ? waveformDurationMs
                                                : (playbackController
                                                   ? playbackController.durationMs : 0)
    readonly property real playbackPositionMs: playbackController
                                              ? playbackController.positionMs : 0
    readonly property var displayedWaveformLayers: {
        if (SettingsController.waveformMode === 2)
            return ({})
        var source = waveformLayers || ({})
        return {
            mix: source.mix || [],
            _sampleRate: Number(source._sampleRate) || 0,
            _totalSamples: Number(source._totalSamples) || 0,
            _peakCount: Number(source._peakCount) || 0
        }
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
                spacing: root.contentSpacing

                Rectangle {
                    objectName: "integratedLibraryColumn"
                    Layout.preferredWidth: root.leftColumnWidth
                    Layout.minimumWidth: root.leftColumnWidth
                    Layout.maximumWidth: root.leftColumnWidth
                    Layout.fillHeight: true
                    color: Theme.panel
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm

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
                            root.filterModel.resourceFolder = resourceFolder
                            if (nodeType !== "tags")
                                root.filterModel.tagKey = ""
                        }
                    }
                }

                Rectangle {
                    objectName: "integratedTrackColumn"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.panel
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        TrackList {
                            objectName: "integratedTrackList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            trackModel: root.filterModel || root.libraryModel
                            playlistModel: root.playlistModel
                            selectedCategory: root.filterModel
                                              ? root.filterModel.category : "all"
                            tagFilterActive: root.filterModel
                                             ? root.filterModel.tagKey.length > 0 : false
                            activeTagKey: root.filterModel
                                          ? root.filterModel.tagKey : ""
                            integratedCompact: true
                        }

                        SearchFilter {
                            id: searchFilter
                            objectName: "integratedSearchFilter"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 46
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

                Rectangle {
                    objectName: "integratedTagColumn"
                    Layout.preferredWidth: tagPanel.expanded
                                           ? root.rightColumnWidth : 42
                    Layout.minimumWidth: Layout.preferredWidth
                    Layout.maximumWidth: Layout.preferredWidth
                    Layout.fillHeight: true
                    color: Theme.panel
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm

                    TagManagementPanel {
                        id: tagPanel
                        objectName: "integratedTagManagementPanel"
                        anchors.fill: parent
                        tagModel: root.tagModel
                        filterModel: root.filterModel
                        compact: true
                        collapsible: true
                        expanded: true
                    }
                }
            }
        }

        Rectangle {
            id: waveformFrame
            objectName: "integratedWaveformFrame"
            Layout.fillWidth: true
            Layout.preferredHeight: root.waveformHeight
            Layout.topMargin: 24
            Layout.bottomMargin: 12
            Layout.leftMargin: root.contentSpacing
            Layout.rightMargin: root.contentSpacing
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm
            clip: true

            WaveformItem {
                id: waveform
                objectName: "integratedWaveform"
                anchors.fill: parent
                anchors.topMargin: 20
                pointerInteractionEnabled: false
                layers: root.displayedWaveformLayers
                peaks: root.displayedSpectrumPeaks
                duration: root.effectiveDurationMs
                cursorPosition: root.playbackPositionMs
                analysisProgress: root.waveformProvider
                                  ? root.waveformProvider.analysisProgress : 0
                visualMode: SettingsController.waveformMode
                baseColor: SettingsController.waveformMode === 0
                           ? SettingsController.waveformSolidBaseColor
                           : SettingsController.waveformRgbBaseColor
                progressColor: SettingsController.waveformSolidProgressColor
                gradientStartColor: SettingsController.waveformRgbStartColor
                gradientMiddleColor: SettingsController.waveformRgbMiddleColor
                gradientEndColor: SettingsController.waveformRgbEndColor
                rgbProgress: SettingsController.waveformMode === 1
                             && SettingsController.waveformRgbProgress
                amplitudeScale: SettingsController.waveformHeight
                density: SettingsController.waveformDensity
                lineWidth: SettingsController.waveformThickness
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
                            font.pixelSize: 10
                        }
                    }
                }
            }

            Item {
                width: waveform.waveformCursorX
                y: waveform.y
                height: waveform.height
                clip: true
                enabled: false
                WaveformItem {
                    width: waveformFrame.width
                    height: waveform.height
                    enabled: false
                    layers: root.displayedWaveformLayers
                    peaks: root.displayedSpectrumPeaks
                    duration: root.effectiveDurationMs
                    visibleStartMs: waveform.visibleStartMs
                    visibleEndMs: waveform.visibleEndMs
                    position: root.effectiveDurationMs
                    analysisProgress: waveform.analysisProgress
                    visualMode: waveform.visualMode
                    baseColor: waveform.baseColor
                    progressColor: waveform.progressColor
                    gradientStartColor: waveform.gradientStartColor
                    gradientMiddleColor: waveform.gradientMiddleColor
                    gradientEndColor: waveform.gradientEndColor
                    rgbProgress: waveform.rgbProgress
                    amplitudeScale: waveform.amplitudeScale
                    density: waveform.density
                    lineWidth: waveform.lineWidth
                }
            }

            WaveSelectionOverlay {
                id: selectionOverlay
                objectName: "integratedWaveSelectionOverlay"
                anchors.fill: parent
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
                    }
                    root.seekRequested(positionMs)
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
                onSelectionLoopRequested: function(startMs, endMs) {
                    root.selectionLoopRequested(startMs, endMs)
                }
                onDragClipRequested: function(startMs, endMs) {
                    root.dragClipRequested(startMs, endMs)
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
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusSm

            Item {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10

                RowLayout {
                    id: trackSummary
                    z: 2
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Math.min(450, parent.width * 0.32)
                    spacing: 10
                    readonly property var track: {
                        var count = root.libraryModel ? root.libraryModel.count : 0
                        return count > 0 && root.playbackController
                            ? root.libraryModel.trackForId(
                                  root.playbackController.currentTrackId) : null
                    }
                    Image {
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 58
                        source: trackSummary.track
                                && trackSummary.track.coverUrl
                                ? trackSummary.track.coverUrl
                                : "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                        fillMode: Image.PreserveAspectCrop
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: trackSummary.track
                                  && trackSummary.track.title
                                  ? trackSummary.track.title : qsTr("未选择歌曲")
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: trackSummary.track
                                  && trackSummary.track.artist
                                  ? trackSummary.track.artist : ""
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            elide: Text.ElideRight
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
