import QtQuick
import QtQuick.Controls
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
    property int sidePanelPage: 0
    property bool sidePanelExpanded: true

    property int topBarHeight: 52
    property int leftColumnWidth: 248
    property int rightColumnWidth: 312
    property int waveformHeight: 120
    property int waveformNavigatorHeight: 10
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
                    id: sidePanelColumn
                    objectName: "integratedTagColumn"
                    Layout.preferredWidth: root.sidePanelExpanded
                                           ? root.rightColumnWidth : 42
                    Layout.minimumWidth: Layout.preferredWidth
                    Layout.maximumWidth: Layout.preferredWidth
                    Layout.fillHeight: true
                    color: Theme.panel
                    border.color: Theme.border
                    border.width: 1
                    radius: Theme.radiusSm

                    Item {
                        anchors.fill: parent
                        anchors.margins: root.sidePanelExpanded ? 10 : 7

                        RowLayout {
                            id: sidePanelHeader
                            objectName: "integratedSidePanelHeader"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 30
                            spacing: 4

                            Button {
                                id: tagTabButton
                                objectName: "integratedTagTabButton"
                                visible: root.sidePanelExpanded
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                flat: true
                                text: qsTr("标签管理") + " (" + (root.tagModel
                                      ? root.tagModel.rowCount() : 0) + ")"
                                Accessible.name: qsTr("标签管理")
                                onClicked: root.sidePanelPage = 0
                                contentItem: Text {
                                    text: tagTabButton.text
                                    color: root.sidePanelPage === 0
                                           ? Theme.primaryText
                                           : Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 15
                                    font.weight: root.sidePanelPage === 0
                                                 ? Font.DemiBold : Font.Normal
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    objectName: "integratedTagTabOutline"
                                    color: root.sidePanelPage === 0
                                           ? Theme.subtleGlassActive
                                           : tagTabButton.hovered
                                             ? Theme.subtleGlassHover
                                             : Theme.subtleGlassFill
                                    border.color: root.sidePanelPage === 0
                                                  ? Theme.accent
                                                  : Theme.subtleGlassBorder
                                    border.width: 1
                                    radius: Theme.radiusSm
                                }
                            }

                            Button {
                                id: lyricsTabButton
                                objectName: "integratedLyricsTabButton"
                                visible: root.sidePanelExpanded
                                Layout.preferredWidth: 72
                                Layout.fillHeight: true
                                flat: true
                                text: qsTr("歌词")
                                Accessible.name: text
                                onClicked: root.sidePanelPage = 1
                                contentItem: Text {
                                    text: lyricsTabButton.text
                                    color: root.sidePanelPage === 1
                                           ? Theme.primaryText
                                           : Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 15
                                    font.weight: root.sidePanelPage === 1
                                                 ? Font.DemiBold : Font.Normal
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    objectName: "integratedLyricsTabOutline"
                                    color: root.sidePanelPage === 1
                                           ? Theme.subtleGlassActive
                                           : lyricsTabButton.hovered
                                             ? Theme.subtleGlassHover
                                             : Theme.subtleGlassFill
                                    border.color: root.sidePanelPage === 1
                                                  ? Theme.accent
                                                  : Theme.subtleGlassBorder
                                    border.width: 1
                                    radius: Theme.radiusSm
                                }
                            }

                            Item { Layout.fillWidth: !root.sidePanelExpanded }

                            ToolButton {
                                id: sidePanelToggleButton
                                objectName: "integratedSidePanelToggleButton"
                                Layout.preferredWidth: 34
                                Layout.preferredHeight: 30
                                padding: 2
                                flat: true
                                Accessible.name: root.sidePanelExpanded
                                                 ? qsTr("隐藏标签和歌词侧栏")
                                                 : qsTr("显示标签和歌词侧栏")
                                onClicked: root.sidePanelExpanded =
                                           !root.sidePanelExpanded
                                ToolTip.text: Accessible.name
                                ToolTip.visible: hovered
                                background: Rectangle {
                                    color: sidePanelToggleButton.hovered
                                           ? Theme.hoverSurface : "transparent"
                                    radius: Theme.radiusSm
                                }
                                contentItem: ThemedIcon {
                                    objectName: "integratedSidePanelToggleIcon"
                                    source: Theme.icon("side-panel-toggle")
                                    tint: Theme.iconPrimary
                                    sourceSize.width: 26
                                    sourceSize.height: 26
                                    mirror: !root.sidePanelExpanded
                                }
                            }
                        }

                        Item {
                            objectName: "integratedSidePanelContent"
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: sidePanelHeader.bottom
                            anchors.topMargin: 8
                            anchors.bottom: parent.bottom
                            visible: root.sidePanelExpanded

                            Item {
                                id: tagContent
                                objectName: "integratedTagContent"
                                anchors.fill: parent
                                visible: root.sidePanelPage === 0

                                TagManagementPanel {
                                    id: tagPanel
                                    objectName: "integratedTagManagementPanel"
                                    anchors.fill: parent
                                    tagModel: root.tagModel
                                    filterModel: root.filterModel
                                    compact: true
                                    collapsible: false
                                    expanded: true
                                    showHeader: false
                                }
                            }

                            ScrollView {
                                id: lyricsContent
                                objectName: "integratedLyricsContent"
                                anchors.fill: parent
                                visible: root.sidePanelPage === 1
                                clip: true

                                TextArea {
                                    id: lyricsText
                                    objectName: "integratedLyricsText"
                                    width: lyricsContent.availableWidth
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    padding: 8
                                    text: root.playbackController
                                          && root.playbackController.lyrics
                                          && root.playbackController.lyrics.trim().length > 0
                                          ? root.playbackController.lyrics
                                          : qsTr("当前歌曲暂无内嵌歌词")
                                    color: root.playbackController
                                           && root.playbackController.lyrics
                                           && root.playbackController.lyrics.trim().length > 0
                                           ? Theme.primaryText
                                           : Theme.secondaryText
                                    font.family: Theme.fontPrimary
                                    font.pixelSize: 13
                                    horizontalAlignment: text === qsTr("当前歌曲暂无内嵌歌词")
                                                         ? Text.AlignHCenter
                                                         : Text.AlignLeft
                                    background: null
                                }
                            }
                        }
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
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: waveformNavigator.top
                anchors.topMargin: 20
                pointerInteractionEnabled: false
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
                    id: playedWaveform
                    objectName: "integratedPlayedWaveform"
                    width: waveformFrame.width
                    height: waveform.height
                    enabled: false
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
                color: "#54ff84" // theme-color-allow: shared waveform hover guide
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
                border.color: "#54ff84" // theme-color-allow: shared waveform hover guide
                z: 8

                Text {
                    id: integratedHoverTime
                    anchors.centerIn: parent
                    text: root.formatScaleTime(selectionOverlay.hoverPositionMs)
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 11
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
                    border.color: Theme.subtleGlassBorder
                    border.width: 1
                    opacity: rangeFraction < 0.999 ? 1 : 0.72

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
            border.color: Theme.border
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
                    height: 66
                    width: Math.min(450, parent.width * 0.32)
                    spacing: 12
                    readonly property var track: {
                        var count = root.libraryModel ? root.libraryModel.count : 0
                        return count > 0 && root.playbackController
                            ? root.libraryModel.trackForId(
                                  root.playbackController.currentTrackId) : null
                    }
                    Rectangle {
                        objectName: "integratedTrackCover"
                        Layout.preferredWidth: 66
                        Layout.preferredHeight: 66
                        color: Theme.panel
                        border.color: Theme.subtleGlassBorder
                        border.width: 1
                        radius: Theme.radiusSm
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: trackSummary.track
                                    && trackSummary.track.coverUrl
                                    ? trackSummary.track.coverUrl
                                    : "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                            fillMode: trackSummary.track
                                      && trackSummary.track.coverUrl
                                      ? Image.PreserveAspectCrop
                                      : Image.PreserveAspectFit
                            anchors.margins: trackSummary.track
                                             && trackSummary.track.coverUrl
                                             ? 0 : 8
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
                                    border.color: Theme.subtleGlassBorder
                                    border.width: 1
                                    radius: 4

                                    Text {
                                        id: integratedBadgeText
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: Theme.secondaryText
                                        font.family: Theme.fontPrimary
                                        font.pixelSize: 10
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
