import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MainWindowControls"
    when: windowShown

    property var mainWindow: null
    property var suiteWindowState: null
    property var task4StateSnapshot: null
    property var task4TemporaryTagKeys: []
    Component {
        id: videoPlaybackStateComponent
        QtObject {
            property bool visible: false
            property bool loading: false
            property string errorMessage: ""
            property int dismissCalls: 0
            function dismiss() {
                ++dismissCalls
                visible = false
            }
        }
    }

    Component {
        id: videoPlaybackTransportComponent
        QtObject {
            enum PlaybackState { Stopped, Playing }
            property int state: 1
            property int positionMs: 0
            property int durationMs: 120000
            property real speedRatio: 1.0
            property bool muted: false
            property real volume: 0.5
            property int stopCalls: 0
            property bool stopHidesVideo: true
            property var videoState: null
            property var fullscreenProbe: null
            property var stopFullscreenSamples: []
            function previous() {}
            function togglePlayback() {}
            function next() {}
            function seek(value) { positionMs = Math.round(value) }
            function setSpeedRatio(value) { speedRatio = value }
            function toggleMuted() { muted = !muted }
            function setVolume(value) { volume = value }
            function stop() {
                stopFullscreenSamples.push(Boolean(fullscreenProbe
                                                   && fullscreenProbe.videoFullscreen))
                ++stopCalls
                state = 0
                if (videoState && stopHidesVideo)
                    videoState.visible = false
            }
        }
    }
    Component {
        id: fileDropAreaComponent
        FileDropArea {}
    }

    Component {
        id: signalSpyComponent
        SignalSpy {}
    }

    Component {
        id: trackListComponent
        TrackList {
            width: 1100
            height: 500
            // Standalone component tests keep covering the legacy detailed
            // table; production shells select an explicit layout profile.
            layoutProfile: "detailed"
        }
    }

    Component {
        id: listWindowComponent
        ListWindow {
            visible: true
            width: 1000
            height: 620
        }
    }

    Component {
        id: lyricsSidePanelComponent
        LibrarySidePanel {
            width: 312
            height: 420
            currentPage: 1
            expanded: true
            onExpandedRequested: function(value) { expanded = value }
        }
    }

    Component {
        id: defaultListWindowComponent
        ListWindow { visible: true }
    }

    Component {
        id: sideNavigationComponent
        SideNavigation {
            width: 208
            height: 500
            recentAddedCount: 7
            neverPlayedCount: 11
            property string lastSelectedCategory: ""
            property int renameRequestCount: 0
            property int exportRequestCount: 0
            property int deleteRequestCount: 0
            property string lastRequestedPlaylistId: ""
            onCategorySelected: function(category) {
                lastSelectedCategory = category
            }
            onRenamePlaylistRequested: function(playlistId) {
                renameRequestCount += 1
                lastRequestedPlaylistId = playlistId
            }
            onExportPlaylistRequested: function(playlistId) {
                exportRequestCount += 1
                lastRequestedPlaylistId = playlistId
            }
            onRemovePlaylistRequested: function(playlistId) {
                deleteRequestCount += 1
                lastRequestedPlaylistId = playlistId
            }
        }
    }

    Component {
        id: emptyLibraryNavigationComponent
        SideNavigation {
            width: 208
            height: 260
            navigationModel: ListModel {
                ListElement {
                    nodeId: "library:all"
                    nodeType: "library"
                    depth: 0
                    displayName: "我的音乐库"
                    count: 0
                    expanded: true
                    resourceFolder: ""
                    hasChildren: false
                }
            }
        }
    }

    Component {
        id: dockedWindowFrameComponent
        DockedWindowFrame {
            width: 320
            height: 180
        }
    }

    Component {
        id: searchFilterComponent
        SearchFilter {
            width: 900
            height: 54
        }
    }

    Component {
        id: emptyLibraryComponent
        EmptyLibrary {
            width: 900
            height: 420
        }
    }

    Component {
        id: settingsPageComponent
        SettingsPage {
            width: 860
            height: 640
        }
    }

    Component {
        id: trackWaveformThumbnailItemComponent
        TrackWaveformThumbnailItem {
            width: 128
            height: 10
        }
    }

    Component {
        id: tagManagementPanelWindowComponent
        Window {
            visible: true
            width: 400
            height: 540
            property alias filterModel: tagPanel.filterModel
            TagManagementPanel {
                id: tagPanel
                anchors.fill: parent
            }
        }
    }

    Component {
        id: tagManagementPanelComponent
        TagManagementPanel {
            width: 400
            height: 500
        }
    }

    Component {
        id: resourceNavigationWindowComponent
        Window {
            visible: true
            width: 260
            height: 620
            SideNavigation {
                objectName: "resourceTestNavigation"
                anchors.fill: parent
            }
        }
    }


    Component {
        id: fakeThumbnailProviderComponent
        QtObject {
            property int requestCount: 0
            property int cancelCount: 0
            property var requests: []
            property var requestEvidence: []
            property var requestObserver: null
            property var cancellations: []
            property string lastTrackId: ""
            property string lastSourcePath: ""
            property int lastGeneration: -1
            signal thumbnailReady(string trackId, int generation, var peaks)
            signal sourceCacheInvalidated(string sourcePath)

            function request(trackId, sourcePath, generation) {
                var observed = requestObserver ? requestObserver(trackId) : null
                requestCount += 1
                var nextRequests = requests.slice()
                nextRequests.push({ "trackId": trackId,
                                    "sourcePath": sourcePath,
                                    "generation": generation })
                requests = nextRequests
                if (observed) {
                    var nextEvidence = requestEvidence.slice()
                    nextEvidence.push({
                        "trackId": trackId,
                        "hasDelegate": observed.hasDelegate === true,
                        "hasLoader": observed.hasLoader === true,
                        "loaderEnabled": observed.loaderEnabled === true,
                        "loaderHasItem": observed.loaderHasItem === true,
                        "loaderItemTrackId": observed.loaderItemTrackId || "",
                        "loaderActive": observed.loaderActive === true,
                        "rowTop": observed.rowTop,
                        "rowBottom": observed.rowBottom,
                        "viewportTop": observed.viewportTop,
                        "viewportBottom": observed.viewportBottom
                    })
                    requestEvidence = nextEvidence
                }
                lastTrackId = trackId
                lastSourcePath = sourcePath
                lastGeneration = generation
            }
            function cancel(trackId, generation) {
                cancelCount += 1
                var nextCancellations = cancellations.slice()
                nextCancellations.push({ "trackId": trackId,
                                         "generation": generation })
                cancellations = nextCancellations
            }
            function colorForTrackId(trackId) {
                return "#7f6aa8"
            }
        }
    }

    Component {
        id: trackWaveformThumbnailComponent
        TrackWaveformThumbnail {
            width: 128
            height: 9
        }
    }

    Component {
        id: deferredThumbnailDestroyHostComponent
        Item {
            id: deferredHost
            property var thumbnailProvider
            readonly property var wrapper: delegateLoader.item
                                           ? delegateLoader.item.wrapper : null

            function deactivateBeforeNextRequest() {
                Qt.callLater(function() {
                    delegateLoader.active = false
                })
                wrapper.trackId = "destroyed-before-request"
                wrapper.sourcePath = "destroyed-before-request.wav"
                wrapper.delegateGeneration = 47
            }

            Loader {
                id: delegateLoader
                active: true
                sourceComponent: Component {
                    Item {
                        id: delegateContext
                        property alias wrapper: thumbnailLoader.item
                        property var thumbnailProvider:
                            deferredHost.thumbnailProvider
                        Loader {
                            id: thumbnailLoader
                            active: true
                            sourceComponent: Component {
                                TrackWaveformThumbnail {
                                    width: 128
                                    height: 9
                                    provider:
                                        delegateContext.thumbnailProvider
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: isolatedTrackModelComponent
        ListModel {}
    }

    Component {
        id: trackListHostComponent
        Item {
            width: 800
            height: 210
        }
    }

    Component {
        id: destructiveTrackDropTargetComponent
        Rectangle {
            width: 180
            height: 140
            property int dropCount: 0
            DropArea {
                id: destructiveDropArea
                objectName: "destructiveTrackDropArea"
                anchors.fill: parent
                keys: ["application/x-agplayer-track-ids"]
                onDropped: function(drop) {
                    var encoded = drop.getDataAsString(
                                "application/x-agplayer-track-ids")
                    var ids = encoded ? JSON.parse(encoded) : []
                    if (ids.length === 0 && drop.source
                            && drop.source.dragTrackIds)
                        ids = drop.source.dragTrackIds
                    if (ids.length > 0)
                        LibraryModel.removeTrack(ids[0])
                    destructiveDropArea.parent.dropCount += 1
                    drop.acceptProposedAction()
                }
            }
        }
    }

    Component {
        id: trackListWindowComponent
        Window {
            visible: true
            width: 900
            height: 360
            property alias list: hostedTrackList
            TrackList {
                id: hostedTrackList
                anchors.fill: parent
            }
        }
    }

    Component {
        id: stackedTrackListHostComponent
        StackLayout {
            width: 800
            height: 210
            currentIndex: 0
            property alias list: stackedTrackList
            TrackList {
                id: stackedTrackList
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            Item {}
        }
    }

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined", "testMainWindow context property should exist")
        mainWindow = testMainWindow
        verify(mainWindow, "Failed to create Main window")
        wait(50)
        suiteWindowState = {
            "width": mainWindow.width,
            "height": mainWindow.height,
            "visibility": mainWindow.visibility,
            "playerShellMode": SettingsController.playerShellMode,
            "windowLayoutTheme": SettingsController.windowLayoutTheme,
            "playback": mainWindow.playback,
            "videoPlayback": mainWindow.videoPlayback
        }
    }

    function restoreMainWindowState() {
        if (!mainWindow || !suiteWindowState)
            return

        var settingsWindow = findChild(mainWindow, "settingsWindow")
        if (settingsWindow && settingsWindow.visible)
            settingsWindow.close()
        if (mainWindow.videoFullscreen)
            mainWindow.exitVideoFullscreen()
        mainWindow.playback = suiteWindowState.playback
        mainWindow.videoPlayback = suiteWindowState.videoPlayback
        PlaybackController.pause()
        SettingsController.playerShellMode = suiteWindowState.playerShellMode
        SettingsController.windowLayoutTheme = suiteWindowState.windowLayoutTheme

        if (suiteWindowState.visibility === Window.Maximized)
            mainWindow.showMaximized()
        else
            mainWindow.showNormal()
        if (suiteWindowState.visibility !== Window.Maximized) {
            mainWindow.width = suiteWindowState.width
            mainWindow.height = suiteWindowState.height
        }
        mainWindow.requestActivate()
        wait(0)
    }

    function init() {
        restoreMainWindowState()
        failOnWarning(/.?/)
        var filter = findChild(mainWindow, "filterModel")
        task4StateSnapshot = {
            "thumbnailEnabled": SettingsController.listWaveformThumbnailEnabled,
            "thumbnailMode": SettingsController.listWaveformThumbnailMode,
            "selectedTagKey": TagModel.selectedKey,
            "tagKey": filter ? filter.tagKey : "",
            "category": filter ? filter.category : "all",
            "resourceFolder": filter ? filter.resourceFolder : "",
            "searchText": filter ? filter.searchText : "",
            "exactRating": filter ? filter.exactRating : 0,
            "minBpm": filter ? filter.minBpm : 60,
            "maxBpm": filter ? filter.maxBpm : 160
        }
        task4TemporaryTagKeys = []
    }

    function verifyAscendingX(parent, names) {
        var previousX = -1
        for (var i = 0; i < names.length; ++i) {
            var item = findChild(parent, names[i])
            verify(item !== null, "missing " + names[i])
            verify(item.visible, names[i] + " must be visible")
            var x = item.mapToItem(parent, 0, 0).x
            verify(x > previousX, names[i] + " is out of order")
            previousX = x
        }
    }


    function requiredFileInfoRowsAreHydrated(panel) {
        if (!panel || !panel.rows)
            return false
        var requiredValueKeys = [
            "format", "sampleRate", "bitDepth", "channels", "bitRate",
            "duration", "fileSize", "path"
        ]
        for (var rowIndex = 0; rowIndex < panel.rows.length; ++rowIndex) {
            var row = panel.rows[rowIndex]
            if (requiredValueKeys.indexOf(row.key) >= 0
                    && String(row.value || "").length === 0)
                return false
        }
        return true
    }

    function positionMenuActionInViewport(menu, action) {
        var menuView = menu.contentItem
        verify(menuView !== null, "context menu must expose a content viewport")
        verify(typeof menuView.positionViewAtIndex === "function",
               "context menu viewport must support item positioning")
        var actionIndex = -1
        for (var index = 0; index < menu.count; ++index) {
            if (menu.itemAt(index) === action) {
                actionIndex = index
                break
            }
        }
        verify(actionIndex >= 0,
               "file information action must be a real context menu item")
        menuView.positionViewAtIndex(actionIndex, ListView.End)
        wait(0)
        var actionPosition = action.mapToItem(menuView, 0, 0)
        verify(actionPosition.x >= 0
               && actionPosition.x + action.width <= menuView.width
               && actionPosition.y >= 0
               && actionPosition.y + action.height <= menuView.height,
               "file information action must be entirely inside the menu viewport")
    }

    function cleanup() {
        for (var index = 0; index < task4TemporaryTagKeys.length; ++index)
            TagModel.removeTag(task4TemporaryTagKeys[index])
        if (task4StateSnapshot) {
            var filter = findChild(mainWindow, "filterModel")
            if (filter) {
                filter.tagKey = task4StateSnapshot.tagKey
                filter.category = task4StateSnapshot.category
                filter.resourceFolder = task4StateSnapshot.resourceFolder
                filter.searchText = task4StateSnapshot.searchText
                filter.exactRating = task4StateSnapshot.exactRating
                filter.minBpm = task4StateSnapshot.minBpm
                filter.maxBpm = task4StateSnapshot.maxBpm
            }
            TagModel.selectedKey = task4StateSnapshot.selectedTagKey
            SettingsController.listWaveformThumbnailMode =
                    task4StateSnapshot.thumbnailMode
            SettingsController.listWaveformThumbnailEnabled =
                    task4StateSnapshot.thumbnailEnabled
            task4StateSnapshot = null
        }
        restoreMainWindowState()
    }

    function countObjectsNamed(parentObject, expectedName) {
        if (!parentObject)
            return 0
        var total = parentObject.objectName === expectedName ? 1 : 0
        var childItems = parentObject.children || []
        for (var index = 0; index < childItems.length; ++index)
            total += countObjectsNamed(childItems[index], expectedName)
        return total
    }

    function trackRowForId(parentObject, trackId) {
        if (!parentObject)
            return null
        if ((parentObject.objectName === "trackRow"
                || parentObject.objectName === "currentTrackRow")
                && parentObject.trackId === trackId)
            return parentObject
        var childItems = parentObject.children || []
        for (var index = 0; index < childItems.length; ++index) {
            var match = trackRowForId(childItems[index], trackId)
            if (match)
                return match
        }
        return null
    }

    function verifyNewThumbnailRequestsAreVisible(provider, firstRequest,
                                                   phase) {
        verify(provider.requests.length > firstRequest,
               phase + " must issue at least one thumbnail request")
        compare(provider.requestEvidence.length, provider.requests.length,
                phase + " must record geometry for every request")
        for (var index = firstRequest;
             index < provider.requestEvidence.length; ++index) {
            var evidence = provider.requestEvidence[index]
            compare(evidence.trackId, provider.requests[index].trackId)
            verify(evidence.hasDelegate,
                   phase + " requested a track without an active delegate: "
                   + evidence.trackId)
            verify(evidence.loaderActive,
                   phase + " requested a track without an active waveform Loader: "
                   + evidence.trackId + " hasLoader=" + evidence.hasLoader
                   + " enabled=" + evidence.loaderEnabled
                   + " hasItem=" + evidence.loaderHasItem
                   + " itemTrackId=" + evidence.loaderItemTrackId)
            verify(evidence.rowBottom > evidence.viewportTop
                   && evidence.rowTop < evidence.viewportBottom,
                   phase + " requested an offscreen row: " + evidence.trackId
                   + " row=[" + evidence.rowTop + ","
                   + evidence.rowBottom + ") viewport=["
                   + evidence.viewportTop + ","
                   + evidence.viewportBottom + ")")
        }
    }

    function tagRowForKey(key) {
        for (var row = 0; row < TagModel.rowCount(); ++row) {
            var modelIndex = TagModel.index(row, 0)
            if (TagModel.data(modelIndex, TagModel.KeyRole) === key)
                return row
        }
        return -1
    }

    function colorContrast(first, second) {
        function channel(value) {
            return value <= 0.04045 ? value / 12.92
                                    : Math.pow((value + 0.055) / 1.055, 2.4)
        }
        function luminance(color) {
            return 0.2126 * channel(color.r)
                    + 0.7152 * channel(color.g)
                    + 0.0722 * channel(color.b)
        }
        var firstLuminance = luminance(first)
        var secondLuminance = luminance(second)
        return (Math.max(firstLuminance, secondLuminance) + 0.05)
                / (Math.min(firstLuminance, secondLuminance) + 0.05)
    }

    function createIsolatedTrackModel(prefix, count) {
        var model = isolatedTrackModelComponent.createObject(testCase)
        var path = nativeDropHelper.localFilePath(testAudioUrl)
        for (var row = 0; row < count; ++row) {
            model.append({
                "trackId": prefix + row,
                "path": path,
                "title": "Pool track " + row,
                "artist": "AgPlayer QA",
                "album": "Pool reuse",
                "coverUrl": "",
                "favorite": false,
                "rating": 0,
                "bpm": 120,
                "durationMs": 1000,
                "available": true,
                "fileStatus": "available"
            })
        }
        return model
    }

    function test_docked_window_frame_removes_shared_edge_and_contact_corners() {
        var mainFrame = dockedWindowFrameComponent.createObject(mainWindow.contentItem, {
            "dockEdge": "bottom",
            "windowRole": "main"
        })
        verify(mainFrame)
        compare(mainFrame.topLeftRadius, mainFrame.windowRadius)
        compare(mainFrame.bottomLeftRadius, 0)
        compare(mainFrame.bottomRightRadius, 0)
        compare(mainFrame.bottomBorderVisible, false)

        var listFrame = dockedWindowFrameComponent.createObject(mainWindow.contentItem, {
            "dockEdge": "bottom",
            "windowRole": "list"
        })
        verify(listFrame)
        compare(listFrame.topLeftRadius, 0)
        compare(listFrame.topRightRadius, 0)
        compare(listFrame.topBorderVisible, false)

        mainFrame.destroy()
        listFrame.destroy()
    }

    function cleanupTestCase() {
        mainWindow = null
    }

    function test_visible_controls_have_actions() {
        verify(mainWindow !== null, "Main window should exist")
        verify(findChild(mainWindow, "playPauseButton"), "playPauseButton should exist")
        verify(findChild(mainWindow, "previousButton"), "previousButton should exist")
        verify(findChild(mainWindow, "nextButton"), "nextButton should exist")
        verify(findChild(mainWindow, "modeButton"), "modeButton should exist")
        verify(findChild(mainWindow, "volumeSlider"), "volumeSlider should exist")
        verify(findChild(mainWindow, "miniPlayerButton"), "miniPlayerButton should exist")
        verify(findChild(mainWindow, "settingsButton"), "settingsButton should exist in Phase 2 settings task")
        verify(findChild(mainWindow, "audioToolsButton"), "audioToolsButton should exist in Phase 2.1")
        verify(findChild(mainWindow, "equalizerButton"),
               "equalizerButton should expose the real eighteen-control EQ")
        verify(findChild(mainWindow, "listWindowButton"), "listWindowButton should exist")
        verify(findChild(mainWindow, "playerCover"), "player cover should exist")
        verify(findChild(mainWindow, "playerCoverImage").source.toString().length > 0,
               "player cover should always have a fallback source")
        var trackTitle = findChild(mainWindow, "trackTitle")
        var titleViewport = findChild(mainWindow, "trackTitleViewport")
        var favoriteButton = findChild(mainWindow, "favoriteButton")
        verify(trackTitle, "track title should exist")
        verify(titleViewport && titleViewport.clip,
               "long titles must scroll inside a clipped hover viewport")
        verify(favoriteButton, "favorite button should exist")
        verify(favoriteButton.background === null,
               "favorite button should not render a platform-style square")
        verify(favoriteButton.x <= trackTitle.x + trackTitle.implicitWidth + 40,
               "favorite button should remain next to the title")
        verify(findChild(mainWindow, "trackArtistAlbum"), "artist and album should exist")
        verify(findChild(mainWindow, "trackRating"), "track rating should exist")
        verify(findChild(mainWindow, "mainWaveform"), "main waveform should exist")
        compare(findChild(mainWindow, "playerCover").radius, 14)
    }

    function test_thumbnail_runtime_types_are_unique_and_idle() {
        // Catches registering factories/fallback objects instead of the exact
        // application-owned model graph, or doing cache work before a Loader
        // exists in TrackList (integration is deliberately a later task).
        verify(TagModel === expectedTagModel)
        verify(TrackWaveformThumbnailProvider === expectedThumbnailProvider)
        var diagnostics = TrackWaveformThumbnailProvider.diagnostics()
        verify(diagnostics.cacheReadAttempts >= 0)
        verify(diagnostics.queuedJobs >= 0)

        var item = trackWaveformThumbnailItemComponent.createObject(
                    mainWindow.contentItem)
        verify(item)
        item.destroy()
    }

    function test_equalizer_opens_apple_style_eighteen_control_window() {
        var button = findChild(mainWindow, "equalizerButton")
        verify(button)
        mouseClick(button)
        var window = findChild(mainWindow, "equalizerWindow")
        tryVerify(function() { return window && window.visible }, 1000)
        compare(window.width, 1080)
        compare(window.height, 620)
        compare(window.minimumWidth, 960)
        compare(window.minimumHeight, 460)
        var equalizerTitle = findChild(window, "equalizerTitle")
        var equalizerContent = findChild(window, "equalizerContent")
        verify(equalizerTitle,
               "compact EQ must retain system-readable title text")
        verify(equalizerContent,
               "EQ must lay out at native size instead of shrinking a large canvas")
        compare(equalizerContent.scale, 1)
        compare(equalizerTitle.text, qsTr("18 段图形均衡器"))
        compare(equalizerTitle.font.pixelSize, Theme.fontSizePageTitle)
        compare(findChild(window, "equalizerTitleBar").height, 44)
        compare(findChild(window, "equalizerHeaderPanel").height, 48)
        verify(findChild(window, "equalizerFrame").radius <= 8,
               "the compact EQ must use restrained system-style corners")
        verify(findChild(window, "equalizerResponsePanel").radius <= 8)
        compare(findChild(window, "equalizerMinimizeButton").width, 28)
        verify(findChild(window, "equalizerEnabledSwitch"))
        compare(button.contentItem.rotation, 90)
        var previousThemeMode = SettingsController.themeMode
        SettingsController.themeMode = 0
        compare(button.icon.color.toString(), Theme.iconPrimary.toString())
        SettingsController.themeMode = 1
        compare(button.icon.color.toString(), Theme.iconPrimary.toString())
        SettingsController.themeMode = previousThemeMode
        var responseCurve = findChild(window, "equalizerResponseCurve")
        verify(responseCurve)
        var responseEnvelope = responseCurve.responsePoints()
        var dspResponse = EqualizerController.responseCurve(160)
        compare(responseEnvelope.length, 160)
        compare(Math.round(responseEnvelope[0].x),
                Math.round(responseCurve.plotLeft))
        compare(Math.round(responseEnvelope[159].x),
                Math.round(responseCurve.width - responseCurve.plotRight))
        compare(Math.round(responseEnvelope[0].y * 1000),
                Math.round(responseCurve.gainY(
                               dspResponse[0]) * 1000))
        var presetBox = findChild(window, "equalizerPresetBox")
        verify(presetBox)
        for (var themeMode = 0; themeMode <= 1; ++themeMode) {
            SettingsController.themeMode = themeMode
            presetBox.popup.open()
            tryVerify(function() { return presetBox.popup.visible })
            compare(presetBox.popup.background.color.toString(),
                    Theme.elevated.toString())
            compare(presetBox.popup.background.border.color.toString(),
                    Theme.border.toString())
            presetBox.popup.close()
        }
        SettingsController.themeMode = previousThemeMode
        var bands = findChild(window, "equalizerBandRepeater")
        verify(bands)
        compare(bands.count, 18)
        compare(bands.itemAt(14).frequencyLabel, "10k")
        compare(findChild(window, "equalizerBandsMaxLabel").text, "+12")
        compare(findChild(window, "equalizerBandsZeroLabel").text, "0")
        compare(findChild(window, "equalizerBandsMinLabel").text, "−12")
        verify(findChild(window, "equalizerPreampSlider"))
        verify(findChild(window, "equalizerRangeControl"))
        verify(findChild(window, "equalizerPrecisionControl"))
        verify(findChild(window, "equalizerOutputMeter"))
        verify(findChild(window, "equalizerSaveDialog"))
        verify(findChild(window, "equalizerManagePopup"))
        var resetButton = findChild(window, "equalizerResetButton")
        verify(resetButton)
        verify(resetButton.iconSource.toString().indexOf("restore-line.svg") >= 0)
        var contentScroller = findChild(window, "equalizerContentScroller")
        compare(findChild(window, "equalizerContentScrollBar").policy,
                ScrollBar.AlwaysOff)
        compare(findChild(window, "equalizerBandScrollBar").policy,
                ScrollBar.AlwaysOff)
        compare(findChild(window, "equalizerFooterScrollBar").policy,
                ScrollBar.AlwaysOff)
        contentScroller.contentY = Math.min(300,
                    contentScroller.contentHeight - contentScroller.height)
        wait(50)
        EqualizerController.resetAll()
        var firstBand = bands.itemAt(0)
        verify(firstBand)
        firstBand.setGain(3.2)
        tryCompare(firstBand, "gainDb", 3.2)
        compare(EqualizerController.bandGain(0), 3.2)
        compare(EqualizerController.currentPresetId, "custom")
        compare(presetBox.displayText, qsTr("自定义"))
        var firstBandControl = findChild(firstBand, "eqBandSlider-0-control")
        verify(firstBandControl)
        firstBand.setGain(12)
        tryCompare(firstBandControl, "value", 12)
        verify(firstBandControl.visualPosition < 0.05,
               "+12 dB must map to the top of a vertical EQ slider")
        firstBand.setGain(-12)
        tryCompare(firstBandControl, "value", -12)
        verify(firstBandControl.visualPosition > 0.95,
               "-12 dB must map to the bottom of a vertical EQ slider")
        mouseDoubleClickSequence(firstBandControl,
                                 firstBandControl.width / 2,
                                 firstBandControl.height / 2)
        wait(80)
        compare(firstBand.gainDb, 0,
                "double-clicking an EQ band must reset it to 0 dB")
        verify(EqualizerController.setGainRangeDb(18))
        verify(EqualizerController.setPrecisionMode("medium"))
        firstBand.setGain(6)
        tryCompare(firstBand, "gainDb", 6)
        firstBandControl.forceActiveFocus()
        keyPress(Qt.Key_Up)
        tryCompare(firstBand, "gainDb", 6.5)
        keyPress(Qt.Key_PageUp)
        tryCompare(firstBand, "gainDb", 11.5)
        keyPress(Qt.Key_PageDown)
        tryCompare(firstBand, "gainDb", 6.5)
        mouseWheel(firstBandControl, firstBandControl.width / 2,
                   firstBandControl.height / 2, 0, -120)
        tryCompare(firstBand, "gainDb", 6.0)
        var firstBandLabel = findChild(firstBand, "eqBandSlider-0-frequency")
        verify(firstBandLabel)
        mouseDoubleClickSequence(firstBandLabel, firstBandLabel.width / 2,
                                 firstBandLabel.height / 2)
        wait(80)
        compare(firstBand.gainDb, 0,
                "double-clicking an EQ frequency label must reset it to 0 dB")
        contentScroller.contentY = 0
        wait(50)
        var manageButton = findChild(window, "equalizerManageButton")
        var managePopup = findChild(window, "equalizerManagePopup")
        mouseClick(manageButton)
        tryCompare(managePopup, "visible", true)
        verify(findChild(managePopup, "equalizerBypassButton"))
        verify(findChild(managePopup, "equalizerAutoProtection"))
        managePopup.close()
        EqualizerController.resetAll()
        EqualizerController.setGainRangeDb(12)
        EqualizerController.setPrecisionMode("high")
        EqualizerController.bypassed = false
        window.hide()
    }

    function test_empty_library_opens_directly_into_the_player() {
        PlaybackController.pause()
        nativeDropHelper.clearTracks()
        tryVerify(function() { return LibraryModel.count === 0 }, 500)
        compare(findChild(mainWindow, "emptyStartup"), null,
                "the standalone startup page must be removed")
        verify(findChild(mainWindow, "playerPane").visible,
               "the player surface must remain visible with an empty library")
        compare(findChild(mainWindow, "playerControls").emptyMode, false)
        verify(findChild(mainWindow, "waveformModeButton"),
               "waveform mode action should exist in the bottom control bar")
        verify(findChild(mainWindow, "audioToolsButton").visible,
               "audio tools action should be visible in the bottom control bar")
        verify(findChild(mainWindow, "listWindowButton").visible,
               "playlist action should be visible in the bottom control bar")
        verify(findChild(mainWindow, "miniPlayerButton").visible,
               "the direct player surface retains the normal mini action")
    }

    function test_empty_playlist_shows_centered_import_action_and_formats() {
        var empty = emptyLibraryComponent.createObject(mainWindow.contentItem, {
            "playlistMode": true,
            "width": 320
        })
        verify(empty)
        var formats = findChild(empty, "emptyLibraryFormats")
        compare(findChild(empty, "emptyLibraryTitle").text, "Import music")
        verify(formats.text.indexOf("MP3") >= 0)
        compare(formats.wrapMode, Text.WordWrap)
        verify(formats.lineCount >= 1,
               "the supported-format explanation must remain readable")
        verify(formats.paintedWidth <= empty.width - 24,
               "the single format line must tighten to fit the minimum width; "
               + "painted=" + formats.paintedWidth + ", width=" + formats.width
               + ", font=" + formats.font.pixelSize)
        compare(formats.font.pixelSize, Theme.fontSizeCaption,
                "the minimum-width state must wrap instead of shrinking text")
        compare(findChild(empty, "emptyImportButton").text, "Import music")
        empty.destroy()

        var libraryEmpty = emptyLibraryComponent.createObject(
                    mainWindow.contentItem, {
                        "playlistMode": false,
                        "width": 320
                    })
        verify(libraryEmpty)
        var libraryFormats = findChild(libraryEmpty, "emptyLibraryFormats")
        compare(libraryFormats.wrapMode, Text.WordWrap,
                "single-line tightening is specific to empty playlists")
        compare(libraryFormats.font.pixelSize, Theme.fontSizeCaption,
                "empty-state copy uses the shared caption token")
        libraryEmpty.destroy()
    }

    function test_failed_import_surfaces_status_in_main() {
        var status = findChild(mainWindow, "importStatusPanel")
        verify(status, "main window should expose shared import status")

        mainWindow.importFiles([Qt.resolvedUrl("file:///agplayer-missing-test.wav")])
        tryVerify(function() {
            return !ImportController.busy && ImportController.errors.length > 0
        }, 3000)

        verify(status.active, "import errors should activate the status panel")
        verify(status.visible, "import errors should be visible in the main window")
        compare(findChild(mainWindow, "emptyStartup"), null,
                "import errors must stay on the direct player surface")
    }

    function test_import_files_reaches_real_controller() {
        verify(testAudioUrl.toString().length > 0,
               "generated audio fixture should be available")
        // Earlier drag/drop tests populate this shared library. Start this
        // import assertion from its own empty state, including async completion.
        tryCompare(ImportController, "busy", false, 5000)
        PlaybackController.stop()
        nativeDropHelper.clearLibrary()
        compare(LibraryModel.count, 0)
        mainWindow.importFiles([testAudioUrl])
        tryVerify(function() { return LibraryModel.count === 1 }, 5000)
    }

    function test_loaded_player_keeps_brand_and_balanced_controls() {
        var brand = findChild(mainWindow, "titleBrand")
        var center = findChild(mainWindow, "centerPlaybackControls")
        var listButton = findChild(mainWindow, "listWindowButton")
        var miniButton = findChild(mainWindow, "miniPlayerButton")
        verify(brand && brand.visible, "brand must remain visible after loading a track")
        verify(center && listButton && miniButton)
        var centerPoint = center.mapToItem(mainWindow.contentItem,
                                           center.width / 2,
                                           center.height / 2)
        var listPoint = listButton.mapToItem(mainWindow.contentItem,
                                             listButton.width / 2,
                                             listButton.height / 2)
        var miniPoint = miniButton.mapToItem(mainWindow.contentItem,
                                             miniButton.width / 2,
                                             miniButton.height / 2)
        compare(Math.round(centerPoint.y), Math.round(listPoint.y))
        compare(Math.round(centerPoint.y), Math.round(miniPoint.y))
    }

    function test_immersive_window_file_drop_imports_and_starts_playback() {
        var rendering = mainWindow.immersiveRenderingEnabled
        var hostMode = PlayerExperienceController.hostMode
        mainWindow.immersiveRenderingEnabled = false
        PlayerExperienceController.hostMode = PlayerExperienceController.Windowed
        PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
        try {
            var coordinator = findChild(mainWindow, "immersiveCoordinator")
            tryVerify(function() { return coordinator.fullscreenWindow && coordinator.fullscreenWindow.visible }, 3000)
            tryCompare(coordinator, "attachedHostMode", PlayerExperienceController.Windowed, 3000)
            wait(100) // allow the newly exposed window to polish its DropArea
            var copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
            verify(copiedAudio)
            verify(nativeDropHelper.sendUrls(coordinator.fullscreenWindow, [copiedAudio]),
                   "The immersive canvas must accept a real Qt file drop")
            tryVerify(function() { return !ImportController.busy }, 5000)
            compare(ImportController.errors.length, 0)
            tryVerify(function() {
                return ImportController.importedTrackIds.length > 0
                    && PlaybackController.currentTrackId === ImportController.importedTrackIds[0]
            }, 3000)
        } finally {
            PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
            PlayerExperienceController.hostMode = hostMode
            mainWindow.immersiveRenderingEnabled = rendering
            mainWindow.visible = true
        }
    }

    function test_native_qt_drop_reaches_the_real_import_controller() {
        var previousCount = LibraryModel.count
        var copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio && copiedAudio.toString().length > 0)
        verify(nativeDropHelper.sendUrls(mainWindow, [copiedAudio]),
               "the native top-level window must accept a real URL drop")
        tryVerify(function() { return !ImportController.busy }, 5000)
        verify(ImportController.errors.length === 0)
        verify(LibraryModel.count > previousCount,
               "the routed drop must import the audio without a QML shortcut")
        tryVerify(function() {
            return ImportController.importedTrackIds.length > 0
                    && PlaybackController.currentTrackId
                    === ImportController.importedTrackIds[0]
        }, 3000)
    }

    function test_embedded_resource_folder_accepts_native_directory_drop_data() {
        return [
            { tag: "integrated", shellMode: 1,
              navigation: "integratedLibraryNavigation" },
            { tag: "rolling", shellMode: 2,
              navigation: "rollingLibraryNavigation" }
        ]
    }

    function test_embedded_resource_folder_accepts_native_directory_drop(data) {
        for (let warning = 0;
             !nativeDropHelper.supportsWindowsDropFiles() && warning < 4;
             ++warning)
            ignoreWarning(/This plugin does not support propagateSizeHints\(\)/)
        const previousShell = SettingsController.playerShellMode
        const folder = nativeDropHelper.createDropDirectory()
        verify(folder)
        let folderPath = nativeDropHelper.localFilePath(folder)
        folderPath = folderPath.replace(/\\/g, "/")
        const initialCount = ResourceFolderController.monitoredFolders.length
        SettingsController.playerShellMode = data.shellMode
        try {
            let navigation = null
            tryVerify(function() {
                navigation = findChild(mainWindow, data.navigation)
                return navigation && navigation.visible
            }, 1500)
            const navigationList = findChild(navigation, "libraryNavigationList")
            verify(navigationList)
            navigationList.positionViewAtEnd()
            wait(0)
            const dropTarget = findChild(navigation, "resourceFolderDropTarget")
            verify(dropTarget && dropTarget.visible)
            const delivered = nativeDropHelper.supportsWindowsDropFiles()
                    ? nativeDropHelper.sendWindowsDropFiles(dropTarget, [folder])
                    : nativeDropHelper.sendUrls(dropTarget, [folder])
            verify(delivered,
                   "native directory drop must be accepted by the embedded resource folder")
            tryCompare(ResourceFolderController.monitoredFolders, "length",
                       initialCount + 1, 1000)
            verify(ResourceFolderController.monitoredFolders.indexOf(folderPath) >= 0,
                   "the dropped directory must be registered as a monitored resource folder")
        } finally {
            ResourceFolderController.removeMonitoredFolder(folderPath)
            SettingsController.playerShellMode = previousShell
        }
    }

    // Run after the interaction suite: importing a real file deliberately
    // starts background metadata/waveform work and must not perturb unrelated
    // pointer-animation checks.
    function test_zz_main_window_has_a_qml_drop_fallback_for_shell_drag_routes() {
        var dropFallback = findChild(mainWindow, "mainFileDropFallback")
        verify(dropFallback,
               "the main window must retain a Qt drop fallback when Explorer does not send WM_DROPFILES")

        var copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio && copiedAudio.toString().length > 0)
        var previousCount = LibraryModel.count
        verify(dropFallback.submitUrls([copiedAudio]))
        tryVerify(function() { return !ImportController.busy }, 5000)
        verify(ImportController.errors.length === 0)
        verify(LibraryModel.count > previousCount,
               "the QML fallback must reach the real asynchronous importer")
    }

    function test_zzz_list_window_has_a_qml_drop_fallback_for_shell_drag_routes() {
        const listWindow = createTemporaryObject(listWindowComponent, testCase)
        verify(listWindow)
        tryVerify(function() { return listWindow.visible }, 1000)

        const dropFallback = findChild(listWindow, "listFileDropFallback")
        verify(dropFallback)
        const copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio && copiedAudio.toString().length > 0)
        const previousCount = LibraryModel.count
        verify(dropFallback.submitUrls([copiedAudio]))
        tryVerify(function() { return !ImportController.busy }, 5000)
        verify(ImportController.errors.length === 0)
        verify(LibraryModel.count > previousCount,
               "the list fallback must reach the real asynchronous importer")
        listWindow.destroy()
    }

    function test_zzzz_main_window_accepts_a_real_windows_dropfiles_message() {
        if (!nativeDropHelper.supportsWindowsDropFiles()) {
            skip("WM_DROPFILES requires the native qwindows platform plugin")
            return
        }
        const copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio && copiedAudio.toString().length > 0)
        const previousCount = LibraryModel.count
        verify(nativeDropHelper.sendWindowsDropFiles(mainWindow, [copiedAudio]),
               "the production main window must accept WM_DROPFILES")
        tryVerify(function() { return LibraryModel.count > previousCount },
                  5000, "WM_DROPFILES must reach the real asynchronous importer")
        tryVerify(function() { return !ImportController.busy }, 5000)
        verify(ImportController.errors.length === 0)
    }

    function test_track_list_supports_native_select_all_and_submenu() {
        if (LibraryModel.count === 0)
            nativeDropHelper.ensureSortableTracks()
        verify(LibraryModel.count > 0)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        mainWindow.requestActivate()
        wait(20)
        list.forceActiveFocus()
        wait(10)
        verify(list.activeFocus)
        keyClick(Qt.Key_A, Qt.ControlModifier)
        compare(list.selectedTrackIds.length, LibraryModel.count)
        verify(findChild(list, "moveTracksMenu"))
        var firstRow = list.itemAtIndex(0)
        verify(firstRow, "a visible track row should exist")
        var dragProxy = findChild(list, "trackDragProxy")
        verify(dragProxy)
        verify(dragProxy.Drag.keys.indexOf("application/x-agplayer-track-ids") >= 0,
               "track rows must advertise the same drag key accepted by playlists")
        var contextMenu = findChild(list, "trackContextMenu")
        verify(contextMenu, "track context menu must remain available")
        mouseClick(firstRow, firstRow.width / 2, firstRow.height / 2,
                   Qt.RightButton)
        tryVerify(function() { return contextMenu.visible }, 500)
        contextMenu.close()
        list.visible = false
        list.destroy()
        wait(10)
    }

    function test_track_context_menu_keeps_exact_action_order_and_labels() {
        if (LibraryModel.count === 0)
            nativeDropHelper.ensureSortableTracks()
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        list.positionViewAtBeginning()
        wait(30)
        var firstRow = list.itemAtIndex(0)
        verify(firstRow)
        mouseClick(firstRow, firstRow.width / 2, firstRow.height / 2,
                   Qt.RightButton)
        var menu = findChild(list, "trackContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)

        var expected = [
            ["trackMenuPlay", "播放"],
            ["trackMenuPlayNext", "下一首播放"],
            ["moveTracksMenu", "加入歌单"],
            ["audioToolsTrackMenu", "使用音频工具打开"],
            ["trackMenuShowFolder", "在文件夹中显示"],
            ["trackMenuCopyPath", "复制文件路径"],
            ["trackMenuTag", "打标签"],
            ["trackMenuMoveFile", "移动到指定文件夹"],
            ["trackMenuCopyFile", "复制到指定文件夹"],
            ["trackMenuRemove", "从列表删除"],
            ["trackMenuTrash", "彻底删除至回收站"]
        ]
        for (var index = 0; index < expected.length; ++index) {
            var action = findChild(menu, expected[index][0])
            verify(action, "missing context action " + expected[index][0])
            compare(action.text !== undefined ? action.text : action.title,
                    expected[index][1])
        }
        verify(!findChild(menu, "trackMenuRename"))
        verify(!findChild(menu, "trackMenuRelocate"))
        verify(!findChild(menu, "trackMenuDetails"),
               "right-click file information was explicitly removed")
        var playAction = findChild(menu, "trackMenuPlay")
        var trashAction = findChild(menu, "trackMenuTrash")
        verify(playAction && trashAction)
        verify(menu.width <= 160,
               "playlist context menu must shrink to its translated labels")
        compare(playAction.width, menu.width)
        compare(trashAction.width, menu.width)
        verify(trashAction.contentItem.implicitWidth
               <= trashAction.contentItem.width,
               "the longest action label must remain fully visible: "
               + trashAction.contentItem.implicitWidth + " > "
               + trashAction.contentItem.width + " (menu " + menu.width + ")")
        menu.close()
        list.destroy()
    }

    function test_track_header_stays_visible_while_rows_scroll() {
        var sortableIds = nativeDropHelper.ensureSortableTracks()
        var list = trackListComponent.createObject(mainWindow.contentItem,
                                                   { width: 760, height: 90 })
        verify(list)
        compare(list.headerPositioning, ListView.OverlayHeader)
        verify(list.headerItem)
        verify(list.headerItem.z > 0)
        list.positionViewAtEnd()
        wait(30)
        verify(list.headerItem.y >= list.contentY - 1,
               "the table header must remain over the scrolled rows")
        list.destroy()
    }

    function test_zzzzz_default_queue_density_keeps_ten_rows_visible() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        nativeDropHelper.ensureSortableTracks()
        var list = trackListComponent.createObject(mainWindow.contentItem,
                                                   { width: 960, height: 476 })
        verify(list)
        compare(list.rowHeight, Theme.listRowHeight)
        verify(list.headerItem)
        verify(Math.floor((list.height - list.headerItem.height)
                          / list.rowHeight) >= 10,
               "the default queue viewport must show ten full songs")
        list.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_list_window_default_height_keeps_ten_songs_visible() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        nativeDropHelper.ensureSortableTracks()
        var filterModel = findChild(mainWindow, "filterModel")
        filterModel.category = "all"
        filterModel.tagKey = ""
        filterModel.resourceFolder = ""
        for (var enabled of [false, true]) {
            SettingsController.listWaveformThumbnailEnabled = enabled
            var listWindow = createTemporaryObject(defaultListWindowComponent,
                                                   testCase,
                                                   { "filterModel": filterModel })
            verify(listWindow)
            var trackList = findChild(listWindow, "sharedTrackList")
            var filter = findChild(listWindow, "librarySearchFilter")
            verify(trackList && filter)
            var expectedRowHeight = enabled ? Theme.mediaListRowHeight
                                            : Theme.listRowHeight
            var expectedHeight = Theme.titleBarHeight + Theme.tableHeaderHeight
                    + 10 * expectedRowHeight + listWindow.filterBarHeight
            compare(listWindow.height, expectedHeight)
            compare(filter.height, listWindow.filterBarHeight)
            tryCompare(trackList, "height",
                       Theme.tableHeaderHeight + 10 * expectedRowHeight)
            listWindow.destroy()
        }

        SettingsController.listWaveformThumbnailEnabled = false
        var adjustable = createTemporaryObject(defaultListWindowComponent,
                                               testCase,
                                               { "filterModel": filterModel })
        verify(adjustable)
        adjustable.height = 677
        wait(0)
        compare(adjustable.height, 677)
        SettingsController.listWaveformThumbnailEnabled = true
        wait(0)
        compare(adjustable.height, 677,
                "changing thumbnail mode must not undo a user resize")
        adjustable.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }


    function test_track_list_context_menu_omits_file_information() {
        var trackIds = nativeDropHelper.ensureSortableTracks()
        verify(trackIds.length > 0)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        tryVerify(function() { return list.count > 0 }, 500)
        list.positionViewAtBeginning()
        wait(30)

        var menu = null
        try {
            var firstRow = list.itemAtIndex(0)
            verify(firstRow, "a visible track row should exist")
            mouseClick(firstRow, firstRow.width / 2, firstRow.height / 2,
                       Qt.RightButton)
            menu = findChild(list, "trackContextMenu")
            tryVerify(function() { return menu && menu.visible }, 500)
            compare(findChild(menu, "trackMenuDetails"), null)
        } finally {
            if (menu)
                menu.close()
            list.destroy()
        }
    }

    function test_legacy_library_category_returns_to_tracks_and_keeps_playlist_navigation() {
        var trackIds = nativeDropHelper.ensureSortableTracks()
        verify(trackIds.length > 0)
        var filter = findChild(mainWindow, "filterModel")
        filter.category = "all"
        filter.searchText = ""
        filter.tagKey = ""
        filter.resourceFolder = ""
        var playlistId = PlaylistModel.createPlaylist("legacy-route-" + Date.now())
        verify(playlistId.length > 0)
        verify(PlaylistModel.addTracks(playlistId, [trackIds[0]]))
        var window = listWindowComponent.createObject(null, { "filterModel": filter })
        verify(window)
        try {
            filter.category = "library"
            tryCompare(filter, "category", "all", 500)
            var list = findChild(window, "sharedTrackList")
            tryVerify(function() { return list.visible && list.count > 0 }, 500)
            verify(findChild(window, "librarySearchFilter").visible)
            verify(window.routeNavigationNode("favorites", "favorites:favorites", ""))
            compare(filter.category, "favorites")
            verify(window.routeNavigationNode("playlist", "playlist:" + playlistId, ""))
            compare(filter.category, playlistId)
            tryVerify(function() { return list.visible && list.count === 1 }, 500)
        } finally {
            window.destroy()
            filter.category = "all"
            PlaylistModel.removePlaylist(playlistId)
        }
    }


    function test_sidebar_exposes_required_top_level_nodes_and_linear_icons() {
        var side = sideNavigationComponent.createObject(mainWindow.contentItem)
        verify(side)
        verify(findChild(side, "navigationNode-library:all"))
        verify(findChild(side, "navigationNode-favorites:favorites"))
        verify(findChild(side, "navigationNode-tags:manage"))
        verify(!findChild(side, "historyCategoryButton"))
        verify(!findChild(side, "recentAddedCategoryButton"))
        verify(!findChild(side, "neverPlayedCategoryButton"))
        compare(side.iconForNode("library"), "music-2-line")
        compare(side.iconForNode("playlist"), "list-unordered")
        compare(side.iconForNode("tags"), "price-tag-3-line")
        compare(side.iconForNode("resourceFolder"), "folder-open-line")
        side.destroy()
    }

    function test_sidebar_does_not_resolve_an_empty_supplied_icon() {
        var side = sideNavigationComponent.createObject(mainWindow.contentItem)
        verify(side)
        var favoriteSuppliedIcon = findChild(side,
                                             "suppliedNodeIcon-favorites")
        verify(favoriteSuppliedIcon)
        compare(favoriteSuppliedIcon.visible, false)
        compare(favoriteSuppliedIcon.source.toString(), "")
        side.destroy()
    }

    function test_category_change_returns_track_list_to_top() {
        nativeDropHelper.ensureSortableTracks()
        var list = trackListComponent.createObject(mainWindow.contentItem,
                                                   { "height": 90 })
        verify(list)
        tryVerify(function() { return list.count >= 3 })
        list.positionViewAtEnd()
        wait(30)
        verify(list.contentY > list.originY + 1,
               "precondition: the short list viewport must be scrolled")

        list.selectedCategory = "favorites"
        tryVerify(function() {
            return list.contentY <= list.originY + 1
        }, 500, "changing categories must show the category from its top")
        list.destroy()
    }

    function test_track_context_play_action_uses_real_mouse_click() {
        mainWindow.importFiles([testAudioUrl])
        tryVerify(function() { return !ImportController.busy }, 5000)
        var playablePath = nativeDropHelper.localFilePath(testAudioUrl)
        var playableIndex = -1
        for (var row = 0; row < LibraryModel.count; ++row) {
            var path = String(LibraryModel.data(LibraryModel.index(row, 0),
                                                LibraryModel.PathRole))
                    .replace(/\\/g, "/")
            if (path.toLowerCase() === playablePath.toLowerCase()) {
                playableIndex = row
                break
            }
        }
        verify(playableIndex >= 0)
        var trackId = LibraryModel.data(LibraryModel.index(playableIndex, 0),
                                        LibraryModel.TrackIdRole)

        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        list.positionViewAtIndex(playableIndex, ListView.Center)
        wait(30)
        var rowItem = list.itemAtIndex(playableIndex)
        verify(rowItem)
        mouseClick(rowItem, rowItem.width / 2, rowItem.height / 2,
                   Qt.RightButton)
        var menu = findChild(list, "trackContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        var playAction = findChild(menu, "trackMenuPlay")
        verify(playAction && playAction.enabled)
        verify(playAction.width > 20 && playAction.height > 20,
               "visible menu actions need a clickable hit target")
        mouseClick(playAction, playAction.width / 2,
                   playAction.height / 2)
        tryCompare(PlaybackController, "currentTrackId", trackId, 1000)
        list.destroy()
    }

    function test_track_context_audio_tool_executes_submenu_command() {
        WindowController.hideAudioTools()
        AudioToolsController.selectTool(0)
        compare(WindowController.audioToolsVisible, false)
        compare(AudioToolsController.currentTool, 0)
        mainWindow.importFiles([testAudioUrl])
        tryVerify(function() { return !ImportController.busy }, 5000)
        var playablePath = nativeDropHelper.localFilePath(testAudioUrl)
        var playableIndex = -1
        for (var row = 0; row < LibraryModel.count; ++row) {
            var path = String(LibraryModel.data(LibraryModel.index(row, 0),
                                                LibraryModel.PathRole))
                    .replace(/\\/g, "/")
            if (path.toLowerCase() === playablePath.toLowerCase()) {
                playableIndex = row
                break
            }
        }
        verify(playableIndex >= 0)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        list.positionViewAtIndex(playableIndex, ListView.Center)
        wait(30)
        var rowItem = list.itemAtIndex(playableIndex)
        verify(rowItem)
        mouseClick(rowItem, rowItem.width / 2, rowItem.height / 2,
                   Qt.RightButton)
        var menu = findChild(list, "trackContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        var toolsMenu = findChild(menu, "audioToolsTrackMenu")
        verify(toolsMenu)
        var toolsMenuEntry = menu.itemAt(3)
        verify(toolsMenuEntry,
               "the audio-tools submenu must expose a visible parent action")
        compare(toolsMenuEntry.subMenu, toolsMenu)
        verify(toolsMenuEntry.arrow.visible)
        mouseMove(toolsMenuEntry, toolsMenuEntry.width / 2,
                  toolsMenuEntry.height / 2)
        if (!toolsMenu.visible)
            toolsMenu.open()
        tryVerify(function() { return toolsMenu.visible }, 500)
        var editorAction = findChild(toolsMenu, "trackMenuAudioEditor")
        verify(editorAction && editorAction.enabled)
        mouseClick(editorAction, editorAction.width / 2,
                   editorAction.height / 2)
        tryCompare(AudioToolsController, "currentTool", 0)
        tryCompare(WindowController, "audioToolsVisible", true)
        tryCompare(AudioEditorController, "hasDocument", true, 2000)
        verify(AudioEditorController.fileName.length > 0,
               "the context-menu action must load the selected audio file")
        toolsMenu.close()
        menu.close()
        WindowController.hideAudioTools()
        list.destroy()
    }

    function test_long_track_title_scrolls_only_while_hovered() {
        var trackId = nativeDropHelper.ensureLongTitleTrack()
        verify(trackId.length > 0)
        var list = trackListComponent.createObject(mainWindow.contentItem,
                                                   { width: 760, height: 260 })
        verify(list)
        var rowIndex = LibraryModel.indexForTrackId(trackId)
        list.positionViewAtIndex(rowIndex, ListView.Center)
        wait(30)
        var rowItem = list.itemAtIndex(rowIndex)
        verify(rowItem)
        var marquee = findChild(rowItem, "trackTitleMarquee")
        verify(marquee, "long titles need a dedicated clipped marquee surface")
        verify(marquee.overflowing)
        compare(marquee.textOffset, 0)
        mouseMove(marquee, marquee.width / 2, marquee.height / 2)
        tryVerify(function() { return marquee.textOffset < -1 }, 2500)
        mouseMove(list, 2, list.height - 2)
        tryCompare(marquee, "textOffset", 0, 500)
        list.destroy()
    }

    function test_z_album_and_artist_use_fixed_hover_marquee_columns() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var trackId = nativeDropHelper.ensureLongAlbumArtistTrack()
        var rowIndex = LibraryModel.indexForTrackId(trackId)
        verify(rowIndex >= 0)
        mainWindow.requestActivate()
        tryVerify(function() { return mainWindow.active }, 1000)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        list.positionViewAtIndex(rowIndex, ListView.Center)
        wait(30)
        var row = list.itemAtIndex(rowIndex)
        verify(row)
        var album = findChild(row, "trackAlbumMarquee")
        var artist = findChild(row, "trackArtistMarquee")
        verify(album && artist)
        compare(Math.round(album.width), 112)
        compare(Math.round(artist.width), 112)
        verify(album.overflowing && artist.overflowing)
        compare(album.textOffset, 0)
        mouseMove(list, 2, list.height - 2)
        wait(10)
        mouseMove(album, album.width / 2, album.height / 2)
        // This animation starts with a deliberate hover pause.  The parent
        // ApplicationWindow may have lost activation to a prior tool/settings
        // test, so activate it before supplying the real pointer move.
        tryVerify(function() { return album.textOffset < -1 }, 4500)
        mouseMove(list, 2, list.height - 2)
        tryCompare(album, "textOffset", 0, 500)
        list.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_fixed_track_columns_share_header_axis() {
        var list = trackListComponent.createObject(mainWindow.contentItem,
                                                   { width: 1050, height: 320 })
        verify(list)
        tryVerify(function() { return list.count > 0 })
        var row = list.itemAtIndex(0)
        verify(row)
        var names = ["Index", "Favorite", "Album", "Artist", "Rating", "Bpm", "Duration"]
        for (var i = 0; i < names.length; ++i) {
            var header = findChild(list, "trackHeader" + names[i])
            var cell = findChild(row, "track" + names[i] + "Cell")
            verify(header && cell, "missing fixed column " + names[i])
            compare(Math.round(header.mapToItem(list, 0, 0).x),
                    Math.round(cell.mapToItem(list, 0, 0).x))
            compare(Math.round(header.width), Math.round(cell.width))
        }
        compare(Math.round(findChild(row, "trackAlbumCell").width), 112)
        compare(Math.round(findChild(row, "trackArtistCell").width), 112)
        list.destroy()
    }

    function test_track_context_add_to_playlist_executes_submenu_command() {
        var ids = nativeDropHelper.ensureSortableTracks()
        verify(ids.length > 0)
        var playlistId = PlaylistModel.createPlaylist(
                    "Context add " + Date.now())
        verify(playlistId.length > 0)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        var rowIndex = LibraryModel.indexForTrackId(ids[0])
        list.positionViewAtIndex(rowIndex, ListView.Center)
        wait(30)
        var rowItem = list.itemAtIndex(rowIndex)
        verify(rowItem)
        mouseClick(rowItem, rowItem.width / 2, rowItem.height / 2,
                   Qt.RightButton)
        var menu = findChild(list, "trackContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        var moveMenu = findChild(menu, "moveTracksMenu")
        verify(moveMenu && moveMenu.enabled)
        var moveMenuEntry = menu.itemAt(2)
        verify(moveMenuEntry,
               "the playlist submenu must expose a visible parent action")
        compare(moveMenuEntry.contentItem.color.toString(),
                Theme.primaryText.toString())
        compare(moveMenuEntry.background.color.toString(),
                "#00000000")
        compare(moveMenuEntry.subMenu, moveMenu)
        verify(moveMenuEntry.arrow.visible)
        mouseMove(moveMenuEntry, moveMenuEntry.width / 2,
                  moveMenuEntry.height / 2)
        if (!moveMenu.visible)
            moveMenu.open()
        tryVerify(function() { return moveMenu.visible }, 500)
        var targetAction = findChild(moveMenu,
                                     "playlistMoveTarget-" + playlistId)
        verify(targetAction && targetAction.enabled)
        mouseClick(targetAction, targetAction.width / 2,
                   targetAction.height / 2)
        tryVerify(function() {
            return PlaylistModel.containsTrack(playlistId, ids[0])
        }, 500)
        moveMenu.close()
        menu.close()
        PlaylistModel.removePlaylist(playlistId)
        list.destroy()
    }

    function test_track_rows_reorder_with_real_mouse_drag() {
        var ids = nativeDropHelper.ensureSortableTracks()
        compare(ids.length, 3)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        tryCompare(list, "count", LibraryModel.count, 500)

        var fromIndex = LibraryModel.indexForTrackId(ids[0])
        var targetIndex = LibraryModel.indexForTrackId(ids[2])
        verify(fromIndex >= 0 && targetIndex > fromIndex)
        list.positionViewAtIndex(fromIndex, ListView.Beginning)
        wait(30)
        var fromRow = list.itemAtIndex(fromIndex)
        var targetRow = list.itemAtIndex(targetIndex)
        verify(fromRow && targetRow)
        verify(fromRow.available, "seeded drag row must be enabled")
        var dragArea = findChild(fromRow, "trackRowDragArea")
        verify(dragArea, "track title must expose a real drag surface")

        var deltaY = targetRow.mapToItem(fromRow, 0, targetRow.height / 2).y
        mouseDrag(dragArea, dragArea.width / 2, dragArea.height / 2,
                  0, deltaY - dragArea.height / 2, Qt.LeftButton,
                  Qt.NoModifier, 30)

        tryVerify(function() {
            return LibraryModel.indexForTrackId(ids[0]) + 1
                   === LibraryModel.indexForTrackId(ids[2])
        }, 500)
        list.destroy()
    }

    function test_track_title_surface_honors_ctrl_selection_and_double_click() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var ids = nativeDropHelper.ensureSortableTracks()
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        mainWindow.requestActivate()
        tryVerify(function() { return mainWindow.active }, 1000)
        list.positionViewAtBeginning()
        wait(30)
        var first = list.itemAtIndex(LibraryModel.indexForTrackId(ids[0]))
        var second = list.itemAtIndex(LibraryModel.indexForTrackId(ids[1]))
        verify(first && second)
        var firstArea = findChild(first, "trackRowDragArea")
        var secondArea = findChild(second, "trackRowDragArea")
        verify(firstArea && secondArea)
        mouseClick(firstArea, firstArea.width / 2, firstArea.height / 2)
        mouseClick(secondArea, secondArea.width / 2, secondArea.height / 2,
                   Qt.LeftButton, Qt.ControlModifier)
        compare(list.selectedTrackIds.length, 2)
        var playablePath = nativeDropHelper.localFilePath(testAudioUrl)
        var playableIndex = -1
        for (var rowIndex = 0; rowIndex < LibraryModel.count; ++rowIndex) {
            var rowPath = String(LibraryModel.data(
                                     LibraryModel.index(rowIndex, 0),
                                     LibraryModel.PathRole)).replace(/\\/g, "/")
            if (rowPath.toLowerCase() === playablePath.toLowerCase()) {
                playableIndex = rowIndex
                break
            }
        }
        verify(playableIndex >= 0)
        list.positionViewAtIndex(playableIndex, ListView.Center)
        wait(30)
        var playable = list.itemAtIndex(playableIndex)
        verify(playable)
        var playableArea = findChild(playable, "trackRowDragArea")
        verify(playableArea)
        mouseDoubleClickSequence(playableArea, playableArea.width / 2,
                                 playableArea.height / 2)
        tryCompare(PlaybackController, "currentTrackId",
                   LibraryModel.data(LibraryModel.index(playableIndex, 0),
                                     LibraryModel.TrackIdRole))

        // The shared integrated/rolling layout exposes the waveform as a
        // separate column.  It must remain an equally valid activation
        // target instead of only the title cell starting playback.
        SettingsController.listWaveformThumbnailEnabled = true
        list.layoutProfile = "integrated"
        wait(30)
        list.positionViewAtIndex(playableIndex, ListView.Center)
        wait(30)
        playable = list.itemAtIndex(playableIndex)
        verify(playable)
        var waveformLoader = findChild(
                    playable, "singleWindowWaveformThumbnailLoader")
        verify(waveformLoader && waveformLoader.visible)
        var waveformActivation = findChild(
                    waveformLoader, "singleWindowWaveformActivation")
        verify(waveformActivation,
               "the standalone waveform column must expose double-click playback")
        mouseDoubleClickSequence(waveformLoader,
                                 waveformLoader.width / 2,
                                 waveformLoader.height / 2)
        tryCompare(PlaybackController, "currentTrackId",
                   LibraryModel.data(LibraryModel.index(playableIndex, 0),
                                     LibraryModel.TrackIdRole))
        list.destroy()
        PlaybackController.pause()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_track_favorite_and_rating_cells_receive_real_mouse_clicks() {
        var ids = nativeDropHelper.ensureSortableTracks()
        var rowIndex = LibraryModel.indexForTrackId(ids[0])
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        list.positionViewAtIndex(rowIndex, ListView.Center)
        wait(30)
        var row = list.itemAtIndex(rowIndex)
        verify(row)
        var favorite = findChild(row, "trackFavoriteCell")
        var rating = findChild(row, "trackRatingCell")
        verify(favorite && rating)
        var initialFavorite = LibraryModel.data(LibraryModel.index(rowIndex, 0),
                                                LibraryModel.FavoriteRole)
        mouseClick(favorite, favorite.width / 2, favorite.height / 2)
        tryVerify(function() {
            return LibraryModel.data(LibraryModel.index(rowIndex, 0),
                                     LibraryModel.FavoriteRole) === !initialFavorite
        })
        mouseClick(rating, rating.width * 0.1, rating.height / 2)
        tryVerify(function() {
            return LibraryModel.data(LibraryModel.index(rowIndex, 0),
                                     LibraryModel.RatingRole) === 1
        })
        list.destroy()
    }

    function test_selected_tracks_drag_into_playlist_with_real_mouse() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var ids = nativeDropHelper.ensureSortableTracks()
        compare(ids.length, 3)
        var playlistId = PlaylistModel.createPlaylist(
                    "QML drag target " + Date.now())
        verify(playlistId.length > 0)

        var navigation = sideNavigationComponent.createObject(
                    mainWindow.contentItem, { "x": 0, "y": 0 })
        var list = trackListComponent.createObject(
                    mainWindow.contentItem, { "x": 220, "y": 0 })
        verify(navigation && list)
        mainWindow.requestActivate()
        tryVerify(function() { return mainWindow.active }, 1000)

        var firstIndex = LibraryModel.indexForTrackId(ids[0])
        var secondIndex = LibraryModel.indexForTrackId(ids[1])
        list.positionViewAtIndex(firstIndex, ListView.Beginning)
        wait(30)
        var firstRow = list.itemAtIndex(firstIndex)
        var secondRow = list.itemAtIndex(secondIndex)
        verify(firstRow && secondRow)
        var firstArea = findChild(firstRow, "trackRowDragArea")
        var secondArea = findChild(secondRow, "trackRowDragArea")
        verify(firstArea && secondArea)
        mouseClick(firstArea, firstArea.width / 2, firstArea.height / 2)
        mouseClick(secondArea, secondArea.width / 2, secondArea.height / 2,
                   Qt.LeftButton, Qt.ControlModifier)
        compare(list.selectedTrackIds.length, 2)

        var target = findChild(navigation,
                               "playlistDropTarget-" + playlistId)
        verify(target, "custom playlists must expose a stable drop target")
        var targetPoint = target.mapToItem(firstArea,
                                           target.width / 2,
                                           target.height / 2)
        var proxy = findChild(list, "trackDragProxy")
        verify(proxy)
        mousePress(firstArea, firstArea.width / 2, firstArea.height / 2,
                   Qt.LeftButton)
        mouseMove(firstArea, firstArea.width / 2 - 12,
                  firstArea.height / 2, 20, Qt.LeftButton)
        tryVerify(function() { return proxy.Drag.active }, 500)
        compare(list.dragPreviewCreationCount, 1)
        compare(list.dragTrackIds.length, 2)
        var preview = findChild(list, "trackDragPreview")
        verify(preview, "drag start must lazily create one shared preview")
        compare(preview.opacity, 0.68)
        compare(preview.selectedCount, 2)
        verify(preview.windowOverlayHosted,
               "the drag preview must live in the window overlay, outside ListView clipping")
        var previewTitle = findChild(preview, "trackDragPreviewTitle")
        var previewCount = findChild(preview, "trackDragPreviewCount")
        verify(previewTitle && previewCount)
        compare(previewTitle.text, firstRow.title,
                "multi-selection preview must retain the dragged song title")
        verify(previewCount.text.indexOf("2") >= 0,
               "multi-selection count must be separate from the song title")
        var previewBefore = preview.mapToItem(mainWindow.contentItem, 0, 0)
        mouseMove(firstArea, targetPoint.x, targetPoint.y,
                  60, Qt.LeftButton)
        compare(list.dragPreviewCreationCount, 1)
        tryVerify(function() {
            var moved = preview.mapToItem(mainWindow.contentItem, 0, 0)
            return Math.abs(moved.x - previewBefore.x) > 20
                    || Math.abs(moved.y - previewBefore.y) > 20
        }, 500, "the window-overlay preview must follow the active pointer")
        tryVerify(function() { return target.containsDrag }, 500)
        var feedback = findChild(navigation,
                                 "playlistDropFeedback-" + playlistId)
        verify(feedback && feedback.visible,
               "a playlist must show a clear accepting hover state")
        mouseRelease(firstArea, targetPoint.x, targetPoint.y,
                     Qt.LeftButton)

        tryVerify(function() {
            return PlaylistModel.containsTrack(playlistId, ids[0])
        }, 500)
        verify(PlaylistModel.containsTrack(playlistId, ids[1]),
               "all Ctrl-selected rows must arrive in the target playlist")
        compare(target.acceptedAnimationCount, 1,
                "a successful playlist drop must play one lightweight load pulse")
        tryVerify(function() { return !findChild(list, "trackDragPreview") }, 500)
        PlaylistModel.removePlaylist(playlistId)
        list.destroy()
        navigation.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_embedded_library_actions_data() {
        return [{ tag: "single-window", theme: "single-window", navigation: "integratedLibraryNavigation" },
                { tag: "professional", theme: "rolling-player", navigation: "rollingLibraryNavigation" }]
    }

    function test_embedded_library_actions(data) {
        for (var warning = 0; !nativeDropHelper.supportsWindowsDropFiles() && warning < 4; ++warning)
            ignoreWarning(/This plugin does not support propagateSizeHints\(\)/)
        var previousTheme = SettingsController.windowLayoutTheme
        var previousShell = SettingsController.playerShellMode
        var previousExpanded = LibraryNavigationModel.data(
                    LibraryNavigationModel.index(0, 0), LibraryNavigationModel.ExpandedRole)
        var filter = findChild(mainWindow, "filterModel")
        var createdId = ""
        SettingsController.playerShellMode = 1
        SettingsController.windowLayoutTheme = data.theme
        try {
            var navigation = null
            tryVerify(function() {
                navigation = findChild(mainWindow, data.navigation)
                return navigation && navigation.visible
            }, 1500)
            LibraryNavigationModel.setExpanded("library:all", false)
            wait(0)
            function action(nodeName, actionName) {
                var node = findChild(navigation, nodeName)
                verify(node && node.visible)
                mouseClick(node, node.width * 0.75, node.height / 2, Qt.RightButton)
                var menu = findChild(navigation, "playlistContextMenu")
                tryCompare(menu, "opened", true)
                var item = findChild(menu, actionName)
                verify(item && item.enabled)
                mouseClick(item, item.width / 2, item.height / 2)
            }
            function clickDialogButton(dialog, standardButton) {
                tryCompare(dialog, "opened", true)
                verify(waitForPolish(mainWindow))
                var button = dialog.standardButton(standardButton)
                verify(button)
                mouseClick(button, button.width / 2, button.height / 2)
            }
            action("navigationNode-library:all", "playlistMenuCreate")
            var create = findChild(mainWindow, "createPlaylistDialog")
            verify(create, "the active embedded host must own a create dialog")
            tryCompare(create, "visible", true)
            compare(create.parent, mainWindow.contentItem,
                    "playlist popups must be anchored to the visible main window")
            var name = "Embedded " + data.tag + " " + Date.now()
            findChild(create, "createPlaylistField").text = name
            clickDialogButton(create, Dialog.Ok)
            createdId = PlaylistModel.idAt(PlaylistModel.count - 1)
            compare(PlaylistModel.nameForId(createdId), name)
            compare(filter.category, createdId)
            wait(0)
            var nodeName = "playlistCategory-" + createdId
            tryVerify(function() { var node = findChild(navigation, nodeName); return node && node.visible })
            action(nodeName, "playlistMenuRename")
            var rename = findChild(mainWindow, "renamePlaylistDialog")
            tryCompare(rename, "visible", true)
            findChild(rename, "renamePlaylistField").text = name + " renamed"
            clickDialogButton(rename, Dialog.Ok)
            compare(PlaylistModel.nameForId(createdId), name + " renamed")
            action(nodeName, "playlistMenuExport")
            var exportOptions = findChild(mainWindow, "exportOptionsDialog")
            tryCompare(exportOptions, "visible", true)
            compare(findChild(mainWindow, "embeddedLibraryActions").exportPlaylistId, createdId)
            var exportAccepted = signalSpyComponent.createObject(testCase, {
                "target": exportOptions, "signalName": "accepted"
            })
            clickDialogButton(exportOptions, Dialog.Cancel)
            compare(exportAccepted.count, 0, "cancel must not launch the OS export picker")
            exportAccepted.destroy()
            action(nodeName, "playlistMenuDelete")
            var remove = findChild(mainWindow, "removePlaylistDialog")
            tryCompare(remove, "visible", true)
            clickDialogButton(remove, Dialog.Yes)
            compare(PlaylistModel.nameForId(createdId), "")
            compare(filter.category, "all")
            createdId = ""
        } finally {
            if (createdId) PlaylistModel.removePlaylist(createdId)
            LibraryNavigationModel.setExpanded("library:all", previousExpanded)
            filter.category = "all"
            SettingsController.windowLayoutTheme = previousTheme
            SettingsController.playerShellMode = previousShell
        }
    }

    function test_embedded_resource_navigation_clears_previous_category_data() {
        return test_embedded_library_actions_data()
    }

    function test_embedded_resource_navigation_clears_previous_category(data) {
        for (var warning = 0; !nativeDropHelper.supportsWindowsDropFiles() && warning < 4; ++warning)
            ignoreWarning(/This plugin does not support propagateSizeHints\(\)/)
        var previousTheme = SettingsController.windowLayoutTheme
        var previousExpanded = LibraryNavigationModel.data(
                    LibraryNavigationModel.index(0, 0), LibraryNavigationModel.ExpandedRole)
        var filter = findChild(mainWindow, "filterModel")
        var folderUrl = nativeDropHelper.createDropDirectory()
        var folderPath = ResourceFolderController.classifyDropUrl(folderUrl).path
        verify(ResourceFolderController.addMonitoredFolder(folderPath))
        var playlistId = PlaylistModel.createPlaylist("Resource route " + Date.now())
        try {
            SettingsController.windowLayoutTheme = data.theme
            var navigation = null
            tryVerify(function() {
                navigation = findChild(mainWindow, data.navigation)
                return navigation && navigation.visible
            }, 1500)
            LibraryNavigationModel.setExpanded("library:all", false)
            wait(0)
            var nodeId = ""
            for (var row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
                var index = LibraryNavigationModel.index(row, 0)
                if (String(LibraryNavigationModel.data(index, LibraryNavigationModel.ResourceFolderRole)) === folderPath) {
                    nodeId = LibraryNavigationModel.data(index, LibraryNavigationModel.NodeIdRole)
                    break
                }
            }
            verify(nodeId)
            navigation.revealNode(nodeId)
            wait(0)
            var categories = [playlistId, "favorites"]
            for (var i = 0; i < categories.length; ++i) {
                filter.category = categories[i]
                filter.resourceFolder = ""
                var node = findChild(navigation, "navigationNode-" + nodeId)
                verify(node && node.visible)
                mouseClick(node, node.width * 0.75, node.height / 2)
                tryCompare(filter, "resourceFolder", folderPath)
                compare(filter.category, "all", "resource clicks must replace, not intersect the preceding playlist/category")
                compare(filter.tagKey, "")
            }
        } finally {
            filter.category = "all"
            filter.resourceFolder = ""
            PlaylistModel.removePlaylist(playlistId)
            ResourceFolderController.removeMonitoredFolder(folderPath)
            LibraryNavigationModel.setExpanded("library:all", previousExpanded)
            SettingsController.windowLayoutTheme = previousTheme
        }
    }

    function test_embedded_import_survives_shell_change() {
        for (var warning = 0; !nativeDropHelper.supportsWindowsDropFiles() && warning < 4; ++warning)
            ignoreWarning(/This plugin does not support propagateSizeHints\(\)/)
        var previousTheme = SettingsController.windowLayoutTheme
        var filter = findChild(mainWindow, "filterModel")
        var id = PlaylistModel.createPlaylist("Embedded import " + Date.now())
        var importedId = ""
        try {
            SettingsController.windowLayoutTheme = "single-window"
            var navigation = null
            tryVerify(function() {
                navigation = findChild(mainWindow, "integratedLibraryNavigation")
                return navigation && navigation.visible
            })
            var actions = findChild(mainWindow, "embeddedLibraryActions")
            verify(actions)
            // Exercise the real asynchronous importer and shell lifetime,
            // without programmatically cancelling an OS-modal file picker.
            // The native picker itself is outside this integration fixture.
            actions.importTargetPlaylistId = id
            var audio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
            verify(audio)
            verify(actions.importUrls([audio]))
            verify(ImportController.busy)
            SettingsController.windowLayoutTheme = "rolling-player"
            filter.category = "favorites"
            compare(findChild(mainWindow, "embeddedLibraryActions"), actions,
                    "replacing a shell must retain the import owner")
            tryCompare(actions, "importBatchActive", false, 5000)
            verify(ImportController.importedTrackIds.length > 0)
            importedId = ImportController.importedTrackIds[0]
            verify(PlaylistModel.containsTrack(id, importedId),
                   "shell replacement must not destroy the pending import's playlist target")
            compare(filter.category, "favorites", "completion must not override later navigation")
        } finally {
            PlaylistModel.removePlaylist(id)
            if (importedId) LibraryModel.removeTrack(importedId)
            filter.category = "all"
            SettingsController.windowLayoutTheme = previousTheme
        }
    }

    function test_playlist_context_actions_use_real_mouse_and_keep_playlist_id() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1200,
            "height": 620
        })
        verify(window && filterModel)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)
        var navigation = findChild(window, "referenceSideNavigation")
        var libraryNode = findChild(navigation, "navigationNode-library:all")
        verify(navigation && libraryNode)

        function createPlaylistThroughMenu(name) {
            mouseClick(libraryNode, libraryNode.width / 2,
                       libraryNode.height / 2, Qt.RightButton)
            var contextMenu = findChild(navigation, "playlistContextMenu")
            tryVerify(function() { return contextMenu.visible }, 500)
            var createAction = findChild(contextMenu, "playlistMenuCreate")
            mouseClick(createAction, createAction.width / 2,
                       createAction.height / 2)
            var dialog = findChild(window, "createPlaylistDialog")
            tryVerify(function() { return dialog.visible }, 500)
            var field = findChild(dialog, "createPlaylistField")
            field.text = name
            dialog.accept()
            var createdId = PlaylistModel.idAt(PlaylistModel.count - 1)
            compare(PlaylistModel.nameForId(createdId), name)
            return createdId
        }

        var playlistA = createPlaylistThroughMenu("Context A " + Date.now())
        libraryNode = findChild(navigation, "navigationNode-library:all")
        var playlistB = createPlaylistThroughMenu("Context B " + Date.now())
        verify(playlistA.length > 0 && playlistB.length > 0
               && playlistA !== playlistB)
        filterModel.category = playlistA

        var category = findChild(navigation, "playlistCategory-" + playlistB)
        verify(category, "custom playlist needs a stable context target")
        mouseClick(category, category.width / 2, category.height / 2,
                   Qt.RightButton)
        var menu = findChild(navigation, "playlistContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        var importAction = findChild(menu, "playlistMenuImport")
        verify(importAction && importAction.enabled)
        mouseClick(importAction, importAction.width / 2,
                   importAction.height / 2)
        compare(window.importTargetPlaylistId, playlistB,
                "the right-click target must be fixed before opening the dialog")
        var importDialog = null
        tryVerify(function() {
            importDialog = window.activeImportDialog
            return importDialog !== null
        }, 500)
        var copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio)
        var importFinished = signalSpyComponent.createObject(testCase, {
            "target": ImportController,
            "signalName": "finished"
        })
        verify(importFinished)
        window.importSelectedUrls([copiedAudio])
        tryCompare(importFinished, "count", 1, 5000)
        verify(ImportController.importedTrackIds.length > 0)
        var importedId = ImportController.importedTrackIds[0]
        tryVerify(function() {
            return PlaylistModel.containsTrack(playlistB, importedId)
        }, 500)
        verify(!PlaylistModel.containsTrack(playlistA, importedId))
        compare(filterModel.category, playlistB,
                "importing into a playlist must enter that playlist")
        window.enterCategory(playlistA, "playlist")
        importDialog.destroy()
        window.activeImportDialog = null
        window.importTargetPlaylistId = ""
        wait(0)

        category = findChild(navigation, "playlistCategory-" + playlistB)
        mouseClick(category, category.width / 2, category.height / 2,
                   Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        var renameAction = findChild(menu, "playlistMenuRename")
        verify(renameAction && renameAction.enabled)
        mouseClick(renameAction, renameAction.width / 2,
                   renameAction.height / 2)
        var renameDialog = findChild(window, "renamePlaylistDialog")
        tryVerify(function() { return renameDialog.visible }, 500)
        var renameField = findChild(renameDialog, "renamePlaylistField")
        renameField.text = "Renamed B " + Date.now()
        var renamed = renameField.text
        renameDialog.accept()
        compare(PlaylistModel.nameForId(playlistB), renamed)

        category = findChild(navigation, "playlistCategory-" + playlistB)
        mouseClick(category, category.width / 2, category.height / 2,
                   Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        var deleteAction = findChild(menu, "playlistMenuDelete")
        verify(deleteAction && deleteAction.enabled)
        mouseClick(deleteAction, deleteAction.width / 2,
                   deleteAction.height / 2)
        var removeDialog = findChild(window, "removePlaylistDialog")
        tryVerify(function() { return removeDialog.visible }, 500)
        removeDialog.accept()
        tryVerify(function() {
            return PlaylistModel.nameForId(playlistB).length === 0
        }, 500)
        compare(filterModel.category, playlistA,
                "deleting a non-active playlist must retain the active one")
        PlaylistModel.removePlaylist(playlistA)
        LibraryModel.removeTrack(importedId)
        importFinished.destroy()
        window.close()
        window.destroy()
        wait(0)
        mainWindow.requestActivate()
    }

    function test_playlist_async_import_keeps_its_original_target() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1200,
            "height": 620
        })
        verify(window && filterModel)
        var playlistA = PlaylistModel.createPlaylist(
                    "Async target A " + Date.now())
        var playlistB = PlaylistModel.createPlaylist(
                    "Async target B " + Date.now())
        verify(playlistA.length > 0 && playlistB.length > 0
               && playlistA !== playlistB)
        var copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio)
        var importFinished = signalSpyComponent.createObject(testCase, {
            "target": ImportController,
            "signalName": "finished"
        })
        verify(importFinished)

        window.openImportDialog(playlistB)
        var firstDialog = window.activeImportDialog
        verify(firstDialog)
        compare(window.importTargetPlaylistId, playlistB)
        window.importSelectedUrls([copiedAudio])
        verify(ImportController.busy,
               "the first batch must still be asynchronous during overlap")

        window.openImportDialog(playlistA)
        var possibleSecondDialog = window.activeImportDialog
        if (possibleSecondDialog !== firstDialog)
            possibleSecondDialog.reject()

        tryCompare(importFinished, "count", 1, 5000)
        verify(ImportController.importedTrackIds.length > 0)
        var importedId = ImportController.importedTrackIds[0]
        tryVerify(function() {
            return PlaylistModel.containsTrack(playlistB, importedId)
        }, 500)
        verify(!PlaylistModel.containsTrack(playlistA, importedId),
               "a later dialog must not steal the first batch")

        firstDialog.destroy()
        window.activeImportDialog = null
        PlaylistModel.removePlaylist(playlistA)
        PlaylistModel.removePlaylist(playlistB)
        LibraryModel.removeTrack(importedId)
        importFinished.destroy()
        window.close()
        window.destroy()
        wait(0)
        mainWindow.requestActivate()
    }

    function test_resource_drop_accepts_only_folders_while_audio_import_busy() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400,
            "height": 620
        })
        verify(window && filterModel)
        filterModel.category = "all"
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)
        var navigation = findChild(window, "referenceSideNavigation")
        var navigationList = findChild(navigation, "libraryNavigationList")
        verify(navigation && navigationList)
        navigationList.positionViewAtEnd()
        wait(0)
        var dropTarget = findChild(navigation, "resourceFolderDropTarget")
        verify(dropTarget)

        var firstAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        var rejectedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        var mixedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        var busyFolder = nativeDropHelper.createDropDirectory()
        verify(firstAudio && rejectedAudio && mixedAudio && busyFolder)
        var initialFolderCount = ResourceFolderController.monitoredFolders.length
        var initialLibraryCount = LibraryModel.count
        var importFinished = signalSpyComponent.createObject(testCase, {
            "target": ImportController,
            "signalName": "finished"
        })
        verify(importFinished)

        verify(window.beginImport([firstAudio]))
        verify(ImportController.busy)
        var pureAudioAccepted = nativeDropHelper.sendUrls(
                    dropTarget, [rejectedAudio])
        verify(ImportController.busy)
        var mixedAccepted = nativeDropHelper.sendUrls(
                    dropTarget, [mixedAudio, busyFolder])
        compare(pureAudioAccepted, false,
                "a busy audio-only drop must remain unaccepted")
        compare(mixedAccepted, true,
                "a mixed resource drop must accept its directory")
        compare(ResourceFolderController.monitoredFolders.length,
                initialFolderCount + 1,
                "audio import state must not block resource directories")

        tryCompare(importFinished, "count", 1, 5000)
        compare(LibraryModel.count, initialLibraryCount + 1,
                "busy drops must not queue or import audio silently")
        var firstImportedId = ImportController.importedTrackIds[0]
        verify(firstImportedId)

        compare(nativeDropHelper.sendUrls(
                    dropTarget, [rejectedAudio, mixedAudio]), true,
                "resource audio must be accepted after imports finish")
        tryCompare(importFinished, "count", 2, 5000)
        compare(LibraryModel.count, initialLibraryCount + 3)
        var laterImportedIds = ImportController.importedTrackIds.slice(0)
        compare(laterImportedIds.length, 2)

        var folderPath = nativeDropHelper.localFilePath(busyFolder)
        folderPath = folderPath.replace(/\\/g, "/")
        verify(ResourceFolderController.removeMonitoredFolder(folderPath))
        LibraryModel.removeTrack(firstImportedId)
        for (var laterId of laterImportedIds)
            LibraryModel.removeTrack(laterId)
        importFinished.destroy()
        window.close()
        window.destroy()
        wait(0)
        mainWindow.requestActivate()
    }

    function test_list_drop_rejects_unsupported_files_clearly() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1200,
            "height": 620
        })
        verify(window)
        var unsupported = nativeDropHelper.createNonAudioDropFile()
        verify(unsupported)
        compare(window.handleListDropUrls([unsupported]), false,
                "an unsupported drop must not report apparent success")
        verify(!ImportController.busy)
        window.destroy()
    }

    function test_playlist_and_resource_navigation_states_are_exclusive() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400,
            "height": 620
        })
        verify(window && filterModel)
        var playlistId = PlaylistModel.createPlaylist(
                    "Transition target " + Date.now())
        verify(playlistId)

        filterModel.tagKey = "stale-tag"
        TagModel.selectedKey = "stale-tag"
        filterModel.resourceFolder = "C:/stale/resource"
        window.enterCategory(playlistId, "playlist")
        compare(filterModel.category, playlistId)
        compare(filterModel.tagKey, "")
        compare(TagModel.selectedKey, "")
        compare(filterModel.resourceFolder, "")

        var rootUrl = nativeDropHelper.createDropDirectory()
        var rootPath = ResourceFolderController.classifyDropUrl(rootUrl).path
        verify(rootPath)
        window.enterResource("resourceRoot", rootPath)
        compare(filterModel.category, "all")
        compare(filterModel.tagKey, "")
        compare(filterModel.resourceFolder, rootPath)

        window.handleResourceFolderRemoved(rootPath)
        compare(filterModel.category, "all")
        compare(filterModel.resourceFolder, "")
        var navigation = findChild(window, "referenceSideNavigation")
        compare(navigation.activeNodeType, "library")

        PlaylistModel.removePlaylist(playlistId)
        window.destroy()
        wait(0)
        mainWindow.requestActivate()
    }

    function test_create_and_import_playlist_clear_old_tag_resource_filters() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400,
            "height": 620
        })
        verify(window && filterModel)
        filterModel.category = "all"
        filterModel.tagKey = "stale-tag"
        TagModel.selectedKey = "stale-tag"
        filterModel.resourceFolder = "C:/stale/resource"

        var createDialog = findChild(window, "createPlaylistDialog")
        var createField = findChild(createDialog, "createPlaylistField")
        createDialog.open()
        createField.text = "Created transition " + Date.now()
        createDialog.accept()
        var createdId = filterModel.category
        verify(createdId !== "all")
        compare(filterModel.tagKey, "")
        compare(TagModel.selectedKey, "")
        compare(filterModel.resourceFolder, "")

        filterModel.resourceFolder = "C:/another/stale/resource"
        filterModel.tagKey = "stale-again"
        var importId = PlaylistModel.createPlaylist(
                    "Import transition " + Date.now())
        verify(window.openImportDialog(importId))
        compare(filterModel.category, importId)
        compare(filterModel.tagKey, "")
        compare(filterModel.resourceFolder, "")
        window.activeImportDialog.reject()
        window.activeImportDialog.destroy()
        window.activeImportDialog = null

        PlaylistModel.removePlaylist(createdId)
        PlaylistModel.removePlaylist(importId)
        window.destroy()
        wait(0)
        mainWindow.requestActivate()
    }

    function test_new_playlist_expands_collapsed_library_navigation() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400,
            "height": 620
        })
        verify(window && filterModel)
        var navigation = findChild(window, "referenceSideNavigation")
        verify(navigation)
        var createdId = ""
        try {
            verify(LibraryNavigationModel.setExpanded("library:all", false))
            var libraryIndex = -1
            for (var row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
                var index = LibraryNavigationModel.index(row, 0)
                if (LibraryNavigationModel.data(
                            index, LibraryNavigationModel.NodeIdRole)
                        === "library:all") {
                    libraryIndex = row
                    break
                }
            }
            verify(libraryIndex >= 0)
            compare(LibraryNavigationModel.data(
                        LibraryNavigationModel.index(libraryIndex, 0),
                        LibraryNavigationModel.ExpandedRole), false)

            var dialog = findChild(window, "createPlaylistDialog")
            var field = findChild(dialog, "createPlaylistField")
            verify(dialog && field)
            dialog.open()
            field.text = "Visible after create " + Date.now()
            dialog.accept()
            createdId = filterModel.category
            verify(createdId && createdId !== "all")

            tryVerify(function() {
                return findChild(navigation,
                                 "playlistCategory-" + createdId) !== null
            }, 500, "creating a playlist must reveal its selected navigation row")
            libraryIndex = -1
            for (row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
                index = LibraryNavigationModel.index(row, 0)
                if (LibraryNavigationModel.data(
                            index, LibraryNavigationModel.NodeIdRole)
                        === "library:all") {
                    libraryIndex = row
                    break
                }
            }
            verify(libraryIndex >= 0)
            compare(LibraryNavigationModel.data(
                        LibraryNavigationModel.index(libraryIndex, 0),
                        LibraryNavigationModel.ExpandedRole), true)
        } finally {
            if (createdId)
                PlaylistModel.removePlaylist(createdId)
            LibraryNavigationModel.setExpanded("library:all", true)
            filterModel.category = "all"
            window.destroy()
            wait(0)
            mainWindow.requestActivate()
        }
    }

    function test_resource_row_click_survives_navigation_model_reset() {
        var filterModel = findChild(mainWindow, "filterModel")
        var folderUrl = nativeDropHelper.createDropDirectory()
        var folderPath = ResourceFolderController.classifyDropUrl(folderUrl).path
        verify(folderPath)
        verify(ResourceFolderController.addMonitoredFolder(folderPath))
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400,
            "height": 620
        })
        verify(window && filterModel)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)
        var navigation = findChild(window, "referenceSideNavigation")
        verify(navigation)
        var secondFolderPath = ""
        var firstFolderRegistered = true
        try {
            LibraryNavigationModel.setExpanded("library:all", false)
            filterModel.category = "all"
            filterModel.resourceFolder = ""

            var resourceNode = null
            for (var row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
                var index = LibraryNavigationModel.index(row, 0)
                var candidatePath = String(LibraryNavigationModel.data(
                    index, LibraryNavigationModel.ResourceFolderRole))
                if (candidatePath.replace(/\\/g, "/").toLowerCase()
                        === folderPath.replace(/\\/g, "/").toLowerCase()) {
                    resourceNode = findChild(navigation, "navigationNode-"
                        + LibraryNavigationModel.data(
                            index, LibraryNavigationModel.NodeIdRole))
                    break
                }
            }
            verify(resourceNode)
            var point = resourceNode.mapToItem(
                        navigation, resourceNode.width * 0.75,
                        resourceNode.height / 2)
            mousePress(navigation, point.x, point.y, Qt.LeftButton)

            // Adding a later root rebuilds the navigation model while keeping
            // this first resource row at the same visual position.  A row
            // handler owned by the recycled delegate loses this click.
            var secondFolderUrl = nativeDropHelper.createDropDirectory()
            secondFolderPath = ResourceFolderController.classifyDropUrl(
                        secondFolderUrl).path
            verify(secondFolderPath)
            verify(ResourceFolderController.addMonitoredFolder(secondFolderPath))
            wait(0)
            mouseRelease(navigation, point.x, point.y, Qt.LeftButton)

            tryCompare(filterModel, "resourceFolder", folderPath, 500,
                       "a model refresh between press/release must not swallow the resource click")
            compare(navigation.activeNodeType, "resourceRoot")

            filterModel.resourceFolder = ""
            resourceNode = null
            for (row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
                index = LibraryNavigationModel.index(row, 0)
                candidatePath = String(LibraryNavigationModel.data(
                    index, LibraryNavigationModel.ResourceFolderRole))
                if (candidatePath.replace(/\\/g, "/").toLowerCase()
                        === folderPath.replace(/\\/g, "/").toLowerCase()) {
                    resourceNode = findChild(navigation, "navigationNode-"
                        + LibraryNavigationModel.data(
                            index, LibraryNavigationModel.NodeIdRole))
                    break
                }
            }
            verify(resourceNode)
            point = resourceNode.mapToItem(
                        navigation, resourceNode.width * 0.75,
                        resourceNode.height / 2)
            mousePress(navigation, point.x, point.y, Qt.LeftButton)
            verify(ResourceFolderController.removeMonitoredFolder(folderPath))
            firstFolderRegistered = false
            wait(0)
            mouseRelease(navigation, point.x, point.y, Qt.LeftButton)
            compare(filterModel.resourceFolder, "",
                    "removing the pressed row must not select a different row that moves under the release point")
        } finally {
            if (secondFolderPath)
                ResourceFolderController.removeMonitoredFolder(secondFolderPath)
            if (firstFolderRegistered)
                ResourceFolderController.removeMonitoredFolder(folderPath)
            LibraryNavigationModel.setExpanded("library:all", true)
            filterModel.category = "all"
            filterModel.resourceFolder = ""
            window.destroy()
            wait(0)
            mainWindow.requestActivate()
        }
    }

    function test_resource_expand_arrow_does_not_change_filter_selection() {
        var filterModel = findChild(mainWindow, "filterModel")
        var rootUrl = nativeDropHelper.createNestedDropDirectory()
        var rootPath = ResourceFolderController.classifyDropUrl(rootUrl).path
        verify(rootPath)
        verify(ResourceFolderController.addMonitoredFolder(rootPath))
        ResourceFolderController.rescan()
        var childPath = rootPath + "/child"
        tryVerify(function() {
            return !ResourceFolderController.scanning
                    && ResourceFolderController.resourceDirectories.some(
                        function(path) {
                            return path.replace(/\\/g, "/").toLowerCase()
                                    === childPath.toLowerCase()
                        })
        }, 3000)

        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400,
            "height": 620
        })
        verify(window && filterModel)
        var navigation = findChild(window, "referenceSideNavigation")
        verify(navigation)
        try {
            LibraryNavigationModel.setExpanded("library:all", false)
            wait(0)
            window.enterCategory("favorites", "favorites")
            compare(filterModel.category, "favorites")
            compare(filterModel.resourceFolder, "")

            var resourceNode = null
            var resourceIndex = -1
            for (var row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
                var index = LibraryNavigationModel.index(row, 0)
                var candidatePath = String(LibraryNavigationModel.data(
                    index, LibraryNavigationModel.ResourceFolderRole))
                if (candidatePath.replace(/\\/g, "/").toLowerCase()
                        === rootPath.replace(/\\/g, "/").toLowerCase()) {
                    resourceIndex = row
                    resourceNode = findChild(navigation, "navigationNode-"
                        + LibraryNavigationModel.data(
                            index, LibraryNavigationModel.NodeIdRole))
                    break
                }
            }
            verify(resourceIndex >= 0 && resourceNode)
            var modelIndex = LibraryNavigationModel.index(resourceIndex, 0)
            compare(LibraryNavigationModel.data(
                        modelIndex, LibraryNavigationModel.HasChildrenRole), true)
            compare(LibraryNavigationModel.data(
                        modelIndex, LibraryNavigationModel.ExpandedRole), false)
            var expandButton = findChild(resourceNode, "navigationExpandButton")
            verify(expandButton && expandButton.visible)

            mouseClick(expandButton, expandButton.width / 2,
                       expandButton.height / 2, Qt.LeftButton)
            tryVerify(function() {
                var current = LibraryNavigationModel.index(resourceIndex, 0)
                return LibraryNavigationModel.data(
                            current, LibraryNavigationModel.ExpandedRole) === true
            }, 500)
            compare(filterModel.category, "favorites")
            compare(filterModel.resourceFolder, "",
                    "the tree arrow must not also select the resource root")
            compare(navigation.activeNodeType, "favorites")
        } finally {
            window.destroy()
            ResourceFolderController.removeMonitoredFolder(rootPath)
            LibraryNavigationModel.setExpanded("library:all", true)
            filterModel.category = "all"
            filterModel.resourceFolder = ""
            wait(0)
            mainWindow.requestActivate()
        }
    }

    function test_z_delete_key_uses_current_view_semantics() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var ids = nativeDropHelper.ensureSortableTracks()
        compare(ids.length, 3)
        var playlistId = PlaylistModel.createPlaylist(
                    "Delete semantics " + Date.now())
        verify(playlistId.length > 0)
        compare(PlaylistModel.addTracks(playlistId, [ids[0]]), 1)

        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        mainWindow.requestActivate()
        tryVerify(function() { return mainWindow.active }, 1000)
        list.forceActiveFocus()
        wait(30)

        list.selectedCategory = playlistId
        var firstIndex = LibraryModel.indexForTrackId(ids[0])
        list.positionViewAtIndex(firstIndex, ListView.Beginning)
        wait(20)
        var firstArea = findChild(list.itemAtIndex(firstIndex),
                                  "trackRowDragArea")
        verify(firstArea)
        mouseClick(firstArea, firstArea.width / 2, firstArea.height / 2)
        keyClick(Qt.Key_Delete)
        tryVerify(function() {
            return !PlaylistModel.containsTrack(playlistId, ids[0])
        }, 500)
        verify(LibraryModel.indexForTrackId(ids[0]) >= 0,
               "playlist Delete must not remove the library record")

        var secondIndex = LibraryModel.indexForTrackId(ids[1])
        verify(LibraryModel.setFavorite(secondIndex, true))
        list.selectedCategory = "favorites"
        list.positionViewAtIndex(secondIndex, ListView.Center)
        wait(20)
        var secondArea = findChild(list.itemAtIndex(secondIndex),
                                   "trackRowDragArea")
        verify(secondArea)
        mouseClick(secondArea, secondArea.width / 2, secondArea.height / 2)
        keyClick(Qt.Key_Delete)
        tryVerify(function() {
            var row = LibraryModel.indexForTrackId(ids[1])
            return row >= 0 && !LibraryModel.data(
                        LibraryModel.index(row, 0), LibraryModel.FavoriteRole)
        }, 500)

        verify(LibraryModel.setTags(ids[0], ["Focus", "Night"]))
        list.selectedCategory = "all"
        list.tagFilterActive = true
        list.activeTagKey = ""
        compare(list.canRemoveFromCurrentView, false,
                "an incomplete tag filter must never fall through to library deletion")
        list.activeTagKey = "focus"
        list.selectOnly(ids[0], LibraryModel.indexForTrackId(ids[0]))
        list.removeSelectedFromCurrentView()
        verify(LibraryModel.indexForTrackId(ids[0]) >= 0,
               "tag-filter removal must keep the library record")
        compare(LibraryModel.trackForId(ids[0]).tags, ["Night"])

        list.tagFilterActive = false
        list.activeTagKey = ""
        list.selectedCategory = "recentAdded"
        compare(list.canRemoveFromCurrentView, false,
                "read-only virtual views must disable list removal")

        var thirdIndex = LibraryModel.indexForTrackId(ids[2])
        list.selectedCategory = "all"
        list.positionViewAtIndex(thirdIndex, ListView.Center)
        wait(20)
        var thirdArea = findChild(list.itemAtIndex(thirdIndex),
                                  "trackRowDragArea")
        verify(thirdArea)
        mouseClick(thirdArea, thirdArea.width / 2, thirdArea.height / 2)
        keyClick(Qt.Key_Delete)
        tryVerify(function() {
            return LibraryModel.indexForTrackId(ids[2]) < 0
        }, 500)

        PlaylistModel.removePlaylist(playlistId)
        list.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_waveform_click_seeks_real_playback_controller() {
        SettingsController.waveformMode = 0
        var waveform = findChild(mainWindow, "mainWaveform")
        var seekSurface = findChild(mainWindow, "waveformInteractionSurface")
        verify(waveform, "main waveform should exist after importing audio")
        verify(seekSurface, "visible waveform surface should own seeking")

        var playablePath = nativeDropHelper.localFilePath(testAudioUrl)
        var playableIndex = -1
        for (var row = 0; row < LibraryModel.count; ++row) {
            var path = String(LibraryModel.data(LibraryModel.index(row, 0),
                                                LibraryModel.PathRole))
                    .replace(/\\/g, "/")
            if (path.toLowerCase() === playablePath.toLowerCase()) {
                playableIndex = row
                break
            }
        }
        verify(playableIndex >= 0)
        PlaybackController.playRow(playableIndex)
        tryVerify(function() {
            return PlaybackController.durationMs > 0 && waveform.width > 0
                    && waveform.duration === PlaybackController.durationMs
        }, 5000)

        wait(120)
        var expected = waveform.timeForX(seekSurface.width * 0.75)
        PlaybackController.pause()
        mouseClick(seekSurface, seekSurface.width * 0.75,
                   seekSurface.height / 2)
        compare(PlaybackController.errorMessage, "",
                "waveform seek must not be rejected by the playback core")
        tryVerify(function() {
            return Math.abs(PlaybackController.positionMs - expected) < 150
        }, 1000, "seek mismatch: actual=" + PlaybackController.positionMs
                 + ", expected=" + expected
                 + ", width=" + seekSurface.width)
        var playedWaveform = findChild(mainWindow, "playedWaveform")
        verify(playedWaveform)
        compare(waveform.position, 0)
        compare(playedWaveform.position, waveform.duration)
        var playedClip = findChild(mainWindow, "waveformPlayedClip")
        verify(playedClip,
               "played waveform colour must be clipped at the exact playback pixel")
        tryVerify(function() {
            return Math.abs(playedClip.width - waveform.waveformCursorX) <= 0.5
        }, 500, "played colour boundary must use the C++ waveform mapper")
        var playbackGuide = findChild(mainWindow, "waveformPlaybackGuide")
        verify(playbackGuide, "the precise playback cursor must be present")
        compare(playbackGuide.width, 1)
        compare(playbackGuide.color.toString(), "#8b5cf6")
        verify(Math.abs(playbackGuide.x - waveform.waveformCursorX) <= 0.5)
        var originalWidth = mainWindow.width
        mainWindow.width = Math.max(mainWindow.minimumWidth, originalWidth - 160)
        wait(30)
        verify(Math.abs(playedClip.width - waveform.waveformCursorX) <= 0.5,
               "played colour boundary must remain authoritative after resizing")
        mainWindow.width = originalWidth
    }

    function test_waveform_time_axis_prefers_exact_decoded_waveform_duration() {
        var pane = findChild(mainWindow, "playerPane")
        verify(pane)
        verify(PlaybackController.durationMs > 0)
        // The complete PCM analysis is authoritative for waveform pixels.
        // Container duration may include encoder padding and stretch beats.
        pane.waveformDurationMs = PlaybackController.durationMs * 1.25
        compare(pane.effectiveDurationMs, pane.waveformDurationMs)
        verify(findChild(mainWindow, "waveformPlaybackGuide"))
    }

    function test_waveform_stays_aligned_at_required_window_sizes() {
        var waveform = findChild(mainWindow, "mainWaveform")
        var playedClip = findChild(mainWindow, "waveformPlayedClip")
        var playbackGuide = findChild(mainWindow, "waveformPlaybackGuide")
        verify(waveform && playedClip && playbackGuide)
        var originalWidth = mainWindow.width
        var originalHeight = mainWindow.height
        var sizes = [[800, 500], [1920, 1080], [3840, 2160]]
        for (var i = 0; i < sizes.length; ++i) {
            mainWindow.width = sizes[i][0]
            mainWindow.height = sizes[i][1]
            wait(30)
            verify(Math.abs(playedClip.width - waveform.waveformCursorX) <= 0.5)
            verify(Math.abs(playbackGuide.x - waveform.waveformCursorX) <= 0.5)
            compare(waveform.pixelForTime(waveform.duration), waveform.renderWidth)
        }
        mainWindow.width = originalWidth
        mainWindow.height = originalHeight
    }

    function test_waveform_mode_button_cycles_the_live_setting() {
        var button = findChild(mainWindow, "waveformModeButton")
        verify(button)
        verify(button.icon.source.toString().endsWith("/waveform-switch.svg"),
               "waveform switch must use the user-provided shared waveform icon")
        var previousMode = SettingsController.waveformMode
        SettingsController.waveformMode = 0
        button.clicked()
        tryCompare(SettingsController, "waveformMode", 3)
        SettingsController.waveformMode = previousMode
    }

    function test_playback_modes_use_distinct_system_tinted_line_icons() {
        var button = findChild(mainWindow, "modeButton")
        verify(button)
        var previousMode = PlaybackController.mode
        var cases = [
            { mode: PlaybackController.Sequential, icon: "/play-order-line.svg" },
            { mode: PlaybackController.Shuffle, icon: "/shuffle-arrows-line.svg" },
            { mode: PlaybackController.RepeatOne, icon: "/repeat-one-line-alt.svg" },
            { mode: PlaybackController.RepeatAll, icon: "/repeat-list-line.svg" }
        ]

        for (var index = 0; index < cases.length; ++index) {
            PlaybackController.setMode(cases[index].mode)
            tryCompare(PlaybackController, "mode", cases[index].mode)
            verify(button.icon.source.toString().endsWith(cases[index].icon),
                   "each playback mode must use its own line icon")
            compare(button.icon.color.toString(), Theme.iconPrimary.toString())
        }
        PlaybackController.setMode(previousMode)
    }

    function test_player_controls_use_reference_scale_and_green_hover_guide() {
        var previous = findChild(mainWindow, "previousButton")
        var next = findChild(mainWindow, "nextButton")
        var mode = findChild(mainWindow, "modeButton")
        var play = findChild(mainWindow, "playPauseButton")
        var hoverGuide = findChild(mainWindow, "waveformHoverGuide")
        verify(previous)
        verify(next)
        verify(mode)
        verify(play)
        verify(hoverGuide)
        verify(!findChild(mainWindow, "waveformProgressFeather"))
        verify(findChild(mainWindow, "waveformPlaybackGuide"))
        compare(previous.icon.width, 24)
        compare(next.icon.width, 24)
        compare(mode.icon.width, 20)
        compare(play.width, 52)
        compare(play.height, 52)
        compare(play.icon.width, 24)
        compare(hoverGuide.color.toString(), "#54ff84")
        verify(findChild(mainWindow, "mainWaveform"),
               "the C++ waveform item must own hover and seek input")
    }

    function test_main_waveform_preserves_first_peak_without_an_opaque_edge_mask() {
        var sharedView = findChild(mainWindow, "mainFullTrackWaveform")
        var waveform = findChild(mainWindow, "mainWaveform")
        var playedClip = findChild(mainWindow, "waveformPlayedClip")
        var playedWaveform = findChild(mainWindow, "playedWaveform")
        var mask = findChild(mainWindow, "waveformLeftEdgeMask")
        verify(sharedView && waveform && playedClip && playedWaveform)
        compare(waveform.width, sharedView.width)
        compare(playedWaveform.width, sharedView.width)
        verify(!mask || !mask.visible || mask.opacity === 0,
               "the first peak must not be covered by a black/white vertical strip")
        compare(playedWaveform.progressColor.toString(),
                waveform.progressColor.toString(),
                "the edge fix must preserve the configured played-progress colour")
        compare(playedWaveform.position, playedWaveform.duration,
                "the played overlay must remain a fully coloured waveform pass")
        compare(playedClip.x, 0,
                "the played overlay must still start at the waveform origin")
    }

    function test_solid_waveform_uses_the_shared_gray_unplayed_base() {
        var previousMode = SettingsController.waveformMode
        SettingsController.waveformMode = 0
        var waveform = findChild(mainWindow, "mainWaveform")
        verify(waveform)
        tryCompare(waveform, "visualMode", 0)
        compare(waveform.baseColor.toString(), "#9098a6")
        compare(waveform.baseColor.toString(),
                SettingsController.waveformSolidBaseColor.toString())
        SettingsController.waveformMode = previousMode
    }

    function test_waveform_hover_surface_covers_played_and_unplayed_regions() {
        var previousPreview = SettingsController.waveformHoverTimePreview
        SettingsController.waveformHoverTimePreview = true
        var surface = findChild(mainWindow, "mainWaveform")
        var interactionSurface = findChild(mainWindow, "waveformInteractionSurface")
        var guide = findChild(mainWindow, "waveformHoverGuide")
        var capsule = findChild(mainWindow, "waveformHoverTimeCapsule")
        verify(surface)
        verify(interactionSurface)
        verify(guide)
        verify(capsule)
        verify(surface.enabled)
        compare(surface.timeForX(surface.width * 0.15),
                Math.round(surface.duration * 0.15))
        compare(surface.timeForX(surface.width * 0.85),
                Math.round(surface.duration * 0.85))
        compare(surface.pixelForTime(surface.duration), surface.width)
        interactionSurface.updatePreviewAt(interactionSurface.width * 0.15)
        tryVerify(function() { return guide.visible }, 300)
        tryVerify(function() { return capsule.visible }, 300)
        tryCompare(guide, "x",
                   surface.pixelForTime(Math.round(surface.duration * 0.15)))
        interactionSurface.updatePreviewAt(interactionSurface.width * 0.85)
        tryVerify(function() { return guide.visible }, 300)
        tryCompare(guide, "x",
                   surface.pixelForTime(Math.round(surface.duration * 0.85)))
        SettingsController.waveformHoverTimePreview = previousPreview
    }

    function test_waveform_playback_guide_can_be_hidden_without_hiding_progress_color() {
        var previous = SettingsController.waveformPlaybackGuide
        var guide = findChild(mainWindow, "waveformPlaybackGuide")
        var playedClip = findChild(mainWindow, "waveformPlayedClip")
        var waveform = findChild(mainWindow, "mainWaveform")
        verify(guide && playedClip && waveform)
        SettingsController.waveformPlaybackGuide = true
        tryCompare(guide, "visible", true)
        compare(guide.color.toString(), "#8b5cf6")
        SettingsController.waveformPlaybackGuide = false
        tryCompare(guide, "visible", false)
        verify(playedClip.visible,
               "disabling the guide must retain the played-color region")
        if (SettingsController.waveformMode === 3)
            compare(waveform.position, 0)
        SettingsController.waveformPlaybackGuide = previous
    }

    function test_play_button_uses_system_solid_style_without_rgb_runtime() {
        var play = findChild(mainWindow, "playPauseButton")
        var ring = findChild(mainWindow, "playButtonRgbRing")
        var glow = findChild(mainWindow, "playButtonRgbGlow")
        var body = findChild(mainWindow, "playButtonBody")
        verify(play)
        verify(!ring)
        verify(!glow)
        verify(body)
        compare(play.width, 52)
        compare(play.height, 52)
        compare(body.color.toString(), Theme.panel.toString())
        compare(body.border.width, 3)
        compare(body.border.color.toString(),
                (PlaybackController.state === PlaybackController.Playing
                 ? Theme.playRingPlaying : Theme.playRingPaused).toString())
        compare(typeof SettingsController.playButtonRgbGlow, "undefined")
    }

    function test_play_button_tracks_real_playback_state() {
        mainWindow.importFiles([testAudioUrl])
        tryVerify(function() { return !ImportController.busy }, 5000)
        var playablePath = nativeDropHelper.localFilePath(testAudioUrl)
        var playableIndex = -1
        for (var row = 0; row < LibraryModel.count; ++row) {
            var path = String(LibraryModel.data(LibraryModel.index(row, 0),
                                                LibraryModel.PathRole))
                    .replace(/\\/g, "/")
            if (path.toLowerCase() === playablePath.toLowerCase()) {
                playableIndex = row
                break
            }
        }
        verify(playableIndex >= 0)
        var play = findChild(mainWindow, "playPauseButton")
        var body = findChild(mainWindow, "playButtonBody")
        verify(play && body)

        PlaybackController.playRow(playableIndex)
        tryCompare(PlaybackController, "state", PlaybackController.Playing,
                   5000)
        verify(play.icon.source.toString().endsWith("/pause-fill.svg"))
        compare(play.Accessible.name, qsTr("暂停"))
        compare(body.border.color.toString(),
                Theme.playRingPlaying.toString())

        PlaybackController.pause()
        tryVerify(function() {
            return PlaybackController.state !== PlaybackController.Playing
        }, 1000)
        verify(play.icon.source.toString().endsWith("/play-fill.svg"))
        compare(play.Accessible.name, qsTr("播放"))
        compare(body.border.color.toString(),
                Theme.playRingPaused.toString())
    }

    function test_volume_control_uses_compact_white_handle_and_percentage() {
        var mute = findChild(mainWindow, "muteButton")
        var slider = findChild(mainWindow, "volumeSlider")
        var percent = findChild(mainWindow, "volumePercentLabel")
        verify(mute)
        verify(slider)
        verify(percent)
        compare(percent.horizontalAlignment, Text.AlignLeft)
        compare(slider.rightPadding, 0)
        compare(slider.leftPadding, 0)
        compare(percent.anchors.leftMargin, percent.width > 0 ? 4 : 0)
        compare(mute.icon.color.toString(), Theme.iconPrimary.toString())
        compare(mute.icon.width, 20)
        verify(slider.handle.width <= 10)

        var previousVolume = PlaybackController.volume
        PlaybackController.setVolume(0.42)
        tryCompare(percent, "text", "42%")
        PlaybackController.setVolume(previousVolume)
    }

    function test_player_core_controls_do_not_shift_when_volume_expands() {
        if (LibraryModel.count === 0)
            nativeDropHelper.ensureSortableTracks()
        var controls = findChild(mainWindow, "playerControls")
        var core = findChild(mainWindow, "centerPlaybackControls")
        var play = findChild(mainWindow, "playPauseButton")
        var volume = findChild(mainWindow, "mainVolumeControl")
        verify(controls && core && play && volume)

        function verifyCoreCentered() {
            var controlsCenter = controls.mapToItem(
                        mainWindow.contentItem, controls.width / 2, 0).x
            var playCenter = play.mapToItem(
                        mainWindow.contentItem, play.width / 2, 0).x
            verify(Math.abs(playCenter - controlsCenter) <= 0.5,
                   "the play button must stay centered to the nearest pixel: "
                   + "delta=" + (playCenter - controlsCenter))
        }

        wait(300)
        volume.expandedForQa = false
        wait(260)
        verifyCoreCentered()
        var widthBefore = core.width
        var controlsCenterBefore = controls.mapToItem(
                    mainWindow.contentItem, controls.width / 2, 0).x
        var playCenterBefore = play.mapToItem(controls,
                                              play.width / 2,
                                              play.height / 2).x
        volume.expandedForQa = true
        wait(220)
        compare(core.width, widthBefore,
                "the right-side volume flyout must not enter core layout width")
        var controlsCenterAfter = controls.mapToItem(
                    mainWindow.contentItem, controls.width / 2, 0).x
        if (Math.round(controlsCenterAfter) === Math.round(controlsCenterBefore))
            compare(Math.round(play.mapToItem(controls,
                                              play.width / 2,
                                              play.height / 2).x),
                    Math.round(playCenterBefore),
                    "expanding volume must not move the core transport controls")
        verifyCoreCentered()
        verify(volume.x >= core.x + core.width,
               "the volume control must float to the right of the centered core")
        volume.expandedForQa = false
    }

    function test_classic_default_width_keeps_control_groups_separate() {
        if (LibraryModel.count === 0)
            nativeDropHelper.ensureSortableTracks()
        var previousWidth = mainWindow.width
        mainWindow.width = 960
        wait(50)

        var controls = findChild(mainWindow, "playerControls")
        var transport = findChild(controls, "centerPlaybackControls")
        var volume = findChild(controls, "mainVolumeControl")
        var rightActions = findChild(controls, "playerSecondaryActions")
        verify(controls && transport && volume && rightActions)
        verifyAscendingX(transport, [
            "equalizerButton", "waveformModeButton", "previousButton",
            "playPauseButton", "nextButton", "modeButton"
        ])
        verifyAscendingX(rightActions, [
            "themeModeButton", "immersiveActionButton", "miniPlayerButton"
        ])
        compare(findChild(rightActions, "themeModeButton").icon.width, 22)
        compare(findChild(rightActions, "animatedImmersiveIcon").width, 20)
        compare(findChild(rightActions, "animatedImmersiveIcon").height, 20)
        compare(findChild(rightActions, "miniPlayerButton").icon.width, 22)
        verify(findChild(rightActions, "themeModeButton").width >= 32)
        verify(findChild(rightActions, "immersiveActionButton").width >= 32)
        verify(findChild(rightActions, "miniPlayerButton").width >= 32)
        verify(findChild(controls, "audioToolsButton").mapToItem(
                   controls, findChild(controls, "audioToolsButton").width, 0).x
               <= transport.mapToItem(controls, 0, 0).x)
        verify(transport.mapToItem(controls, transport.width, 0).x
               <= findChild(controls, "lyricsActionButton").mapToItem(
                   controls, 0, 0).x)
        verify(findChild(controls, "lyricsActionButton").mapToItem(
                   controls, findChild(controls, "lyricsActionButton").width, 0).x
               <= volume.mapToItem(controls, 0, 0).x)
        verify(volume.mapToItem(controls, volume.width, 0).x
               <= rightActions.mapToItem(controls, 0, 0).x)
        compare(findChild(controls, "miniPlayerButton").visible, true)

        mainWindow.width = previousWidth
        wait(20)
    }

    function test_classic_intermediate_width_uses_safe_compact_boundary() {
        if (LibraryModel.count === 0)
            nativeDropHelper.ensureSortableTracks()
        var previousWidth = mainWindow.width
        mainWindow.width = 800
        wait(50)

        var controls = findChild(mainWindow, "playerControls")
        var transport = findChild(controls, "centerPlaybackControls")
        var volume = findChild(controls, "mainVolumeControl")
        var rightActions = findChild(controls, "playerSecondaryActions")
        verify(controls && transport && volume && rightActions)
        compare(controls.compactTransport, true)
        verify(findChild(controls, "audioToolsButton").visible)
        verify(findChild(controls, "lyricsActionButton").visible)
        verify(findChild(controls, "equalizerButton").visible)
        verify(findChild(controls, "waveformModeButton").visible)
        verify(findChild(controls, "themeModeButton").visible)
        verify(findChild(controls, "immersiveActionButton").visible)
        verify(findChild(controls, "miniPlayerButton").visible)
        verify(transport.mapToItem(controls, transport.width, 0).x
               <= volume.mapToItem(controls, 0, 0).x)
        verify(volume.mapToItem(controls, volume.width, 0).x
               <= controls.width)

        mainWindow.width = previousWidth
        wait(20)
    }

    function test_main_volume_flyout_retracts_after_pointer_leaves() {
        var control = findChild(mainWindow, "mainVolumeControl")
        var closeTimer = findChild(mainWindow, "mainVolumeCloseTimer")
        var slider = findChild(mainWindow, "volumeSlider")
        verify(control && closeTimer && slider)
        control.expandedForQa = true
        slider.forceActiveFocus()
        closeTimer.restart()
        compare(closeTimer.interval, 250)
        wait(320)
        tryVerify(function() { return !control.expandedForQa }, 200)
    }

    function test_spectrum_source_is_mirrored_before_responsive_rendering() {
        var pane = findChild(mainWindow, "playerPane")
        verify(pane)
        var shaped = pane.shapeSpectrum([1, 1, 1, 1])
        compare(shaped.length, 128)
        compare(shaped[0], shaped[shaped.length - 1])
        compare(shaped[31], shaped[shaped.length - 32])
    }

    function test_spectrum_uses_responsive_five_pixel_bottom_bars() {
        var waveform = findChild(mainWindow, "mainWaveform")
        verify(waveform)
        var previousMode = SettingsController.waveformMode
        SettingsController.waveformMode = 2
        tryCompare(waveform, "visualMode", 2)
        compare(waveform.lineWidth, 3)
        compare(waveform.spectrumBarCount, 128)
        compare(waveform.spectrumBarWidth, 5)
        compare(waveform.spectrumBarGap, 2)
        compare(waveform.spectrumMaxHeight, 96)
        fuzzyCompare(waveform.spectrumAttackSeconds, 0.02, 0.001)
        fuzzyCompare(waveform.spectrumDecaySeconds, 0.10, 0.001)
        fuzzyCompare(waveform.spectrumPeakFallSeconds, 0.75, 0.001)
        fuzzyCompare(waveform.amplitudeScale, 1.0, 0.001)
        var pane = findChild(mainWindow, "playerPane")
        var shaped = pane.shapeSpectrum([1, 1, 1, 1])
        compare(shaped.length, 128)
        SettingsController.waveformMode = previousMode
    }

    function test_search_filter_uses_editable_bpm_bounds_and_compact_modules() {
        var filter = searchFilterComponent.createObject(mainWindow.contentItem)
        verify(filter)
        compare(filter.implicitHeight, 32)
        compare(findChild(filter, "keywordModule").width, 184)
        compare(findChild(filter, "librarySearchField").placeholderText,
                "歌曲 · 艺术家 · 专辑 · 标签")
        verify(findChild(filter, "librarySearchIcon"))
        compare(findChild(filter, "bpmModule").width, 216)
        compare(findChild(filter, "keywordModule").border.color.toString(),
                Theme.controlSubtleBorder.toString())
        var bpmRange = findChild(filter, "bpmRange")
        compare(bpmRange.background.height, Theme.sliderTrackHeight)
        compare(bpmRange.first.handle.width, Theme.sliderHandleExtent)
        compare(bpmRange.second.handle.width, Theme.sliderHandleExtent)
        var clearButton = findChild(filter, "clearFiltersButton")
        var bpmModule = findChild(filter, "bpmModule")
        verify(clearButton && bpmModule)
        compare(clearButton.height, bpmModule.height)
        verify(clearButton.x > bpmModule.x + bpmModule.width,
               "clear must follow BPM")
        verify(clearButton.x < bpmModule.x + bpmModule.width + 30,
               "clear must remain beside BPM instead of at the far edge")
        bpmRange.first.value = 72
        bpmRange.second.value = 155
        tryVerify(function() {
            return bpmRange.first.handle.x < bpmRange.second.handle.x
        })
        var firstX = bpmRange.first.handle.x
        bpmRange.second.value = 150
        tryVerify(function() {
            return bpmRange.second.handle.x < bpmRange.width
                    && bpmRange.first.handle.x === firstX
        })
        var minimum = findChild(filter, "minimumBpmField")
        var maximum = findChild(filter, "maximumBpmField")
        verify(minimum)
        verify(maximum)
        minimum.text = "72"
        maximum.text = "155"
        minimum.editingFinished()
        maximum.editingFinished()
        compare(filter.pendingMinBpm, 72)
        compare(filter.pendingMaxBpm, 155)
        filter.destroy()
    }

    function test_track_header_is_compact_and_emphasized() {
        var list = trackListComponent.createObject(mainWindow.contentItem, {
            "width": 960,
            "height": 360
        })
        verify(list)
        compare(list.headerItem.height, Theme.tableHeaderHeight)
        compare(findChild(list, "trackHeaderTitle").font.weight,
                Font.DemiBold)
        list.destroy()
    }

    function test_fractional_bpm_is_not_rounded_away_in_visible_surfaces() {
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        compare(list.formatBpm(127.5), "127.5")
        compare(list.formatBpm(128), "128")
        list.destroy()

        var pane = findChild(mainWindow, "playerPane")
        verify(pane)
        compare(pane.formatBpm(127.5), "127.5 BPM")
        compare(pane.formatBpm(128), "128 BPM")
    }

    function test_waveform_modes_use_offline_waveform_and_live_spectrum() {
        var waveform = findChild(mainWindow, "mainWaveform")
        var playedClip = findChild(mainWindow, "waveformPlayedClip")
        var playedWaveform = findChild(mainWindow, "playedWaveform")
        var playbackGuide = findChild(mainWindow, "waveformPlaybackGuide")
        var session = mainWindow.waveformSession
        var frequencySettings = SettingsController.frequencyColorWaveform
        verify(waveform && playedClip && playedWaveform && playbackGuide && session)
        var previousMode = SettingsController.waveformMode
        var previousGuide = SettingsController.waveformPlaybackGuide
        var previousLayers = session.layers
        var previousDuration = session.durationMs
        var testLayers = {
            "mix": [0.2, 0.5, 0.8, 0.4],
            "bass": [0.3, 0.4, 0.5, 0.2],
            "mid": [0.2, 0.6, 0.4, 0.3],
            "high": [0.1, 0.3, 0.7, 0.5],
            "_sampleRate": 48000,
            "_totalSamples": 48000,
            "_peakCount": 4
        }
        session.layers = testLayers
        session.durationMs = 1000

        SettingsController.waveformMode = 0
        tryCompare(waveform.layers.mix, "length", 4)
        compare(waveform.visualMode, 0)

        SettingsController.waveformMode = 3
        SettingsController.waveformPlaybackGuide = true
        session.layers = testLayers
        tryCompare(waveform.layers.high, "length", 4)
        compare(waveform.visualMode, 3)
        compare(String(waveform.lowColor), String(frequencySettings.lowColor))
        compare(String(waveform.midColor), String(frequencySettings.midColor))
        compare(String(waveform.highColor), String(frequencySettings.highColor))
        compare(waveform.frequencyUnplayedOpacity,
                Theme.nonImmersiveSpectralUnplayedOpacity)
        compare(waveform.position, 0,
                "the base frequency pass must remain entirely unplayed")
        compare(playedClip.visible, true,
                "frequency progress must use the same smooth clipped overlay")
        compare(playedWaveform.position, playedWaveform.duration)
        tryVerify(function() {
            return Math.abs(playedClip.width - waveform.waveformCursorX) <= 0.5
        })
        compare(playbackGuide.visible, true)

        SettingsController.waveformMode = 1
        tryCompare(waveform, "visualMode", 1)

        SettingsController.waveformMode = 2
        tryCompare(waveform, "visualMode", 2)

        SettingsController.waveformMode = previousMode
        SettingsController.waveformPlaybackGuide = previousGuide
        session.layers = previousLayers
        session.durationMs = previousDuration
    }

    function test_z_current_track_metadata_reacts_to_library_changes() {
        var favoriteButton = findChild(mainWindow, "favoriteButton")
        verify(favoriteButton, "current-track favorite button should exist")
        verify(PlaybackController.currentTrackId.length > 0)

        var row = LibraryModel.indexForTrackId(PlaybackController.currentTrackId)
        verify(row >= 0)
        LibraryModel.setFavorite(row, false)
        tryVerify(function() {
            return favoriteButton.icon.source.toString()
                    === Theme.icon("heart-line").toString()
        })

        LibraryModel.setFavorite(row, true)
        tryVerify(function() {
            return favoriteButton.icon.source.toString()
                    === Theme.icon("heart-fill").toString()
        })
        LibraryModel.setFavorite(row, false)
    }

    function test_current_track_metadata_uses_the_playback_track_id() {
        var trackId = nativeDropHelper.ensureLongAlbumArtistTrack()
        var row = LibraryModel.indexForTrackId(trackId)
        verify(row >= 0)

        PlaybackController.playRow(row)
        tryCompare(PlaybackController, "currentTrackId", trackId, 1000)

        var artistAlbum = findChild(mainWindow, "trackArtistAlbum")
        verify(artistAlbum)
        tryCompare(artistAlbum, "artist",
                   "An intentionally long artist name for hover marquee verification")
        tryCompare(artistAlbum, "album",
                   "An intentionally long album name for hover marquee verification")
        verify(artistAlbum.text.indexOf(" · ") > 0,
               "the main player must render artist, album and tags separately")
        compare(artistAlbum.text.split(" · ").length, 2)
        verify(!artistAlbum.text.endsWith(" · "))
        verify(artistAlbum.text.indexOf("无标签") < 0)

        LibraryModel.setTags(trackId, ["测试标签"])
        tryVerify(function() {
            return artistAlbum.text.split(" · ").length === 3
                    && artistAlbum.text.endsWith("测试标签")
        }, 1000, "real tags must be appended after artist and album")
        LibraryModel.setTags(trackId, [])
    }

    function test_main_waveform_toggle_uses_complete_line_icon() {
        var button = findChild(mainWindow, "waveformModeButton")
        verify(button)
        verify(button.icon.source.toString().endsWith("/waveform-switch.svg"))
        compare(button.icon.width, 20)
        compare(button.icon.height, 20)
    }

    function test_current_track_rating_follows_artist_and_album() {
        var ids = nativeDropHelper.ensureSortableTracks()
        verify(ids.length > 0)
        PlaybackController.playRow(LibraryModel.indexForTrackId(ids[0]))
        tryCompare(PlaybackController, "currentTrackId", ids[0], 1000)
        var artistAlbum = findChild(mainWindow, "trackArtistAlbum")
        var rating = findChild(mainWindow, "trackRating")
        verify(artistAlbum && rating)
        var artistEnd = artistAlbum.mapToItem(mainWindow.contentItem,
                                              artistAlbum.width, 0).x
        var ratingStart = rating.mapToItem(mainWindow.contentItem, 0, 0).x
        verify(ratingStart <= artistEnd + 24,
               "rating stars must follow artist/album instead of the row edge: "
               + ratingStart + " > " + artistEnd)
    }

    function test_classic_player_rating_uses_reference_icon_size() {
        var rating = findChild(mainWindow, "trackRating")
        verify(rating, "the classic player must expose its rating row")
        compare(rating.iconSize, 17)
    }

    function test_classic_header_rows_are_compact_and_centered() {
        nativeDropHelper.ensureSortableTracks()
        var cover = findChild(mainWindow, "playerCover")
        var title = findChild(mainWindow, "trackTitleViewport").parent
        var artist = findChild(mainWindow, "trackArtistRatingRow")
        var metadata = findChild(mainWindow, "trackMetadataBadges")
        var oldHeight = mainWindow.height
        try {
            for (var h of [266, 360, 500]) {
                mainWindow.height = h
                wait(50)
                var top = title.mapToItem(cover, 0, 0).y
                var middle = artist.mapToItem(cover, 0, 0).y
                var bottom = metadata.mapToItem(cover, 0, metadata.height).y
                verify(Math.abs((top + bottom) / 2 - cover.height / 2) <= 1,
                       "three rows must center on the cover: " + [h, top, bottom, cover.height])
                var gap1 = middle - top - title.height
                var gap2 = metadata.mapToItem(cover, 0, 0).y - middle - artist.height
                verify(Math.abs(gap1 - gap2) <= 1, "row gaps must be equal")
                verify(metadata.height <= 20, "metadata chips should stay compact")
            }
        } finally {
            mainWindow.height = oldHeight
        }
    }

    function test_z_playing_row_uses_three_independent_spectrum_bars() {
        verify(PlaybackController.currentTrackId.length > 0)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        tryVerify(function() { return list.count > 0 })

        var indicator = findChild(list, "playingBarsIndicator")
        verify(indicator)
        var currentRow = findChild(list, "currentTrackRow")
        verify(currentRow)
        var currentFavorite = findChild(currentRow, "trackFavoriteCell")
        var currentRatingStar = findChild(currentRow, "trackRatingStar0")
        var currentSubtitle = findChild(currentRow, "singleWindowTrackSubtitle")
        verify(currentFavorite && currentRatingStar && currentSubtitle)
        var previousMode = SettingsController.themeMode
        try {
            for (var mode of [0, 1]) {
                SettingsController.themeMode = mode
                wait(0)
                for (var profile of ["classic", "integrated", "rolling"]) {
                    list.layoutProfile = profile
                    wait(0)
                    compare(currentRow.color.toString(), Theme.accent.toString())
                    compare(currentRow.color.a, 1)
                    compare(currentFavorite.icon.color.toString(),
                            Theme.accentText.toString())
                    compare(currentRatingStar.tint.toString(),
                            Theme.accentText.toString())
                    compare(currentSubtitle.color.toString(),
                            Theme.accentText.toString())
                }
            }
        } finally {
            SettingsController.themeMode = previousMode
        }
        compare(indicator.barCount, 3)
        compare(indicator.barColor.toString(), Theme.accentText.toString())
        verify(findChild(indicator, "playingBar0"))
        verify(findChild(indicator, "playingBar1"))
        verify(findChild(indicator, "playingBar2"))
        list.destroy()
    }

    function test_z_track_columns_and_search_modules_follow_stable_grid() {
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        compare(findChild(list, "trackHeaderIndex").width, 42)
        compare(findChild(list, "trackHeaderFavorite").width, 52)
        compare(findChild(list, "trackHeaderAlbum").width, 112)
        compare(findChild(list, "trackHeaderArtist").width, 112)
        compare(findChild(list, "trackHeaderRating").width, 110)
        compare(findChild(list, "trackHeaderBpm").width, 64)
        compare(findChild(list, "trackHeaderDuration").width, 72)
        compare(findChild(list, "trackHeaderFavoriteAlbumGap").width, 6)
        verify(!findChild(list, "trackRowOptionsButton"),
               "the duration column must be the final visible column")
        list.destroy()

        var filter = searchFilterComponent.createObject(mainWindow.contentItem)
        verify(filter)
        compare(findChild(filter, "keywordModule").width, 184)
        compare(findChild(filter, "ratingModule").width, 132)
        compare(findChild(filter, "bpmModule").width, 216)
        compare(findChild(filter, "bpmRange").width, 104)
        filter.destroy()
    }

    function test_z_classic_list_uses_relaxed_reference_columns_and_icons() {
        var previousCategory = findChild(mainWindow, "filterModel").category
        var previousTagKey = findChild(mainWindow, "filterModel").tagKey
        var filterModel = findChild(mainWindow, "filterModel")
        var window = null
        try {
            nativeDropHelper.ensureSortableTracks()
            filterModel.category = "all"
            filterModel.tagKey = ""
            window = listWindowComponent.createObject(null, {
                "filterModel": filterModel,
                "width": 1400,
                "height": 720
            })
            verify(window)
            compare(window.boundedLyricsWindowY(100, 500, 0, 800), 100)
            compare(window.boundedLyricsWindowY(700, 500, 0, 800), 300)
            compare(window.boundedLyricsWindowX(100, 700, 240, 0, 1000),
                    708)
            compare(window.boundedLyricsWindowX(300, 900, 240, 0, 1000),
                    52)
            var lyricsWindow = findChild(window, "listLyricsWindow")
            verify(lyricsWindow)
            verify(lyricsWindow.hostScreen)
            compare(lyricsWindow.hostScreen.name, window.screen.name)
            compare(lyricsWindow.hostScreen.width, window.screen.width)
            compare(lyricsWindow.hostScreen.height, window.screen.height)
            var list = findChild(window, "sharedTrackList")
            verify(list)
            compare(list.layoutProfile, "classic")
            window.show()
            verify(waitForRendering(list))

            var title = findChild(list, "trackHeaderTitle")
            var duration = findChild(list, "trackHeaderDuration")
            var rating = findChild(list, "trackHeaderRating")
            var favorite = findChild(list, "trackHeaderFavorite")
            var bpm = findChild(list, "trackHeaderBpm")
            var artist = findChild(list, "trackHeaderArtist")
            var album = findChild(list, "trackHeaderAlbum")
            verify(title && duration && rating && favorite && bpm
                   && artist && album)
            compare(artist.visible, false)
            compare(album.visible, false)
            verify(duration.visible && rating.visible && favorite.visible
                   && bpm.visible)
            verify(title.mapToItem(list, 0, 0).x
                   < duration.mapToItem(list, 0, 0).x)
            verify(duration.mapToItem(list, 0, 0).x
                   < rating.mapToItem(list, 0, 0).x)
            verify(rating.mapToItem(list, 0, 0).x
                   < favorite.mapToItem(list, 0, 0).x)
            verify(favorite.mapToItem(list, 0, 0).x
                   < bpm.mapToItem(list, 0, 0).x)

            list.positionViewAtBeginning()
            var row = null
            tryVerify(function() {
                row = list.itemAtIndex(0)
                return row !== null
            })
            var rowFavorite = findChild(row, "trackFavoriteCell")
            var rowStar = findChild(row, "trackRatingStar0")
            verify(rowFavorite && rowStar)
            compare(rowFavorite.icon.width, 20)
            compare(rowFavorite.icon.height, 20)
            compare(rowStar.sourceSize.width, 15)
            compare(rowStar.sourceSize.height, 15)
            verify(rowFavorite.icon.width > rowStar.sourceSize.width)

            var navigation = findChild(window, "referenceSideNavigation")
            verify(navigation)
            navigation.activateNode("tags", "tags:manage", "")
            tryCompare(window, "tagManagementMode", true)
            compare(list.tagManagementLayout, true)
            compare(duration.visible, true)
            compare(bpm.visible, false)

            window.width = 960
            wait(20)
            verify(list.ratingWidth >= list.ratingIconSize * 5)
            var ratingCell = findChild(row, "trackRatingCell")
            var lastStar = findChild(row, "trackRatingStar4")
            verify(ratingCell && lastStar)
            verify(lastStar.mapToItem(ratingCell, 0, 0).x
                   + lastStar.width <= ratingCell.width + 0.5)
        } finally {
            if (window)
                window.destroy()
            filterModel.category = previousCategory
            filterModel.tagKey = previousTagKey
        }
    }

    function test_native_and_qml_shell_drops_share_the_classified_submission() {
        verify(mainWindow["handleShellDropUrls"] !== undefined,
               "all player shells need one classified drop submission")
        var unsupported = nativeDropHelper.createNonAudioDropFile()
        var missing = nativeDropHelper.missingDropUrl()
        verify(unsupported && missing)
        ImportController.clearErrors()
        compare(mainWindow.handleShellDropUrls([unsupported, missing]), false)
        wait(50)
        verify(!ImportController.busy)
        compare(ImportController.errors.length, 0,
                "classification must reject unsupported paths before import")
    }

    function test_task1b_track_list_profiles_keep_trailing_columns_fixed() {
        var profiles = ["classic", "integrated", "rolling"]
        var widths = [863, 960, 1180, 1440, 1672]
        for (var profileIndex = 0; profileIndex < profiles.length;
             ++profileIndex) {
            var list = trackListComponent.createObject(mainWindow.contentItem)
            verify(list)
            verify(list["layoutProfile"] !== undefined,
                   "TrackList must expose one explicit layoutProfile")
            list.layoutProfile = profiles[profileIndex]
            var trailingWidth = -1
            for (var widthIndex = 0; widthIndex < widths.length; ++widthIndex) {
                list.width = widths[widthIndex]
                verify(waitForRendering(list))
                var duration = findChild(list, "trackHeaderDuration")
                var rating = findChild(list, "trackHeaderRating")
                var favorite = findChild(list, "trackHeaderFavorite")
                var bpm = findChild(list, "trackHeaderBpm")
                verify(duration && rating && favorite && bpm)
                verify(duration.visible && rating.visible && favorite.visible)
                if (profileIndex === 0)
                    verify(bpm.visible)
                var firstTrailing = duration.mapToItem(list, 0, 0).x
                var fixedWidth = list.width - firstTrailing
                if (trailingWidth < 0)
                    trailingWidth = fixedWidth
                else
                    verify(Math.abs(fixedWidth - trailingWidth) <= 1.0,
                           "profile=" + profiles[profileIndex]
                           + " width=" + widths[widthIndex]
                           + " trailing=" + fixedWidth
                           + " expected=" + trailingWidth)
            }
            list.destroy()
        }
    }

    function test_task1b_navigation_uses_shared_icon_and_action_tokens() {
        verify(Theme["navigationIconVisualSize"] !== undefined)
        verify(Theme["navigationActionExtent"] !== undefined)
        verify(Theme.navigationActionExtent >= 28)
        var side = sideNavigationComponent.createObject(mainWindow.contentItem)
        verify(side)
        compare(side.navigationIconVisualSize,
                Theme.navigationIconVisualSize)
        compare(side.navigationActionExtent,
                Theme.navigationActionExtent)
        var libraryIcon = findChild(side, "suppliedNodeIcon-library")
        verify(libraryIcon)
        compare(libraryIcon.sourceSize.width,
                Theme.navigationIconVisualSize + 2)
        compare(libraryIcon.sourceSize.height,
                Theme.navigationIconVisualSize + 2)
        var nodeNames = ["navigationNode-library:all",
                         "navigationNode-favorites:favorites",
                         "navigationNode-tags:manage"]
        for (var index = 0; index < nodeNames.length; ++index) {
            var node = findChild(side, nodeNames[index])
            verify(node)
            verify(node.height >= Theme.navigationActionExtent)
        }
        side.destroy()
    }

    function test_task1b_duplicate_and_invalid_resource_drops_are_rejected() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1200,
            "height": 620
        })
        verify(window)
        var folder = nativeDropHelper.createDropDirectory()
        var invalid = nativeDropHelper.createNonAudioDropFile()
        verify(folder && invalid)
        verify(window.handleResourceDropUrls([folder]))
        compare(window.handleResourceDropUrls([folder]), false,
                "an already registered directory must not report success")
        compare(window.handleResourceDropUrls([invalid]), false)
        var path = ResourceFolderController.classifyDropUrl(folder).path
        verify(ResourceFolderController.removeMonitoredFolder(path))
        window.destroy()
    }

    function test_task1b_resource_drop_reports_completion_only_after_refresh() {
        tryVerify(function() { return !ResourceFolderController.scanning }, 3000)
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1200,
            "height": 620
        })
        verify(window)
        verify(window["resourceDropStatus"] !== undefined)
        var folder = nativeDropHelper.createDropDirectory()
        verify(folder)
        var scanFinished = signalSpyComponent.createObject(testCase, {
            "target": ResourceFolderController,
            "signalName": "scanFinished"
        })
        verify(scanFinished)
        verify(window.handleResourceDropUrls([folder]))
        compare(window.resourceDropStatus, "pending",
                "drop acceptance must not be presented as completion")
        var statusLabel = findChild(window, "resourceDropStatusLabel")
        verify(statusLabel && statusLabel.visible)
        verify(statusLabel.text.indexOf("刷新") >= 0)
        verify(statusLabel.text.indexOf("完成") < 0)
        tryCompare(scanFinished, "count", 1, 3000)
        tryCompare(window, "resourceDropStatus", "completed", 1000)
        verify(statusLabel.text.indexOf("完成") >= 0)
        wait(100)
        compare(window.resourceDropStatus, "completed")
        tryCompare(window, "resourceDropStatus", "idle", 5500)
        compare(statusLabel.text, "")
        var path = ResourceFolderController.classifyDropUrl(folder).path
        verify(ResourceFolderController.removeMonitoredFolder(path))
        scanFinished.destroy()
        window.destroy()
    }

    function test_task1b_resource_drop_waits_for_scan_started_import() {
        tryVerify(function() {
            return !ResourceFolderController.scanning
                    && !ImportController.busy
        }, 3000)
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1200,
            "height": 620
        })
        verify(window)
        var folder = nativeDropHelper.createAudioDropDirectory(testAudioUrl, 24)
        verify(folder)
        var importFinished = signalSpyComponent.createObject(testCase, {
            "target": ImportController,
            "signalName": "finished"
        })
        verify(importFinished)
        verify(window.handleResourceDropUrls([folder]))
        compare(window.resourceDropStatus, "pending")
        tryVerify(function() { return ImportController.busy }, 3000)
        compare(window.resourceDropStatus, "importing")
        compare(importFinished.count, 0)
        tryCompare(importFinished, "count", 1, 5000)
        tryCompare(window, "resourceDropStatus", "completed", 1000)
        var path = ResourceFolderController.classifyDropUrl(folder).path
        verify(ResourceFolderController.removeMonitoredFolder(path))
        var importedIds = ImportController.importedTrackIds.slice(0)
        for (var index = 0; index < importedIds.length; ++index)
            LibraryModel.removeTrack(importedIds[index])
        importFinished.destroy()
        window.destroy()
    }

    function test_task1b_resource_drop_reports_import_failure() {
        tryVerify(function() {
            return !ResourceFolderController.scanning
                    && !ImportController.busy
        }, 3000)
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1200,
            "height": 620
        })
        verify(window)
        var folder = nativeDropHelper.createInvalidAudioDropDirectory()
        verify(folder)
        var importFinished = signalSpyComponent.createObject(testCase, {
            "target": ImportController,
            "signalName": "finished"
        })
        verify(importFinished)
        verify(window.handleResourceDropUrls([folder]))
        tryCompare(importFinished, "count", 1, 5000)
        tryCompare(window, "resourceDropStatus", "failed", 1000)
        var statusLabel = findChild(window, "resourceDropStatusLabel")
        verify(statusLabel && statusLabel.text.indexOf("失败") >= 0)
        var path = ResourceFolderController.classifyDropUrl(folder).path
        verify(ResourceFolderController.removeMonitoredFolder(path))
        ImportController.clearErrors()
        importFinished.destroy()
        window.destroy()
    }

    function test_task1b_classic_and_tag_hosts_keep_trailing_cells_in_bounds() {
        var filterModel = findChild(mainWindow, "filterModel")
        var previousCategory = filterModel.category
        var previousTagKey = filterModel.tagKey
        var window = null
        try {
            verify(nativeDropHelper.ensureSortableTracks().length >= 3)
            filterModel.category = "all"
            filterModel.tagKey = ""
            window = listWindowComponent.createObject(null, {
                "filterModel": filterModel,
                "width": 956,
                "height": 620,
                "visible": true
            })
            verify(window)
            tryVerify(function() { return window.width === 956 }, 1000)
            var list = findChild(window, "sharedTrackList")
            verify(list)
            tryVerify(function() { return list.width > 0 }, 1000)
            function verifyBounds(names) {
                for (var index = 0; index < names.length; ++index) {
                    var cell = findChild(list, names[index])
                    verify(cell && cell.visible, names[index])
                    var right = cell.mapToItem(list, cell.width, 0).x
                    verify(right <= list.width + 1,
                           names[index] + " right=" + right
                           + " list=" + list.width)
                }
            }
            verifyBounds(["trackHeaderDuration", "trackHeaderRating",
                          "trackHeaderFavorite", "trackHeaderBpm"])
            var navigation = findChild(window, "referenceSideNavigation")
            navigation.activateNode("tags", "tags:manage", "")
            tryCompare(window, "tagManagementMode", true)
            verifyBounds(["trackHeaderDuration", "trackHeaderRating",
                          "trackHeaderFavorite"])
        } finally {
            if (window)
                window.destroy()
            filterModel.category = previousCategory
            filterModel.tagKey = previousTagKey
        }
    }

    function test_z_tag_workspace_is_one_continuous_three_column_surface() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400
        })
        verify(window)

        var workspace = findChild(window, "listWorkspace")
        verify(workspace, "ListWindow must expose one continuous workspace")
        compare(workspace.leftColumnWidth, Theme.navigationWidth)
        compare(workspace.rightColumnWidth, Theme.navigationWidthExpanded)
        compare(workspace.dividerWidth, 1)
        compare(workspace.border.width, 0)
        tryVerify(function() { return workspace.centerWidth > 0 })
        compare(countObjectsNamed(workspace, "sharedTrackList"), 1)

        var navigation = findChild(workspace, "referenceSideNavigation")
        verify(navigation)
        var widthBeforeTags = window.width
        navigation.activateNode("tags", "tags:manage", "")
        tryCompare(window, "pageMinimumWidth", 956)
        compare(window.width, widthBeforeTags)
        compare(findChild(navigation, "libraryNavigationList").model,
                LibraryNavigationModel)

        var trackList = findChild(workspace, "sharedTrackList")
        verify(trackList)
        compare(findChild(trackList, "trackHeaderIndex").text, "#")
        compare(findChild(trackList, "trackHeaderTitle").text, "歌曲")
        compare(findChild(trackList, "trackHeaderFavorite").text, "收藏")
        compare(findChild(trackList, "trackHeaderArtist").visible, false)
        compare(findChild(trackList, "trackHeaderAlbum").visible, false)
        compare(findChild(trackList, "trackHeaderDuration").text, "时长")

        var tagPanel = findChild(workspace, "tagManagementPanel")
        verify(tagPanel)
        tryCompare(tagPanel, "width", Theme.navigationWidthExpanded)
        var tagFlickable = findChild(tagPanel, "tagFlickable")
        var tagFlow = findChild(tagPanel, "tagFlow")
        verify(tagFlickable && tagFlow,
               "the tag column must use a scrolling Flow layout")
        compare(tagFlow.width, tagFlickable.width)

        window.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task6_tag_flow_uses_natural_width_wrap_and_scroll() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = tagManagementPanelWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 248,
            "height": 230
        })
        verify(window && filterModel)
        var panel = findChild(window, "tagManagementPanel")
        verify(panel)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)
        tryCompare(findChild(panel, "tagSearchField"), "height", 34)
        tryCompare(findChild(panel, "addTagButton"), "height", 34)

        var names = ["好", "中文", "好听", "音乐", "摇滚", "流行",
                     "民谣", "电子", "古典", "爵士", "轻音乐", "现场",
                     "通勤", "夜晚", "晨间", "运动", "专注", "旅行",
                     "怀旧", "派对",
                     "long Chinese English natural-width capsule"]
        var keys = []
        var createdKeys = []
        for (var index = 0; index < names.length; ++index) {
            var key = names[index].toLocaleLowerCase()
            keys.push(key)
            if (tagRowForKey(key) < 0) {
                verify(panel.addTag(names[index]))
                createdKeys.push(key)
            }
        }
        task4TemporaryTagKeys = createdKeys
        panel.searchText = ""

        var flickable = findChild(panel, "tagFlickable")
        var flow = findChild(panel, "tagFlow")
        verify(flickable && flow)
        var firstPill = findChild(panel, "tagPill-" + keys[0])
        var secondPill = findChild(panel, "tagPill-" + keys[1])
        var longPill = findChild(panel, "tagPill-" + keys[keys.length - 1])
        verify(firstPill && secondPill && longPill)
        mouseMove(window.contentItem, window.width - 2, window.height - 2)
        tryCompare(firstPill, "color", Qt.rgba(0, 0, 0, 0), 1000)
        compare(firstPill.height, 24)
        compare(firstPill.radius, 10)
        compare(firstPill.color.a, 0)
        compare(firstPill.border.width, 1.4)
        compare(findChild(firstPill, "tagCapsuleName-" + keys[0]).font.pixelSize, 12)
        compare(firstPill.border.color, firstPill.baseAccent)
        compare(panel.pillHorizontalPadding, 6)
        compare(panel.pillCountHorizontalPadding, 5)
        compare(panel.pillCountMinimumWidth, 0)
        var firstName = findChild(firstPill, "tagCapsuleName-" + keys[0])
        var firstCount = findChild(firstPill, "tagCapsuleCount-" + keys[0])
        var firstNotch = findChild(firstPill, "tagCapsuleNotch-" + keys[0])
        verify(firstName && firstCount)
        compare(firstNotch, null)
        compare(findChild(firstPill, "tagCapsuleLeft-" + keys[0]), null)
        compare(findChild(firstPill, "tagCapsuleRight-" + keys[0]), null)
        compare(firstName.text, names[0])
        compare(firstCount.text, String(firstPill.parent.trackCount))
        compare(firstName.color, Theme.primaryText)
        compare(firstCount.color, Theme.primaryText)
        verify(longPill.width > firstPill.width,
               "long labels must retain a larger natural capsule width")
        var shortPills = [firstPill, secondPill,
                          findChild(panel, "tagPill-" + keys[2])]
        var sharesShortRow = false
        tryVerify(function() {
            for (var left = 0; left < shortPills.length; ++left) {
                for (var right = left + 1; right < shortPills.length; ++right) {
                    if (shortPills[left] && shortPills[right]
                            && shortPills[right].parent.x
                               > shortPills[left].parent.x
                            && shortPills[right].parent.y
                               === shortPills[left].parent.y) {
                        sharesShortRow = true
                        return true
                    }
                }
            }
            return false
        }, 500)
        verify(sharesShortRow,
               "short labels should share a Flow row in the 248px column; "
               + firstPill.parent.x + "," + firstPill.parent.y
               + "," + firstPill.width + " / "
               + secondPill.parent.x + "," + secondPill.parent.y
               + "," + secondPill.width + " / "
               + shortPills[2].parent.x + "," + shortPills[2].parent.y
               + "," + shortPills[2].width + " flow=" + flow.width)
        verify(longPill.parent.width === flow.width,
               "a long label must naturally occupy the full available Flow row")
        tryVerify(function() { return flickable.contentHeight > flickable.height },
                  500, "many tags must make the Flow content scrollable")

        panel.selectTag(keys[0])
        tryVerify(function() { return firstPill.selectedVisual }, 500)
        mouseMove(window.contentItem, window.width - 2, window.height - 2)
        tryCompare(firstPill, "color", firstPill.baseAccent, 1000)
        compare(firstPill.border.color, firstPill.baseAccent)

        var pointer = findChild(firstPill, "tagPillPointerArea-" + keys[0])
        verify(pointer)
        if (firstPill.selectedVisual)
            panel.selectTag(keys[0])
        tryVerify(function() { return !firstPill.selectedVisual }, 500)
        flickable.contentY = Math.max(0, Math.min(firstPill.parent.y,
                                                  flickable.contentHeight
                                                  - flickable.height))
        wait(0)
        // Move away first so HoverHandler receives a real enter transition
        // both in isolation and after the full suite.
        mouseMove(window.contentItem, window.width - 2, window.height - 2)
        tryVerify(function() { return !firstPill.hoveredVisual }, 500)
        tryCompare(firstPill, "color", Qt.rgba(0, 0, 0, 0), 1000)
        mouseMove(pointer, pointer.width / 2, pointer.height / 2)
        tryVerify(function() { return firstPill.hoveredVisual }, 500)
        compare(firstPill.color, firstPill.baseAccent)
        verify(colorContrast(firstName.color, firstPill.color) >= 4.5)
        mousePress(pointer, pointer.width / 2, pointer.height / 2,
                   Qt.LeftButton)
        tryVerify(function() { return firstPill.pressedVisual }, 500)
        compare(firstPill.color, firstPill.baseAccent)
        mouseRelease(pointer, pointer.width / 2, pointer.height / 2,
                     Qt.LeftButton)

        var previousMode = SettingsController.themeMode
        SettingsController.themeMode = 1
        verify(colorContrast(firstName.color, firstPill.baseAccent) >= 4.5)
        compare(firstCount.color, firstName.color)
        SettingsController.themeMode = 0
        verify(colorContrast(firstName.color, firstPill.baseAccent) >= 4.5)
        var stableAccent = firstPill.baseAccent
        for (var manualColor of ["#ffffff", "#000000", "#ffff00", "#3654ff"]) {
            firstPill.baseAccent = manualColor
            tryCompare(firstPill, "color", firstPill.baseAccent, 1000)
            verify(colorContrast(firstName.color, firstPill.color) >= 4.5,
                   "hover text must remain readable on custom tag colors")
        }
        firstPill.baseAccent = stableAccent
        SettingsController.themeMode = previousMode

        panel.searchText = names[names.length - 1]
        tryCompare(panel, "visibleTagCount", 1)
        panel.searchText = ""
        tryVerify(function() { return findChild(panel, "tagPill-" + keys[0]) }, 500)
        window.destroy()

        mainWindow.requestActivate()
        tryVerify(function() { return mainWindow.active }, 1000)
        var focusPanel = tagManagementPanelComponent.createObject(
                    mainWindow.contentItem, {
                        "x": 0,
                        "y": 0,
                        "width": 248,
                        "height": 230,
                        "filterModel": filterModel,
                        "searchText": names[1]
                    })
        verify(focusPanel)
        var focusPill = findChild(focusPanel, "tagPill-" + keys[1])
        var focusPointer = findChild(
                    focusPill, "tagPillPointerArea-" + keys[1])
        var focusSearch = findChild(focusPanel, "tagSearchField")
        verify(focusPill && focusPointer && focusSearch)
        verify(!focusPointer.activeFocus,
               "tag delegates must not steal focus when the panel opens")
        focusSearch.forceActiveFocus()
        for (var tabStep = 0; tabStep < 4 && !focusPointer.activeFocus;
             ++tabStep) {
            verify(nativeDropHelper.sendKey(focusSearch, Qt.Key_Tab))
            wait(0)
        }
        verify(focusPointer.activeFocus,
               "Tab must be able to enter a tag pill")
        mouseClick(focusPointer, focusPointer.width / 2,
                   focusPointer.height / 2)
        tryVerify(function() { return focusPointer.activeFocus }, 500,
                  "clicking a tag pill must give it focus")
        verify(focusPill.selectedVisual)
        compare(focusPill.color, focusPill.baseAccent)
        verify(!findChild(focusPill, "tagCapsuleFocus-" + keys[1]).visible,
               "selected tags must not show a white inner focus outline")
        var selectedAfterClick = TagModel.selectedKey
        keyClick(Qt.Key_Space)
        compare(TagModel.selectedKey, selectedAfterClick,
                "Space must remain available to the global playback shortcut")
        focusPanel.destroy()
    }

    function test_z_tag_panel_add_search_and_selection_update_real_models() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var filterModel = findChild(mainWindow, "filterModel")
        filterModel.tagKey = ""
        TagModel.selectedKey = ""
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400
        })
        verify(window)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)
        var panel = findChild(window, "tagManagementPanel")
        verify(panel)
        var suffix = String(Date.now())
        var alphaName = "Task4 Alpha " + suffix
        var betaName = "Task4 Beta " + suffix
        var alphaKey = alphaName.toLocaleLowerCase()
        var betaKey = betaName.toLocaleLowerCase()
        task4TemporaryTagKeys = [alphaKey, betaKey]
        var initialCount = TagModel.count
        verify(panel.addTag(alphaName), "add must call the real TagModel")
        verify(panel.addTag(betaName), "second real tag should be added")
        tryCompare(TagModel, "count", initialCount + 2)

        var proxy = findChild(panel, "tagFilterProxy")
        verify(proxy)
        compare(proxy.sourceModel, TagModel)
        panel.searchText = "Alpha " + suffix
        tryCompare(panel, "visibleTagCount", 1)
        panel.selectTag(alphaKey)
        compare(TagModel.selectedKey, alphaKey)
        compare(filterModel.tagKey, alphaKey)
        var navigation = findChild(window, "referenceSideNavigation")
        verify(navigation.nodeIsSelected("tags", "tags:manage", ""))
        verify(!navigation.nodeIsSelected("library", "library:all", ""),
               "tag filtering must not leave both library and tags selected")

        panel.selectTag(alphaKey)
        compare(TagModel.selectedKey, "")
        compare(filterModel.tagKey, "")

        window.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task5_tag_menu_delete_preserves_other_filters() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = tagManagementPanelWindowComponent.createObject(
                    null, { "filterModel": filterModel })
        verify(window)
        var panel = findChild(window, "tagManagementPanel")
        verify(panel)
        var removeRequested = signalSpyComponent.createObject(testCase, {
            "target": panel,
            "signalName": "removeTagRequested"
        })
        verify(removeRequested)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)
        var name = "Task5 Delete " + Date.now()
        var key = name.toLocaleLowerCase()
        task4TemporaryTagKeys = [key]
        verify(panel.addTag(name))
        panel.searchText = name
        tryCompare(panel, "visibleTagCount", 1)
        var tagGrid = findChild(panel, "tagGrid")
        verify(tagGrid)
        tagGrid.positionViewAtBeginning()
        wait(30)
        panel.selectTag(key)
        filterModel.searchText = "keep-search"
        filterModel.exactRating = 4
        filterModel.minBpm = 88
        filterModel.maxBpm = 144

        var tagCell = tagGrid.itemAtIndex(0)
        verify(tagCell)
        var pill = findChild(tagCell, "tagPill-" + key)
        verify(pill)
        var pointerArea = findChild(pill, "tagPillPointerArea-" + key)
        verify(pointerArea)
        var pointerGlobal = pointerArea.mapToGlobal(0, 0)
        verify(pointerGlobal.y >= window.y
               && pointerGlobal.y + pointerArea.height <= window.y + window.height,
               "active tag delegate must be inside the test window; y="
               + pointerGlobal.y + " window=" + window.y + ","
               + window.height + " gridY=" + tagGrid.y
               + " gridHeight=" + tagGrid.height
               + " contentY=" + tagGrid.contentY
               + " cellY=" + tagCell.y)
        mouseClick(pointerArea, pointerArea.width / 2,
                   pointerArea.height / 2, Qt.RightButton)
        compare(panel.contextTagKey, key)
        var menu = findChild(panel, "tagContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        var removeAction = findChild(menu, "tagMenuDelete")
        verify(removeAction && removeAction.enabled)
        mouseClick(removeAction, removeAction.width / 2,
                   removeAction.height / 2)
        var confirm = findChild(panel, "removeTagDialog")
        tryVerify(function() { return confirm && confirm.visible }, 500)
        var warning = findChild(confirm, "removeTagWarning")
        verify(warning.text.indexOf("不删除歌曲或磁盘文件") >= 0)
        confirm.accept()
        compare(removeRequested.count, 1,
                "the mutation signal must follow a successful removal")
        tryCompare(TagModel, "selectedKey", "")
        compare(filterModel.tagKey, "")
        compare(filterModel.searchText, "keep-search")
        compare(filterModel.exactRating, 4)
        compare(filterModel.minBpm, 88)
        compare(filterModel.maxBpm, 144)
        task4TemporaryTagKeys = []
        removeRequested.destroy()
        window.destroy()
    }

    function test_z_task5_tag_menu_rename_color_and_cancel_are_real() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = tagManagementPanelWindowComponent.createObject(
                    null, { "filterModel": filterModel })
        verify(window)
        var panel = findChild(window, "tagManagementPanel")
        verify(panel)
        var renameRequested = signalSpyComponent.createObject(testCase, {
            "target": panel,
            "signalName": "renameTagRequested"
        })
        var colorRequested = signalSpyComponent.createObject(testCase, {
            "target": panel,
            "signalName": "changeTagColorRequested"
        })
        verify(renameRequested && colorRequested)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)

        var suffix = String(Date.now())
        var originalName = "Task5 Original " + suffix
        var originalKey = originalName.toLocaleLowerCase()
        var renamedName = "Task5 Renamed " + suffix
        var renamedKey = renamedName.toLocaleLowerCase()
        task4TemporaryTagKeys = [originalKey, renamedKey]
        verify(panel.addTag(originalName))
        panel.searchText = originalName
        tryCompare(panel, "visibleTagCount", 1)
        var tagGrid = findChild(panel, "tagGrid")
        verify(tagGrid)
        tagGrid.positionViewAtBeginning()
        wait(30)
        panel.selectTag(originalKey)

        var tagCell = tagGrid.itemAtIndex(0)
        verify(tagCell)
        var pointerArea = findChild(
                    tagCell, "tagPillPointerArea-" + originalKey)
        verify(pointerArea)
        var menu = findChild(panel, "tagContextMenu")
        verify(menu && !menu.visible)
        mouseClick(pointerArea, pointerArea.width / 2,
                   pointerArea.height / 2, Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        var renameAction = findChild(menu, "tagMenuRename")
        mouseClick(renameAction, renameAction.width / 2,
                   renameAction.height / 2)
        var renameDialog = findChild(panel, "renameTagDialog")
        var renameField = findChild(renameDialog, "renameTagField")
        tryVerify(function() { return renameDialog.visible }, 500)
        renameField.text = renamedName
        renameDialog.reject()
        compare(tagRowForKey(originalKey) >= 0, true)
        compare(tagRowForKey(renamedKey), -1)
        compare(filterModel.tagKey, originalKey)
        compare(renameRequested.count, 0,
                "cancel must not emit a rename mutation signal")

        mouseClick(pointerArea, pointerArea.width / 2,
                   pointerArea.height / 2, Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        mouseClick(renameAction, renameAction.width / 2,
                   renameAction.height / 2)
        tryVerify(function() { return renameDialog.visible }, 500)
        renameField.text = renamedName
        renameDialog.accept()
        tryVerify(function() { return tagRowForKey(renamedKey) >= 0 }, 500)
        compare(tagRowForKey(originalKey), -1)
        compare(TagModel.selectedKey, renamedKey)
        compare(filterModel.tagKey, renamedKey)
        compare(renameRequested.count, 1)

        panel.searchText = renamedName
        tryCompare(panel, "visibleTagCount", 1)
        tagGrid.positionViewAtBeginning()
        wait(30)
        tagCell = tagGrid.itemAtIndex(0)
        verify(tagCell)
        pointerArea = findChild(tagCell,
                                "tagPillPointerArea-" + renamedKey)
        verify(pointerArea)
        var renamedRow = tagRowForKey(renamedKey)
        var beforeColor = TagModel.data(
                    TagModel.index(renamedRow, 0), TagModel.ColorRole).toString()
        mouseClick(pointerArea, pointerArea.width / 2,
                   pointerArea.height / 2, Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        var colorAction = findChild(menu, "tagMenuColor")
        verify(colorAction && colorAction.enabled)
        var colorField = findChild(panel, "tagColorPicker")
        verify(colorField)
        mouseClick(colorAction, colorAction.width / 2,
                   colorAction.height / 2)
        var colorPicker = findChild(window, "colorFieldPicker")
        verify(colorPicker)
        tryCompare(colorPicker, "visible", true)
        verify(colorPicker.width >= 288 && colorPicker.width <= 304)
        verify(colorPicker.contentItem.width <= colorPicker.availableWidth + 0.5)
        colorPicker.setWorkingColor("#123456")
        colorPicker.close()
        compare(TagModel.data(TagModel.index(renamedRow, 0),
                              TagModel.ColorRole).toString(), beforeColor)
        compare(colorRequested.count, 0,
                "cancel must not emit a color mutation signal")

        mouseClick(pointerArea, pointerArea.width / 2,
                   pointerArea.height / 2, Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        mouseClick(colorAction, colorAction.width / 2,
                   colorAction.height / 2)
        tryCompare(colorPicker, "visible", true)
        colorPicker.setWorkingColor("#123456")
        colorPicker.acceptColor()
        compare(TagModel.data(TagModel.index(renamedRow, 0),
                              TagModel.ColorRole).toString(), "#123456")
        compare(colorRequested.count, 1)

        renameRequested.destroy()
        colorRequested.destroy()
        window.destroy()
    }

    function test_z_task5_failed_tag_rename_keeps_filter_and_emits_no_signal() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = tagManagementPanelWindowComponent.createObject(
                    null, { "filterModel": filterModel })
        verify(window && filterModel)
        var panel = findChild(window, "tagManagementPanel")
        verify(panel)
        var renameRequested = signalSpyComponent.createObject(testCase, {
            "target": panel,
            "signalName": "renameTagRequested"
        })
        verify(renameRequested)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)

        var suffix = String(Date.now())
        var originalName = "Task5 Vanishing " + suffix
        var originalKey = originalName.toLocaleLowerCase()
        var collisionName = "Task5 Existing " + suffix
        var collisionKey = collisionName.toLocaleLowerCase()
        task4TemporaryTagKeys = [originalKey, collisionKey]
        verify(panel.addTag(originalName))
        verify(panel.addTag(collisionName))
        panel.searchText = originalName
        tryCompare(panel, "visibleTagCount", 1)
        var tagGrid = findChild(panel, "tagGrid")
        tagGrid.positionViewAtBeginning()
        wait(30)
        panel.selectTag(originalKey)
        compare(filterModel.tagKey, originalKey)

        var tagCell = tagGrid.itemAtIndex(0)
        verify(tagCell)
        var pointerArea = findChild(
                    tagCell, "tagPillPointerArea-" + originalKey)
        verify(pointerArea)
        mouseClick(pointerArea, pointerArea.width / 2,
                   pointerArea.height / 2, Qt.RightButton)
        var menu = findChild(panel, "tagContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        var renameAction = findChild(menu, "tagMenuRename")
        mouseClick(renameAction, renameAction.width / 2,
                   renameAction.height / 2)
        var renameDialog = findChild(panel, "renameTagDialog")
        var renameField = findChild(renameDialog, "renameTagField")
        tryVerify(function() { return renameDialog.visible }, 500)

        TagModel.removeTag(originalKey)
        compare(tagRowForKey(originalKey), -1)
        compare(tagRowForKey(collisionKey) >= 0, true)
        renameField.text = collisionName
        renameDialog.accept()

        compare(filterModel.tagKey, originalKey,
                "a failed rename must not migrate the active filter")
        compare(renameRequested.count, 0,
                "a failed rename must not emit a mutation signal")
        compare(tagRowForKey(collisionKey) >= 0, true)
        renameRequested.destroy()
        window.destroy()
    }

    function test_z_task5_track_drop_appends_tag_to_every_selected_track() {
        var ids = nativeDropHelper.ensureSortableTracks()
        compare(ids.length, 3)
        verify(LibraryModel.setTags(ids[0], ["Existing"]))
        verify(LibraryModel.setTags(ids[1], ["Other"]))
        var filterModel = findChild(mainWindow, "filterModel")
        var window = tagManagementPanelWindowComponent.createObject(
                    null, { "filterModel": filterModel })
        verify(window)
        var panel = findChild(window, "tagManagementPanel")
        verify(panel)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)

        var tagName = "Task5 Drop " + Date.now()
        var tagKey = tagName.toLocaleLowerCase()
        task4TemporaryTagKeys = [tagKey]
        verify(panel.addTag(tagName))
        panel.searchText = tagName
        tryCompare(panel, "visibleTagCount", 1)
        var tagGrid = findChild(panel, "tagGrid")
        tagGrid.positionViewAtBeginning()
        wait(30)
        var tagCell = tagGrid.itemAtIndex(0)
        verify(tagCell)
        var dropTarget = findChild(tagCell, "tagDropTarget-" + tagKey)
        verify(dropTarget)
        verify(nativeDropHelper.sendTrackIds(dropTarget, [ids[0], ids[1]]),
               "the tag pill must accept the real track-id MIME payload")
        compare(dropTarget.acceptedAnimationCount, 1,
                "a successful tag drop must play one lightweight load pulse")

        var firstTags = LibraryModel.data(
                    LibraryModel.index(LibraryModel.indexForTrackId(ids[0]), 0),
                    LibraryModel.TagsRole)
        var secondTags = LibraryModel.data(
                    LibraryModel.index(LibraryModel.indexForTrackId(ids[1]), 0),
                    LibraryModel.TagsRole)
        verify(firstTags.indexOf("Existing") >= 0)
        verify(firstTags.indexOf(tagName) >= 0)
        verify(secondTags.indexOf("Other") >= 0)
        verify(secondTags.indexOf(tagName) >= 0)
        window.destroy()
    }

    function test_z_task5_tag_drop_shows_accepting_hover_before_drop() {
        var ids = nativeDropHelper.ensureSortableTracks()
        var tagName = "Task2 Hover " + Date.now()
        var tagKey = tagName.toLocaleLowerCase()
        task4TemporaryTagKeys = [tagKey]
        verify(TagModel.createTag(tagName))
        var filterModel = findChild(mainWindow, "filterModel")
        var panel = tagManagementPanelComponent.createObject(
                    mainWindow.contentItem, {
                        "x": 0,
                        "y": 0,
                        "filterModel": filterModel,
                        "searchText": tagName
                    })
        var list = trackListComponent.createObject(
                    mainWindow.contentItem, {
                        "x": 420,
                        "y": 0,
                        "width": 680
                    })
        verify(panel && list)
        mainWindow.requestActivate()
        tryCompare(panel, "visibleTagCount", 1, 500)
        var tagGrid = findChild(panel, "tagGrid")
        tagGrid.positionViewAtBeginning()
        var tagCell = null
        tryVerify(function() {
            tagCell = tagGrid.itemAtIndex(0)
            return tagCell !== null
        }, 500)
        var target = findChild(tagCell, "tagDropTarget-" + tagKey)
        var feedback = findChild(tagCell, "tagDropFeedback-" + tagKey)
        var firstIndex = LibraryModel.indexForTrackId(ids[0])
        list.positionViewAtIndex(firstIndex, ListView.Beginning)
        wait(30)
        var row = list.itemAtIndex(firstIndex)
        var area = findChild(row, "trackRowDragArea")
        var proxy = findChild(list, "trackDragProxy")
        verify(target && feedback)
        verify(row && area && proxy)
        var point = target.mapToItem(area, target.width / 2,
                                     target.height / 2)
        mousePress(area, area.width / 2, area.height / 2, Qt.LeftButton)
        mouseMove(area, area.width / 2 + 20, area.height / 2,
                  20, Qt.LeftButton)
        tryVerify(function() { return proxy.Drag.active }, 500)
        mouseMove(area, point.x, point.y, 60, Qt.LeftButton)
        tryVerify(function() { return target.containsDrag }, 500)
        verify(feedback.visible,
               "tag target must expose a visible accepting hover state")
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !list.dragSessionActive }, 500)
        mouseRelease(area, point.x, point.y, Qt.LeftButton)
        list.destroy()
        panel.destroy()
    }

    function test_z_task5_playlist_noop_drop_is_not_accepted_or_animated() {
        var ids = nativeDropHelper.ensureSortableTracks()
        var playlistId = PlaylistModel.createPlaylist(
                    "Task2 no-op " + Date.now())
        verify(playlistId.length > 0)
        compare(PlaylistModel.addTracks(playlistId, [ids[0]]), 1)
        var navigation = sideNavigationComponent.createObject(
                    mainWindow.contentItem, {
                        "x": 0,
                        "y": 0,
                        "selectedCategory": playlistId
                    })
        var list = trackListComponent.createObject(
                    mainWindow.contentItem, {
                        "x": 220,
                        "y": 0
                    })
        verify(navigation && list)
        var target = findChild(navigation,
                               "playlistDropTarget-" + playlistId)
        verify(target)
        var rowIndex = LibraryModel.indexForTrackId(ids[0])
        list.positionViewAtIndex(rowIndex, ListView.Beginning)
        wait(30)
        var row = list.itemAtIndex(rowIndex)
        var area = findChild(row, "trackRowDragArea")
        var proxy = findChild(list, "trackDragProxy")
        verify(row && area && proxy)
        var beforeOrder = []
        for (var beforeIndex = 0; beforeIndex < list.count; ++beforeIndex)
            beforeOrder.push(list.trackIdAt(beforeIndex))
        var desiredDropY = list.mapToItem(
                    mainWindow.contentItem, 0,
                    (list.headerItem ? list.headerItem.height : 0)
                    + list.rowHeight * 2.5).y
        var currentDropY = target.mapToItem(
                    mainWindow.contentItem, 0, target.height / 2).y
        navigation.y += desiredDropY - currentDropY
        var point = target.mapToItem(area, target.width / 2,
                                     target.height / 2)
        mousePress(area, area.width / 2, area.height / 2, Qt.LeftButton)
        mouseMove(area, area.width / 2 + 20, area.height / 2,
                  20, Qt.LeftButton)
        tryVerify(function() { return proxy.Drag.active }, 500)
        mouseMove(area, point.x, point.y, 60, Qt.LeftButton)
        tryVerify(function() { return target.containsDrag }, 500)
        mouseRelease(area, point.x, point.y, Qt.LeftButton)
        tryVerify(function() { return !list.dragSessionActive }, 500)
        compare(list.lastTrackDragDropAction, Qt.IgnoreAction,
                "a no-op target must leave the drag action unaccepted")
        compare(target.acceptedAnimationCount, 0,
                "no-op drops must not play success feedback")
        compare(list.count, beforeOrder.length)
        for (var afterIndex = 0; afterIndex < list.count; ++afterIndex) {
            compare(list.trackIdAt(afterIndex), beforeOrder[afterIndex],
                    "a rejected external drop must not fall back to list reordering")
        }
        list.destroy()
        navigation.destroy()
        PlaylistModel.removePlaylist(playlistId)
    }

    function test_z_task5_single_track_drag_cancel_releases_preview_without_data_change() {
        var ids = nativeDropHelper.ensureSortableTracks()
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        mainWindow.requestActivate()
        tryVerify(function() { return mainWindow.active }, 1000)
        tryCompare(list, "count", LibraryModel.count, 500)
        list.positionViewAtBeginning()
        wait(30)
        var firstRow = list.itemAtIndex(0)
        verify(firstRow)
        var firstArea = findChild(firstRow, "trackRowDragArea")
        var proxy = findChild(list, "trackDragProxy")
        verify(firstArea && proxy)
        list.selectOnly(firstRow.trackId, 0)
        list.forceActiveFocus()
        var beforeOrder = []
        for (var row = 0; row < list.count; ++row)
            beforeOrder.push(list.trackIdAt(row))

        mousePress(firstArea, firstArea.width / 2,
                   firstArea.height / 2, Qt.LeftButton)
        mouseMove(firstArea, firstArea.width / 2 + 20,
                  firstArea.height / 2, 20, Qt.LeftButton)
        tryVerify(function() { return proxy.Drag.active }, 500)
        compare(list.dragPreviewCreationCount, 1)
        var preview = findChild(list, "trackDragPreview")
        verify(preview)
        compare(preview.selectedCount, 1)
        compare(preview.opacity, 0.68)
        var focusStealer = Qt.createQmlObject(
                    'import QtQuick; TextInput { width: 10; height: 10 }',
                    mainWindow.contentItem)
        focusStealer.forceActiveFocus()
        verify(focusStealer.activeFocus,
               "Escape cleanup must not depend on TrackList focus")
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !proxy.Drag.active }, 500)
        tryVerify(function() { return !findChild(list, "trackDragPreview") }, 500)
        mouseRelease(firstArea, firstArea.width / 2 + 20,
                     firstArea.height / 2, Qt.LeftButton)
        var afterOrder = []
        for (row = 0; row < list.count; ++row)
            afterOrder.push(list.trackIdAt(row))
        compare(afterOrder.join("|"), beforeOrder.join("|"))
        compare(list.dragTrackIds.length, 0)
        focusStealer.destroy()
        list.destroy()
    }

    function test_track_list_batches_multi_selection_tag_edits_once() {
        var ids = nativeDropHelper.ensureSortableTracks()
        verify(ids.length >= 2)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        var flushSpy = signalSpyComponent.createObject(
                    testCase, { "target": LibraryModel,
                                "signalName": "flushRequested" })
        verify(flushSpy.valid)
        flushSpy.clear()

        var tag = "One Batch " + Date.now()
        compare(list.applyTagsToTracks([ids[0], ids[1], ids[0], ""], [tag]), 2)
        compare(flushSpy.count, 1,
                "one multi-selection edit must request one persistence flush")
        compare(LibraryModel.data(
                    LibraryModel.index(LibraryModel.indexForTrackId(ids[0]), 0),
                    LibraryModel.TagsRole), [tag])
        compare(LibraryModel.data(
                    LibraryModel.index(LibraryModel.indexForTrackId(ids[1]), 0),
                    LibraryModel.TagsRole), [tag])
        flushSpy.destroy()
        list.destroy()
    }

    function test_z_task5_drag_drop_can_synchronously_remove_its_source_row() {
        nativeDropHelper.ensureSortableTracks()
        var list = trackListComponent.createObject(mainWindow.contentItem, {
            "x": 0,
            "y": 0,
            "width": 700,
            "height": 300
        })
        var target = destructiveTrackDropTargetComponent.createObject(
                    mainWindow.contentItem, { "x": 740, "y": 80 })
        verify(list && target)
        mainWindow.requestActivate()
        tryVerify(function() { return mainWindow.active }, 1000)
        list.positionViewAtBeginning()
        wait(30)
        var firstRow = list.itemAtIndex(0)
        verify(firstRow)
        var draggedId = firstRow.trackId
        var area = findChild(firstRow, "trackRowDragArea")
        var proxy = findChild(list, "trackDragProxy")
        var dropArea = findChild(target, "destructiveTrackDropArea")
        verify(area && proxy && dropArea)
        var targetPoint = dropArea.mapToItem(area, dropArea.width / 2,
                                             dropArea.height / 2)
        mousePress(area, area.width / 2, area.height / 2, Qt.LeftButton)
        mouseMove(area, area.width / 2 + 20, area.height / 2,
                  20, Qt.LeftButton)
        tryVerify(function() { return proxy.Drag.active }, 500)
        mouseMove(area, targetPoint.x, targetPoint.y, 60, Qt.LeftButton)
        tryVerify(function() { return dropArea.containsDrag }, 500)
        mouseRelease(area, targetPoint.x, targetPoint.y, Qt.LeftButton)
        tryCompare(target, "dropCount", 1, 500)
        compare(LibraryModel.indexForTrackId(draggedId), -1)
        tryVerify(function() { return !list.dragSessionActive }, 500)
        compare(list.dragTrackIds.length, 0)
        tryVerify(function() {
            return !findChild(list, "trackDragPreview")
        }, 500)
        target.destroy()
        list.destroy()
    }

    function test_z_task5_list_window_drag_host_preserves_page_stack() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        nativeDropHelper.ensureSortableTracks()
        var filter = findChild(mainWindow, "filterModel")
        filter.category = "all"
        filter.tagKey = ""
        filter.resourceFolder = ""
        filter.searchText = ""

        var window = listWindowComponent.createObject(null, {
            "filterModel": filter,
            "width": 1400,
            "height": 620
        })
        verify(window)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)
        var list = findChild(window, "sharedTrackList")
        var proxy = findChild(list, "trackDragProxy")
        verify(list && proxy)
        tryVerify(function() { return list.visible && list.count > 0 }, 500)

        var pageStack = list.parent
        while (pageStack && pageStack.currentIndex === undefined)
            pageStack = pageStack.parent
        verify(pageStack, "the shared track list must remain inside its page stack")
        var stackCountBefore = pageStack.count
        var stackIndexBefore = pageStack.currentIndex

        list.positionViewAtBeginning()
        var row = null
        tryVerify(function() {
            row = list.itemAtIndex(0)
            return row !== null
        }, 500)
        var area = findChild(row, "trackRowDragArea")
        verify(area)
        mousePress(area, area.width / 2, area.height / 2, Qt.LeftButton)
        mouseMove(area, area.width / 2 + 20, area.height / 2,
                  20, Qt.LeftButton)
        wait(50)
        var dragStarted = list.dragSessionActive && proxy.Drag.active
        var proxyBefore = proxy.mapToItem(window.contentItem, 0, 0)
        mouseMove(area, area.width / 2 + 100, area.height / 2,
                  40, Qt.LeftButton)
        wait(50)
        var proxyAfter = proxy.mapToItem(window.contentItem, 0, 0)
        var dragFollowed = Math.abs(proxyAfter.x - proxyBefore.x) > 40
        var stackCountAfter = pageStack.count
        var stackIndexAfter = pageStack.currentIndex

        list.cancelTrackDrag()
        mouseRelease(area, area.width / 2 + 100, area.height / 2,
                     Qt.LeftButton)
        window.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled

        verify(stackCountBefore === 3 && stackCountAfter === 3
               && stackIndexBefore === 0 && stackIndexAfter === 0
               && dragStarted && dragFollowed,
               "ListWindow drag input must preserve the three page stack and "
               + "track the pointer: count=" + stackCountBefore + "->"
               + stackCountAfter + ", index=" + stackIndexBefore + "->"
               + stackIndexAfter + ", started=" + dragStarted
               + ", followed=" + dragFollowed)
    }

    function test_z_task5_scrolled_visible_row_starts_window_drag() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var model = createIsolatedTrackModel("drag-scroll-", 30)
        var host = trackListHostComponent.createObject(mainWindow.contentItem)
        var list = trackListComponent.createObject(host, {
            "width": host.width,
            "height": 160,
            "trackModel": model
        })
        verify(model && host && list)
        mainWindow.requestActivate()
        tryCompare(list, "count", 30, 500)
        wait(30)
        list.positionViewAtIndex(20, ListView.Beginning)
        tryVerify(function() {
            return list.contentY > list.rowHeight * 10
        }, 500, "the list must actually be scrolled before starting the drag")
        var visibleRow = list.itemAtIndex(20)
        var area = findChild(visibleRow, "trackRowDragArea")
        var proxy = findChild(list, "trackDragProxy")
        verify(visibleRow && area && proxy)
        var viewportPoint = area.mapToItem(
                    list, area.width / 2, area.height / 2)
        var rowAtViewportPoint = list.dragRowAtViewportPoint(viewportPoint)
        verify(rowAtViewportPoint,
               "viewport point=" + viewportPoint.x + "," + viewportPoint.y
               + " contentY=" + list.contentY + " rowY=" + visibleRow.y)
        compare(rowAtViewportPoint.trackId, "drag-scroll-20")

        mousePress(area, area.width / 2, area.height / 2, Qt.LeftButton)
        mouseMove(area, area.width / 2 + 20, area.height / 2,
                  20, Qt.LeftButton)
        tryVerify(function() {
            return list.dragSessionActive && proxy.Drag.active
        }, 500, "a visible non-first-screen row must start the root drag")
        compare(list.draggedTrackId, "drag-scroll-20",
                "the drag must start from the visible row under the pointer")
        var proxyViewport = proxy.mapToItem(list, 0, 0)
        compare(Math.round(proxyViewport.x), Math.round(viewportPoint.x),
                "the Overlay proxy origin must use the HandlerPoint viewport x")
        compare(Math.round(proxyViewport.y), Math.round(viewportPoint.y),
                "the Overlay proxy origin must use the HandlerPoint viewport y")

        list.cancelTrackDrag()
        mouseRelease(host, host.width / 2, host.height / 2, Qt.LeftButton)
        list.destroy()
        host.destroy()
        model.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task5_scrolled_visible_rows_reorder_locally() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        nativeDropHelper.ensureSortableTracks()
        nativeDropHelper.ensureLongTitleTrack()

        var host = trackListHostComponent.createObject(mainWindow.contentItem, {
            "height": 130
        })
        var list = trackListComponent.createObject(host, {
            "width": host.width,
            "height": host.height
        })
        verify(host && list)
        tryVerify(function() { return list.count >= 4 }, 500)
        list.positionViewAtEnd()
        tryVerify(function() { return list.contentY > 0 }, 500,
                  "the local reorder must exercise a scrolled viewport")
        var fromIndex = list.count - 1
        var targetIndex = fromIndex - 1
        var fromRow = list.itemAtIndex(fromIndex)
        var targetRow = list.itemAtIndex(targetIndex)
        verify(fromRow && targetRow)
        var draggedId = fromRow.trackId
        var targetId = targetRow.trackId
        var area = findChild(fromRow, "trackRowDragArea")
        verify(area)
        var targetPoint = targetRow.mapToItem(
                    area, targetRow.width / 2, targetRow.height / 2)
        var targetViewport = targetRow.mapToItem(
                    list, targetRow.width / 2, targetRow.height / 2)
        verify(list.isLocalTrackReorderPoint(targetViewport),
               "the release row center must be a valid local reorder point")

        var proxy = findChild(list, "trackDragProxy")
        verify(proxy)
        mousePress(area, area.width / 2, area.height / 2, Qt.LeftButton)
        mouseMove(area, area.width / 2 + 20, area.height / 2,
                  20, Qt.LeftButton)
        tryVerify(function() {
            return list.dragSessionActive && proxy.Drag.active
        }, 500)
        mouseMove(area, targetPoint.x, targetPoint.y, 60, Qt.LeftButton)
        mouseRelease(area, targetPoint.x, targetPoint.y, Qt.LeftButton)
        tryVerify(function() { return !list.dragSessionActive }, 500)

        tryVerify(function() {
            return LibraryModel.indexForTrackId(draggedId) === targetIndex
                    && LibraryModel.indexForTrackId(targetId) === fromIndex
        }, 500, "a valid local release must reorder within a scrolled list")
        LibraryModel.reorderTracks([targetId], draggedId)
        list.destroy()
        host.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task5_drag_session_survives_delegate_pool_and_cleans_on_focus_loss() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var model = createIsolatedTrackModel("drag-pool-", 30)
        var host = trackListHostComponent.createObject(mainWindow.contentItem)
        var list = trackListComponent.createObject(host, {
            "width": host.width,
            "height": 160,
            "trackModel": model
        })
        verify(model && host && list)
        mainWindow.requestActivate()
        list.positionViewAtBeginning()
        wait(30)
        var firstRow = list.itemAtIndex(0)
        var proxy = findChild(list, "trackDragProxy")
        var firstArea = findChild(firstRow, "trackRowDragArea")
        verify(firstRow && firstArea && proxy)
        mousePress(firstArea, firstArea.width / 2,
                   firstArea.height / 2, Qt.LeftButton)
        mouseMove(firstArea, firstArea.width / 2 + 20,
                  firstArea.height / 2, 20, Qt.LeftButton)
        tryVerify(function() {
            return list.dragSessionActive && proxy.Drag.active
        }, 500)
        list.currentIndex = -1
        list.positionViewAtEnd()
        wait(100)
        verify(list.dragSessionActive && proxy.Drag.active,
               "delegate pooling must not terminate a window-owned drag")
        compare(list.dragTrackIds.length, 1)
        verify(findChild(list, "trackDragPreview"),
               "the overlay preview must survive delegate pooling")
        var pooledPreview = findChild(list, "trackDragPreview")
        var pooledPreviewBefore = pooledPreview.mapToItem(
                    mainWindow.contentItem, 0, 0)
        mouseMove(host, host.width - 20, host.height / 2,
                  30, Qt.LeftButton)
        tryVerify(function() {
            var moved = pooledPreview.mapToItem(mainWindow.contentItem, 0, 0)
            return Math.abs(moved.x - pooledPreviewBefore.x) > 20
                    || Math.abs(moved.y - pooledPreviewBefore.y) > 20
        }, 500, "the window-owned drag must keep tracking after pooling")
        list.cancelTrackDrag()
        mouseRelease(host, host.width - 20, host.height / 2, Qt.LeftButton)
        tryVerify(function() { return !list.dragSessionActive }, 500)
        list.destroy()
        host.destroy()
        model.destroy()

        nativeDropHelper.ensureSortableTracks()
        var dragWindow = trackListWindowComponent.createObject(null)
        verify(dragWindow)
        dragWindow.requestActivate()
        tryVerify(function() { return dragWindow.active }, 1000)
        var windowList = dragWindow.list
        windowList.positionViewAtBeginning()
        var windowRow = null
        tryVerify(function() {
            windowRow = windowList.itemAtIndex(0)
            return windowRow !== null
        }, 500)
        var windowArea = findChild(windowRow, "trackRowDragArea")
        var windowProxy = findChild(windowList, "trackDragProxy")
        verify(windowArea && windowProxy)
        mousePress(windowArea, windowArea.width / 2,
                   windowArea.height / 2, Qt.LeftButton)
        mouseMove(windowArea, windowArea.width / 2 + 20,
                  windowArea.height / 2, 20, Qt.LeftButton)
        tryVerify(function() { return windowProxy.Drag.active }, 500)
        mainWindow.requestActivate()
        tryVerify(function() { return !dragWindow.active }, 1000)
        tryVerify(function() { return !windowList.dragSessionActive }, 500)
        compare(windowList.dragTrackIds.length, 0)
        mouseRelease(dragWindow, dragWindow.width / 2,
                     dragWindow.height / 2, Qt.LeftButton)
        dragWindow.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task5_resource_folder_drop_and_remove_preserve_disk_files() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filterModel,
            "width": 1400,
            "height": 620
        })
        verify(window)
        window.requestActivate()
        tryVerify(function() { return window.active }, 1000)
        verify(nativeDropHelper.registerListDropWindow(window),
               "the production NativeDropRouter must be attached as Target::List")

        var navigation = findChild(window, "referenceSideNavigation")
        var navigationList = findChild(navigation, "libraryNavigationList")
        var centerTarget = findChild(window, "sharedTrackList")
        var tagTarget = findChild(window, "tagManagementPanel")
        verify(navigation && navigationList && centerTarget && tagTarget)
        navigationList.positionViewAtEnd()
        wait(0)
        var dropTarget = findChild(navigation, "resourceFolderDropTarget")
        verify(dropTarget)

        var copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio)
        var ignoredFile = nativeDropHelper.createNonAudioDropFile()
        var invalidUrl = nativeDropHelper.missingDropUrl()
        verify(ignoredFile && invalidUrl)
        var initialFolderCount = ResourceFolderController.monitoredFolders.length
        var initialLibraryCount = LibraryModel.count

        var centralFolder = nativeDropHelper.createDropDirectory()
        verify(nativeDropHelper.sendUrls(centerTarget, [centralFolder]))
        tryVerify(function() { return !ImportController.busy }, 3000)
        compare(ResourceFolderController.monitoredFolders.length,
                initialFolderCount,
                "a central directory drop must not become a monitored root")
        ImportController.clearErrors()

        var tagFolder = nativeDropHelper.createDropDirectory()
        verify(nativeDropHelper.sendUrls(tagTarget, [tagFolder]))
        tryVerify(function() { return !ImportController.busy }, 3000)
        compare(ResourceFolderController.monitoredFolders.length,
                initialFolderCount,
                "a tag-column directory drop must not become a monitored root")
        ImportController.clearErrors()

        var folderUrl = nativeDropHelper.createDropDirectory()
        verify(folderUrl && nativeDropHelper.pathExists(folderUrl))
        var importFinished = signalSpyComponent.createObject(testCase, {
            "target": ImportController,
            "signalName": "finished"
        })
        var rootsChanged = signalSpyComponent.createObject(testCase, {
            "target": ResourceFolderController,
            "signalName": "monitoredFoldersChanged"
        })
        verify(importFinished && rootsChanged)
        compare(ImportController.busy, false,
                "resource audio precondition requires an idle importer")
        compare(window.importBatchActive, false,
                "an earlier completed list drop must release its batch state")
        compare(ResourceFolderController.classifyDropUrl(copiedAudio).kind,
                ResourceFolderController.AudioFile,
                "the local file must classify as supported audio")
        var audioAccepted = nativeDropHelper.sendUrls(dropTarget,
                                                       [copiedAudio])
        compare(audioAccepted, true,
                "the resource area must accept a local audio file")
        tryCompare(importFinished, "count", 1, 5000)
        tryVerify(function() { return !ImportController.busy }, 5000)
        var audioOnlyLibraryCount = LibraryModel.count
        verify(audioOnlyLibraryCount > initialLibraryCount,
               "a resource-area audio drop must enter the music library")
        for (var importedId of ImportController.importedTrackIds)
            LibraryModel.removeTrack(importedId)
        tryCompare(LibraryModel, "count", initialLibraryCount, 1000)
        importFinished.clear()

        verify(nativeDropHelper.sendUrls(dropTarget,
                                         [folderUrl, folderUrl,
                                          copiedAudio, copiedAudio,
                                          ignoredFile, invalidUrl]))
        tryVerify(function() {
            return ResourceFolderController.monitoredFolders.length
                    === initialFolderCount + 1
        }, 1000)
        tryCompare(importFinished, "count", 1, 5000)
        tryVerify(function() { return !ImportController.busy }, 5000)
        compare(rootsChanged.count, 1,
                "duplicate directory URLs must add one monitored root")
        verify(ImportController.errors.length === 0)
        verify(LibraryModel.count > initialLibraryCount,
               "a mixed resource drop must import its audio files")
        for (importedId of ImportController.importedTrackIds)
            LibraryModel.removeTrack(importedId)
        tryCompare(LibraryModel, "count", initialLibraryCount, 1000)
        var folderPath = nativeDropHelper.localFilePath(folderUrl)
        folderPath = folderPath.replace(/\\/g, "/")
        var rootNode = null
        for (var row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
            var index = LibraryNavigationModel.index(row, 0)
            if (LibraryNavigationModel.data(index,
                    LibraryNavigationModel.ResourceFolderRole)
                    .replace(/\\/g, "/").toLowerCase()
                    === folderPath.toLowerCase()) {
                rootNode = findChild(navigation, "navigationNode-"
                    + LibraryNavigationModel.data(index,
                        LibraryNavigationModel.NodeIdRole))
                break
            }
        }
        verify(rootNode)

        var resourceSection = findChild(navigation, "resourceFolderSection")
        verify(resourceSection)
        verify(resourceSection.y < rootNode.y,
               "the resource heading must precede its directory roots")
        var rootPoint = rootNode.mapToItem(navigation,
                                          rootNode.width / 2,
                                          rootNode.height / 2)
        verify(navigation.resourceDropContainsPoint(rootPoint.x, rootPoint.y),
               "the whole resource tree must accept external folder drops")
        var libraryNode = findChild(navigation,
                                    "navigationNode-library:all")
        verify(libraryNode)
        var libraryPoint = libraryNode.mapToItem(navigation,
                                                 libraryNode.width / 2,
                                                 libraryNode.height / 2)
        verify(!navigation.resourceDropContainsPoint(libraryPoint.x,
                                                     libraryPoint.y),
               "library and playlist rows are not disk resource targets")

        var secondFolderUrl = nativeDropHelper.createDropDirectory()
        verify(secondFolderUrl && nativeDropHelper.pathExists(secondFolderUrl))
        nativeDropHelper.sendUrls(dropTarget, [secondFolderUrl])
        tryVerify(function() {
            return ResourceFolderController.monitoredFolders.length
                    === initialFolderCount + 2
        }, 1000)
        var secondFolderPath = nativeDropHelper.localFilePath(secondFolderUrl).replace(/\\/g, "/")
        rootNode = null
        var secondRootNode = null
        for (row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
            index = LibraryNavigationModel.index(row, 0)
            var candidatePath = LibraryNavigationModel.data(index,
                    LibraryNavigationModel.ResourceFolderRole)
                    .replace(/\\/g, "/").toLowerCase()
            var candidateNode = findChild(navigation, "navigationNode-"
                    + LibraryNavigationModel.data(index,
                        LibraryNavigationModel.NodeIdRole))
            if (candidatePath === folderPath.toLowerCase())
                rootNode = candidateNode
            else if (candidatePath === secondFolderPath.toLowerCase())
                secondRootNode = candidateNode
        }
        verify(rootNode)
        verify(secondRootNode)

        mouseClick(rootNode, rootNode.width / 2, rootNode.height / 2,
                   Qt.RightButton)
        var menu = findChild(navigation, "resourceFolderContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        var rescanAction = findChild(menu, "resourceFolderMenuRescan")
        compare(rescanAction.text, "重新扫描全部资源文件夹")
        menu.close()

        mouseClick(secondRootNode, secondRootNode.width / 2,
                   secondRootNode.height / 2, Qt.LeftButton)
        var removeButton = findChild(navigation, "removeResourceFolderButton")
        tryVerify(function() { return removeButton.enabled }, 500)
        mouseClick(removeButton, removeButton.width / 2,
                   removeButton.height / 2)
        var confirm = findChild(navigation, "removeResourceFolderDialog")
        tryVerify(function() { return confirm && confirm.visible }, 500)
        compare(navigation.pendingResourceFolderRemoval.replace(/\\/g, "/")
                .toLowerCase(), secondFolderPath.toLowerCase())
        navigation.selectedResourceFolder = folderPath
        navigation.activeNodeType = "resourceRoot"
        confirm.accept()
        tryVerify(function() {
            return ResourceFolderController.monitoredFolders.length
                    === initialFolderCount + 1
        }, 1000)
        verify(nativeDropHelper.pathExists(secondFolderUrl))
        verify(ResourceFolderController.monitoredFolders.some(function(path) {
            return path.replace(/\\/g, "/").toLowerCase()
                    === folderPath.toLowerCase()
        }), "stale right-click context must not remove the selected root")

        rootNode = null
        for (row = 0; row < LibraryNavigationModel.rowCount(); ++row) {
            index = LibraryNavigationModel.index(row, 0)
            if (LibraryNavigationModel.data(index,
                    LibraryNavigationModel.ResourceFolderRole)
                    .replace(/\\/g, "/").toLowerCase()
                    === folderPath.toLowerCase()) {
                rootNode = findChild(navigation, "navigationNode-"
                    + LibraryNavigationModel.data(index,
                        LibraryNavigationModel.NodeIdRole))
                break
            }
        }
        verify(rootNode)
        mouseClick(rootNode, rootNode.width / 2, rootNode.height / 2,
                   Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        wait(500)
        tryVerify(function() { return !ResourceFolderController.scanning }, 3000)
        var scanFinished = signalSpyComponent.createObject(testCase, {
            "target": ResourceFolderController,
            "signalName": "scanFinished"
        })
        verify(scanFinished)
        scanFinished.clear()
        verify(rescanAction && rescanAction.enabled)
        mouseClick(rescanAction, rescanAction.width / 2,
                   rescanAction.height / 2)
        tryCompare(scanFinished, "count", 1, 3000)

        mouseClick(rootNode, rootNode.width / 2, rootNode.height / 2,
                   Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        var removeAction = findChild(menu, "resourceFolderMenuRemove")
        mouseClick(removeAction, removeAction.width / 2,
                   removeAction.height / 2)
        tryVerify(function() { return confirm && confirm.visible }, 500)
        var warning = findChild(confirm, "removeResourceFolderWarning")
        verify(warning.text.indexOf("不删除电脑磁盘中的实际文件夹和音乐文件") >= 0)
        confirm.accept()
        tryVerify(function() {
            return ResourceFolderController.monitoredFolders.length
                    === initialFolderCount
        }, 1000)
        verify(nativeDropHelper.pathExists(folderUrl))
        scanFinished.destroy()
        importFinished.destroy()
        rootsChanged.destroy()
        window.destroy()
    }

    function test_z_list_waveforms_only_exist_for_visible_rows_when_enabled() {
        nativeDropHelper.ensureSortableTracks()
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        var previousMode = SettingsController.listWaveformThumbnailMode
        SettingsController.listWaveformThumbnailEnabled = false
        // QtTest destroys this overlay even when a check fails, so later
        // pointer tests cannot be intercepted by a leaked list.
        var list = createTemporaryObject(trackListComponent, mainWindow.contentItem)
        verify(list)
        tryVerify(function() { return list.count > 0 })
        compare(list.rowHeight, Theme.listRowHeight)
        compare(list.thumbnailItemCount, 0)
        verify(!findChild(list, "trackWaveformThumbnail"))

        TrackWaveformThumbnailProvider.refresh()
        var readsBefore = TrackWaveformThumbnailProvider.diagnostics().cacheReadAttempts
        SettingsController.listWaveformThumbnailEnabled = true
        tryCompare(list, "rowHeight", Theme.mediaListRowHeight)
        tryVerify(function() { return list.thumbnailItemCount > 0 })
        verify(findChild(list.itemAtIndex(0), "trackWaveformThumbnail"))
        compare(findChild(list.itemAtIndex(0), "trackCover").width, 34)
        compare(findChild(list.itemAtIndex(0), "trackCover").height, 34)
        tryVerify(function() {
            return TrackWaveformThumbnailProvider.diagnostics().cacheReadAttempts
                    > readsBefore
        }, 3000)
        verify(waitForRendering(list))
        tryVerify(function() {
            return TrackWaveformThumbnailProvider.diagnostics().inFlightTracks === 0
        }, 5000)
        var settledReads = TrackWaveformThumbnailProvider.diagnostics().cacheReadAttempts
        SettingsController.listWaveformThumbnailMode = "Mono"
        wait(100)
        compare(TrackWaveformThumbnailProvider.diagnostics().cacheReadAttempts,
                settledReads,
                "mode changes must recolor without reading waveform data again")

        SettingsController.listWaveformThumbnailEnabled = false
        tryCompare(list, "rowHeight", Theme.listRowHeight)
        tryCompare(list, "thumbnailItemCount", 0)
        verify(!findChild(list, "trackWaveformThumbnail"))
        list.destroy()
        SettingsController.listWaveformThumbnailMode = previousMode
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task6_large_models_keep_thumbnail_requests_visible_bounded() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = true

        function verifyBound(rowCount) {
            var provider = fakeThumbnailProviderComponent.createObject(testCase)
            var model = createIsolatedTrackModel("task6-" + rowCount + "-",
                                                 rowCount)
            var host = trackListHostComponent.createObject(mainWindow.contentItem)
            var list = null
            provider.requestObserver = function(trackId) {
                var row = list
                        ? trackRowForId(list.contentItem, trackId) : null
                var thumbnail = row
                        ? findChild(row, "trackWaveformThumbnail") : null
                var loader = thumbnail ? thumbnail.parent : null
                return {
                    "hasDelegate": !!row,
                    "hasLoader": !!loader,
                    "loaderEnabled": !!(loader && loader.active),
                    "loaderHasItem": !!(loader && loader.item),
                    "loaderItemTrackId": loader && loader.item
                            ? loader.item.trackId : "",
                    "loaderActive": !!(loader && loader.active && loader.item
                                         && loader.item.trackId === trackId),
                    "rowTop": row ? row.y : Number.NaN,
                    "rowBottom": row ? row.y + row.height : Number.NaN,
                    "viewportTop": list ? list.contentY
                            + (list.headerItem ? list.headerItem.height : 0)
                                         : Number.NaN,
                    "viewportBottom": list
                            ? list.contentY + list.height : Number.NaN
                }
            }
            list = trackListComponent.createObject(host, {
                "width": host.width,
                "height": host.height,
                "trackModel": model,
                "thumbnailProvider": provider
            })
            verify(provider && model && host && list)
            tryCompare(list, "count", rowCount, 5000)
            tryVerify(function() {
                return list.thumbnailItemCount > 0
                        && provider.requestCount > 0
            }, 3000)
            wait(25)

            var requestBoundPerViewport = 32
            verifyNewThumbnailRequestsAreVisible(
                        provider, 0,
                        rowCount + "-row initial viewport")
            verify(list.thumbnailItemCount <= requestBoundPerViewport)
            verify(provider.requestCount <= requestBoundPerViewport,
                   rowCount + " rows must not request non-visible thumbnails")

            var beforeMiddle = provider.requestCount
            list.positionViewAtIndex(Math.floor(rowCount / 2), ListView.Beginning)
            tryVerify(function() {
                return provider.requestCount > beforeMiddle
            }, 3000)
            wait(25)
            verifyNewThumbnailRequestsAreVisible(
                        provider, beforeMiddle,
                        rowCount + "-row middle viewport")
            verify(provider.requestCount - beforeMiddle
                   <= requestBoundPerViewport,
                   rowCount + "-row middle scroll exceeded one viewport")
            verify(list.thumbnailItemCount <= requestBoundPerViewport)

            var beforeEnd = provider.requestCount
            list.positionViewAtEnd()
            tryVerify(function() { return provider.requestCount > beforeEnd },
                      3000)
            wait(25)
            verifyNewThumbnailRequestsAreVisible(
                        provider, beforeEnd,
                        rowCount + "-row end viewport")
            verify(provider.requestCount - beforeEnd
                   <= requestBoundPerViewport,
                   rowCount + "-row end scroll exceeded one viewport")
            verify(list.thumbnailItemCount <= requestBoundPerViewport)
            verify(provider.requestCount <= requestBoundPerViewport * 3,
                   "three viewports must stay independent of logical row count")

            host.destroy()
            model.destroy()
            provider.destroy()
        }

        verifyBound(1000)
        verifyBound(10000)
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task6_filter_switching_reuses_one_shared_track_list() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var filter = findChild(mainWindow, "filterModel")
        var window = listWindowComponent.createObject(null, {
            "filterModel": filter,
            "width": 1400,
            "height": 620
        })
        verify(window && filter)
        var workspace = findChild(window, "listWorkspace")
        var shared = findChild(workspace, "sharedTrackList")
        verify(workspace && shared)

        filter.category = "favorites"
        wait(0)
        compare(findChild(workspace, "sharedTrackList"), shared)
        filter.category = "all"
        filter.tagKey = "task6-nonexistent-tag"
        wait(0)
        compare(findChild(workspace, "sharedTrackList"), shared)
        filter.tagKey = ""
        filter.resourceFolder = "C:/task6/nonexistent-folder"
        wait(0)
        compare(findChild(workspace, "sharedTrackList"), shared)
        compare(countObjectsNamed(workspace, "sharedTrackList"), 1)

        window.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task7_tag_page_alone_owns_the_right_panel() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        var task7TrackIds = nativeDropHelper.ensureSortableTracks()
        compare(task7TrackIds.length, 3)
        var filter = findChild(mainWindow, "filterModel")
        filter.category = "all"
        filter.tagKey = ""
        filter.resourceFolder = ""
        filter.searchText = ""
        TagModel.selectedKey = ""
        var window = listWindowComponent.createObject(null, {
            "filterModel": filter,
            "width": 1400,
            "height": 620
        })
        verify(window && filter)
        var workspace = findChild(window, "listWorkspace")
        var navigation = findChild(workspace, "referenceSideNavigation")
        var shared = findChild(workspace, "sharedTrackList")
        var panel = findChild(workspace, "tagManagementPanel")
        var divider = findChild(workspace, "tagPanelDivider")
        verify(workspace && navigation && shared && panel && divider)
        tryVerify(function() { return shared.width > 0 })
        shared.selectedTrackIds = ["task7-retained-selection"]
        var playbackTrackId = PlaybackController.currentTrackId

        compare(panel.visible, false,
                "the normal library page must not reserve a tag column")
        compare(divider.visible, false)
        compare(window.pageMinimumWidth, 956)
        var libraryWidth = shared.width
        var libraryCenterWidth = workspace.centerWidth

        var widthBeforeTags = window.width
        navigation.activateNode("tags", "tags:manage", "")
        tryVerify(function() { return panel.visible && divider.visible })
        compare(window.pageMinimumWidth, 956)
        compare(window.width, widthBeforeTags)
        tryVerify(function() {
            return libraryWidth >= shared.width + workspace.rightColumnWidth
        }, 1000)
        var tagCenterWidth = workspace.centerWidth
        filter.searchText = "task7-retained-search"
        compare(findChild(workspace, "sharedTrackList"), shared)
        compare(shared.selectedTrackIds[0], "task7-retained-selection")
        compare(filter.searchText, "task7-retained-search")
        compare(PlaybackController.currentTrackId, playbackTrackId)

        navigation.activateNode("playlist", "playlist:task7-page", "")
        tryVerify(function() { return !panel.visible && !divider.visible })
        tryVerify(function() {
            return workspace.centerWidth
                    >= tagCenterWidth + workspace.rightColumnWidth
        })
        compare(Math.round(workspace.centerWidth),
                Math.round(libraryCenterWidth))
        compare(findChild(workspace, "sharedTrackList"), shared)

        navigation.activateNode("library", "library:all", "")
        compare(panel.visible, false)
        compare(findChild(workspace, "sharedTrackList"), shared)

        navigation.activateNode("favorites", "favorites", "")
        compare(panel.visible, false)
        compare(divider.visible, false)
        tryVerify(function() {
            return workspace.centerWidth
                    >= tagCenterWidth + workspace.rightColumnWidth
        })
        compare(Math.round(workspace.centerWidth),
                Math.round(libraryCenterWidth))
        compare(findChild(workspace, "sharedTrackList"), shared)
        compare(shared.selectedTrackIds[0], "task7-retained-selection")
        compare(filter.searchText, "task7-retained-search")
        compare(PlaybackController.currentTrackId, playbackTrackId)

        navigation.activateNode("resourceFolder", "resource:task7",
                                "C:/task7/resource")
        compare(panel.visible, false)
        compare(findChild(workspace, "sharedTrackList"), shared)

        navigation.activateNode("tags", "tags:manage", "")
        tryVerify(function() { return panel.visible && divider.visible })
        compare(findChild(workspace, "sharedTrackList"), shared)
        compare(countObjectsNamed(workspace, "sharedTrackList"), 1)
        compare(shared.selectedTrackIds[0], "task7-retained-selection")
        compare(filter.searchText, "task7-retained-search")
        compare(PlaybackController.currentTrackId, playbackTrackId)

        window.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_z_task2_runtime_list_geometry_stays_inside_its_columns() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        var filter = findChild(mainWindow, "filterModel")
        var trackIds = nativeDropHelper.ensureSortableTracks()
        compare(trackIds.length, 3)
        var window = null
        var emptyNavigation = null
        var tagName = "Task2 Runtime Geometry " + Date.now()
        var tagKey = ""
        try {
            filter.category = "all"
            filter.tagKey = ""
            filter.resourceFolder = ""
            filter.searchText = ""
            TagModel.selectedKey = ""
            SettingsController.listWaveformThumbnailEnabled = true

            window = listWindowComponent.createObject(null, {
                "filterModel": filter,
                "width": 1400,
                "height": 720
            })
            verify(window)
            var workspace = findChild(window, "listWorkspace")
            var navigation = findChild(workspace, "referenceSideNavigation")
            var centerColumn = findChild(workspace, "centerTrackColumn")
            var footer = findChild(window, "centerTrackFooter")
            var trackList = findChild(workspace, "sharedTrackList")
            var tagPanel = findChild(workspace, "tagManagementPanel")
            verify(workspace && navigation && centerColumn && footer
                   && trackList && tagPanel)

            navigation.activateNode("tags", "tags:manage", "")
            tryVerify(function() {
                return tagPanel.visible && footer.visible && centerColumn.width > 0
            }, 1000)
            var footerScene = footer.mapToItem(null, 0, 0)
            var centerScene = centerColumn.mapToItem(null, 0, 0)
            var navigationScene = navigation.mapToItem(null, 0, 0)
            var tagPanelScene = tagPanel.mapToItem(null, 0, 0)
            compare(Math.round(footerScene.x), Math.round(centerScene.x),
                    "the filter footer must begin at the center track column")
            compare(Math.round(footer.width), Math.round(centerColumn.width),
                    "the filter footer must exactly match the center track width")
            verify(footerScene.x >= navigationScene.x + navigation.width,
                   "the footer must not cover the 208px sidebar")
            verify(footerScene.x + footer.width <= tagPanelScene.x + 1,
                   "the footer must not cover the 248px tag panel")

            trackList.positionViewAtBeginning()
            var firstRow = null
            tryVerify(function() {
                firstRow = trackList.itemAtIndex(0)
                return firstRow !== null
            }, 1000)
            var title = findChild(firstRow, "trackTitleMarquee")
            var thumbnail = findChild(firstRow, "trackWaveformThumbnailLoader")
            verify(title && thumbnail)
            tryVerify(function() { return thumbnail.active && thumbnail.item }, 1000)
            compare(Math.round(thumbnail.y - (title.y + title.height)), 3,
                    "the waveform thumbnail must sit 2-4px below the title")
            compare(thumbnail.height, 16)

            var cover = findChild(firstRow, "trackCover")
            verify(cover)
            var infoTop = title.mapToItem(firstRow, 0, 0).y
            var infoBottom = thumbnail.mapToItem(firstRow, 0, thumbnail.height).y
            var coverCenter = cover.mapToItem(firstRow, 0, cover.height / 2).y
            verify(Math.abs((infoTop + infoBottom) / 2 - coverCenter) <= 1,
                   "the title and thumbnail block must share the cover's vertical center")

            verify(TagModel.createTag(tagName))
            for (var tagRow = 0; tagRow < TagModel.rowCount(); ++tagRow) {
                var index = TagModel.index(tagRow, 0)
                if (TagModel.data(index, TagModel.DisplayNameRole) === tagName) {
                    tagKey = TagModel.data(index, TagModel.KeyRole)
                    break
                }
            }
            verify(tagKey.length > 0)
            task4TemporaryTagKeys.push(tagKey)
            var tagPill = null
            tryVerify(function() {
                tagPill = findChild(tagPanel, "tagPill-" + tagKey)
                return tagPill !== null
            }, 1000)
            compare(tagPill.height, 24)

            emptyNavigation = emptyLibraryNavigationComponent.createObject(
                        mainWindow.contentItem)
            verify(emptyNavigation)
            var emptyLibraryNode = findChild(emptyNavigation,
                                             "navigationNode-library:all")
            var chevron = findChild(emptyLibraryNode, "navigationExpandButton")
            verify(emptyLibraryNode && chevron)
            tryVerify(function() { return chevron.visible }, 500)
            compare(chevron.width, 28)
            compare(chevron.height, 28)
            compare(chevron.icon.width, 21)
            compare(chevron.icon.height, 21)
        } finally {
            if (emptyNavigation)
                emptyNavigation.destroy()
            if (window)
                window.destroy()
            if (tagKey.length === 0) {
                for (var cleanupRow = 0; cleanupRow < TagModel.rowCount(); ++cleanupRow) {
                    var cleanupCandidate = TagModel.index(cleanupRow, 0)
                    if (TagModel.data(cleanupCandidate, TagModel.DisplayNameRole) === tagName) {
                        tagKey = TagModel.data(cleanupCandidate, TagModel.KeyRole)
                        break
                    }
                }
            }
            if (tagKey.length > 0)
                TagModel.removeTag(tagKey)
            for (var cleanupIndex = task4TemporaryTagKeys.length - 1;
                 cleanupIndex >= 0; --cleanupIndex) {
                if (task4TemporaryTagKeys[cleanupIndex] === tagKey)
                    task4TemporaryTagKeys.splice(cleanupIndex, 1)
            }
            filter.tagKey = ""
            TagModel.selectedKey = ""
            SettingsController.listWaveformThumbnailEnabled = previousEnabled
        }
    }

    function test_z_thumbnail_deferred_request_dies_with_its_wrapper() {
        var provider = fakeThumbnailProviderComponent.createObject(testCase)
        var host = deferredThumbnailDestroyHostComponent.createObject(
                    mainWindow.contentItem, {
                        "thumbnailProvider": provider
                    })
        verify(provider && host && host.wrapper)
        wait(0)
        compare(provider.requestCount, 0)
        compare(provider.cancelCount, 0)

        host.deactivateBeforeNextRequest()
        wait(0)
        compare(host.wrapper, null)
        compare(provider.requestCount, 0,
                "a destroyed wrapper must not dispatch its deferred request")
        compare(provider.cancelCount, 0,
                "no request means there is nothing to cancel")

        host.destroy()
        provider.destroy()
    }

    function test_z_thumbnail_wrapper_cancels_and_rejects_stale_generations() {
        var provider = fakeThumbnailProviderComponent.createObject(testCase)
        var wrapper = trackWaveformThumbnailComponent.createObject(
                    mainWindow.contentItem, {
                        "provider": provider,
                        "trackId": "track-a",
                        "sourcePath": "a.wav",
                        "delegateGeneration": 11,
                        "mode": "Spectral"
                    })
        verify(provider && wrapper)
        tryCompare(provider, "requestCount", 1)
        compare(provider.lastTrackId, "track-a")
        compare(provider.lastSourcePath, "a.wav")
        compare(provider.lastGeneration, 11)

        wrapper.trackId = "track-b"
        wrapper.sourcePath = "b.wav"
        wrapper.delegateGeneration = 12
        tryCompare(provider, "requestCount", 2)
        compare(provider.cancelCount, 1)

        provider.thumbnailReady("track-a", 11, "stale")
        compare(wrapper.waveformPeaks, "")
        provider.thumbnailReady("track-b", 12, "current")
        compare(wrapper.waveformPeaks, "current")

        var settledRequests = provider.requestCount
        wrapper.mode = "Mono"
        wait(50)
        compare(provider.requestCount, settledRequests,
                "mode changes must not request waveform data again")

        wrapper.destroy()
        tryCompare(provider, "cancelCount", 2)
        provider.destroy()
    }

    function test_z_thumbnail_brightness_recolors_without_cache_read() {
        var previousBrightness = SettingsController.trackWaveformBrightness
        var provider = fakeThumbnailProviderComponent.createObject(testCase)
        SettingsController.trackWaveformBrightness = 0.66
        var wrapper = trackWaveformThumbnailComponent.createObject(
                    mainWindow.contentItem, {
                        "provider": provider,
                        "trackId": "brightness-track",
                        "sourcePath": "brightness.wav",
                        "delegateGeneration": 21
                    })
        verify(provider && wrapper)
        tryCompare(provider, "requestCount", 1)
        var itemLoader = findChild(wrapper, "trackWaveformThumbnailItemLoader")
        verify(itemLoader)
        tryVerify(function() { return itemLoader.item !== null })
        compare(wrapper.brightness, 0.66)
        compare(itemLoader.opacity, 0.66)

        var settledRequests = provider.requestCount
        SettingsController.trackWaveformBrightness = 0.42
        tryCompare(wrapper, "brightness", 0.42)
        tryCompare(itemLoader, "opacity", 0.42)
        compare(provider.requestCount, settledRequests,
                "brightness changes must reuse the loaded waveform")

        wrapper.destroy()
        provider.destroy()
        SettingsController.trackWaveformBrightness = previousBrightness
    }

    function test_z_thumbnail_mono_uses_adaptive_gray_white_without_changing_spectral_palette() {
        var previousTheme = SettingsController.themeMode
        var provider = fakeThumbnailProviderComponent.createObject(testCase)
        var wrapper = trackWaveformThumbnailComponent.createObject(
                    mainWindow.contentItem, {
                        "provider": provider,
                        "trackId": "mono-palette-track",
                        "sourcePath": "mono-palette.wav",
                        "delegateGeneration": 1,
                        "mode": "Mono"
                    })
        verify(wrapper)
        var itemLoader = findChild(wrapper, "trackWaveformThumbnailItemLoader")
        tryVerify(function() { return itemLoader && itemLoader.item !== null })

        SettingsController.themeMode = 1
        tryCompare(Theme, "isLight", true)
        compare(itemLoader.item.waveformColor.toString(), "#8a9099")
        var low = String(itemLoader.item.lowColor)
        var mid = String(itemLoader.item.midColor)
        var high = String(itemLoader.item.highColor)

        wrapper.mode = "Spectral"
        wait(0)
        compare(String(itemLoader.item.lowColor), low)
        compare(String(itemLoader.item.midColor), mid)
        compare(String(itemLoader.item.highColor), high)

        SettingsController.themeMode = 0
        tryCompare(Theme, "isLight", false)
        wrapper.mode = "Mono"
        compare(itemLoader.item.waveformColor.toString(), "#d5d8de")

        wrapper.destroy()
        provider.destroy()
        SettingsController.themeMode = previousTheme
    }

    function test_z_visible_thumbnail_retries_only_matching_cache_ready_source() {
        var provider = fakeThumbnailProviderComponent.createObject(testCase)
        var wrapper = trackWaveformThumbnailComponent.createObject(
                    mainWindow.contentItem, {
                        "provider": provider,
                        "trackId": "cache-transition-track",
                        "sourcePath": "cache-transition.wav",
                        "delegateGeneration": 31
                    })
        verify(provider && wrapper)
        tryCompare(provider, "requestCount", 1)
        provider.thumbnailReady("cache-transition-track", 31, "")
        compare(wrapper.waveformPeaks, "")

        provider.sourceCacheInvalidated("different.wav")
        wait(20)
        compare(provider.requestCount, 1,
                "an unrelated cache write must not refresh this wrapper")

        provider.sourceCacheInvalidated("cache-transition.wav")
        tryCompare(provider, "requestCount", 2)
        var readyPeaks = Array(129).join("x")
        compare(readyPeaks.length, 128)
        provider.thumbnailReady("cache-transition-track", 31, readyPeaks)
        compare(wrapper.waveformPeaks.length, 128)

        wrapper.enabled = false
        wait(0)
        var hiddenRequestCount = provider.requestCount
        provider.sourceCacheInvalidated("cache-transition.wav")
        wait(20)
        compare(provider.requestCount, hiddenRequestCount,
                "a hidden wrapper must remain request-free")

        wrapper.destroy()
        provider.destroy()
    }

    function test_z_thumbnail_requests_follow_pool_reuse_and_host_visibility() {
        SettingsController.listWaveformThumbnailEnabled = true
        var provider = fakeThumbnailProviderComponent.createObject(testCase)
        var model = createIsolatedTrackModel("pool-track-", 24)
        var host = trackListHostComponent.createObject(mainWindow.contentItem)
        var list = trackListComponent.createObject(host, {
            "width": host.width,
            "height": host.height,
            "trackModel": model,
            "thumbnailProvider": provider
        })
        verify(provider && model && host && list)
        tryVerify(function() { return provider.requestCount > 0 })
        var firstRow = list.itemAtIndex(0)
        verify(firstRow && findChild(firstRow, "trackWaveformThumbnail"))
        var cancellationsBeforeHeaderCover = provider.cancelCount
        list.contentY = list.rowHeight
        tryVerify(function() {
            var coveredRow = list.itemAtIndex(0)
            return coveredRow
                    && !findChild(coveredRow, "trackWaveformThumbnail")
                    && provider.cancelCount > cancellationsBeforeHeaderCover
        }, 1000)
        list.positionViewAtBeginning()
        tryVerify(function() {
            var returnedRow = list.itemAtIndex(0)
            return returnedRow
                    && findChild(returnedRow, "trackWaveformThumbnail")
        })

        var requestsAtBeginning = provider.requestCount
        var initialGenerations = ({})
        for (var initialIndex = 0;
             initialIndex < provider.requests.length; ++initialIndex) {
            var initialRequest = provider.requests[initialIndex]
            initialGenerations[initialRequest.trackId] =
                    initialRequest.generation
        }

        list.positionViewAtEnd()
        tryVerify(function() {
            return provider.cancelCount > 0
                    && provider.requestCount > requestsAtBeginning
        })
        var canceledInitialGenerations = ({})
        for (var cancellationIndex = 0;
             cancellationIndex < provider.cancellations.length;
             ++cancellationIndex) {
            var cancellation = provider.cancellations[cancellationIndex]
            if (initialGenerations[cancellation.trackId]
                    === cancellation.generation) {
                canceledInitialGenerations[cancellation.trackId] =
                        cancellation.generation
            }
        }
        var requestsAtEnd = provider.requestCount
        list.positionViewAtBeginning()
        tryVerify(function() { return provider.requestCount > requestsAtEnd })
        var reusedSafely = false
        for (var requestIndex = requestsAtBeginning;
             requestIndex < provider.requests.length; ++requestIndex) {
            var request = provider.requests[requestIndex]
            if (canceledInitialGenerations[request.trackId] !== undefined
                    && request.generation
                       > canceledInitialGenerations[request.trackId]) {
                reusedSafely = true
                break
            }
        }
        verify(reusedSafely,
               "a pooled delegate must request a returning track with a new generation")

        var cancellationsBeforeHide = provider.cancelCount
        host.visible = false
        tryCompare(list, "thumbnailItemCount", 0)
        tryVerify(function() {
            return provider.cancelCount > cancellationsBeforeHide
        })

        host.destroy()
        model.destroy()
        provider.destroy()
    }

    function test_z_thumbnail_requests_cancel_for_stack_and_window_hiding() {
        SettingsController.listWaveformThumbnailEnabled = false
        var stackProvider = fakeThumbnailProviderComponent.createObject(testCase)
        var stackModel = createIsolatedTrackModel("stack-track-", 12)
        var stack = stackedTrackListHostComponent.createObject(
                    mainWindow.contentItem)
        verify(stackProvider && stackModel && stack)
        stack.list.trackModel = stackModel
        stack.list.thumbnailProvider = stackProvider
        SettingsController.listWaveformThumbnailEnabled = true
        tryVerify(function() { return stackProvider.requestCount > 0 })
        var cancellationsBeforeStackHide = stackProvider.cancelCount
        stack.currentIndex = 1
        tryVerify(function() { return !stack.list.visible })
        tryCompare(stack.list, "thumbnailItemCount", 0)
        tryVerify(function() {
            return stackProvider.cancelCount > cancellationsBeforeStackHide
        })
        stack.destroy()
        stackModel.destroy()
        stackProvider.destroy()

        nativeDropHelper.ensureSortableTracks()
        var filter = findChild(mainWindow, "filterModel")
        filter.tagKey = ""
        filter.resourceFolder = ""
        filter.searchText = ""
        filter.category = "all"
        SettingsController.listWaveformThumbnailEnabled = false
        var provider = fakeThumbnailProviderComponent.createObject(testCase)
        var window = listWindowComponent.createObject(null, {
            "filterModel": filter,
            "width": 1400
        })
        verify(provider && window)
        var list = findChild(window, "sharedTrackList")
        verify(list)
        list.thumbnailProvider = provider
        SettingsController.listWaveformThumbnailEnabled = true
        tryVerify(function() { return provider.requestCount > 0 })
        var cancellationsBeforeWindowHide = provider.cancelCount
        window.hide()
        tryCompare(list, "thumbnailItemCount", 0)
        tryVerify(function() {
            return provider.cancelCount > cancellationsBeforeWindowHide
        })

        window.destroy()
        provider.destroy()
    }

    function test_empty_library_uses_the_normal_player_structure() {
        var pane = findChild(mainWindow, "playerPane")
        var controls = findChild(mainWindow, "playerControls")
        compare(findChild(mainWindow, "emptyStartup"), null)
        verify(pane.visible)
        compare(controls.emptyMode, false)
        verify(findChild(mainWindow, "titleBrand").visible,
               "brand should be visible in the direct player title bar")
        compare(findChild(mainWindow, "titleBrandText").font.italic, false)
        verify(pane.y + pane.height <= controls.y + 1)
    }

    function test_main_window_allows_a_smaller_responsive_native_size() {
        compare(mainWindow.minimumWidth, 612)
        compare(mainWindow.minimumHeight,
                Theme.titleBarHeight + 128 + 64)
    }

    function test_main_window_keeps_player_content_visible_at_minimum_size() {
        if (LibraryModel.count === 0)
            nativeDropHelper.ensureSortableTracks()
        var oldWidth = mainWindow.width
        var oldHeight = mainWindow.height
        var resizedVolume = findChild(mainWindow, "mainVolumeControl")
        verify(resizedVolume)
        resizedVolume.expandedForQa = true
        wait(180)
        mainWindow.width = mainWindow.minimumWidth
        mainWindow.height = mainWindow.minimumHeight
        wait(50)

        var pane = findChild(mainWindow, "playerPane")
        var controls = findChild(mainWindow, "playerControls")
        var centerControls = findChild(mainWindow, "centerPlaybackControls")
        var volumeControl = findChild(mainWindow, "mainVolumeControl")
        var secondaryActions = findChild(mainWindow, "playerSecondaryActions")
        var cover = findChild(mainWindow, "playerCover")
        var waveform = findChild(mainWindow, "mainWaveform")
        verify(pane && controls && centerControls && volumeControl
               && secondaryActions && cover && waveform)
        tryVerify(function() { return pane.visible }, 1000)
        verify(pane.y + pane.height <= controls.y + 1,
               "player metadata/waveform must not cover playback controls")
        verify(controls.y + controls.height <= mainWindow.contentItem.height + 1,
               "playback controls must remain inside the small window")
        var centerRight = centerControls.mapToItem(
                    controls, centerControls.width, 0).x
        var volumeLeft = volumeControl.mapToItem(controls, 0, 0).x
        var volumeRight = volumeControl.mapToItem(
                    controls, volumeControl.width, 0).x
        var secondaryLeft = secondaryActions.mapToItem(controls, 0, 0).x
        verify(centerRight <= volumeLeft + 0.5,
               "compact playback actions must not overlap the volume control")
        verify(volumeRight <= secondaryLeft + 0.5,
               "compact volume control must not overlap the right actions: "
               + volumeRight + " > " + secondaryLeft)
        compare(findChild(mainWindow, "experienceActions").compact, true)
        compare(findChild(mainWindow, "playerShellModeButton"), null)
        compare(findChild(mainWindow, "miniPlayerButton").visible, true)
        var artist = findChild(mainWindow, "trackArtistAlbum")
        var rating = findChild(mainWindow, "trackRating")
        var metadata = findChild(mainWindow, "trackMetadataBadges")
        verify(artist && artist.visible,
               "artist/album must remain visible at the native minimum height")
        verify(rating && rating.visible,
               "rating must remain visible at the native minimum height")
        verify(metadata && metadata.visible,
               "file metadata must remain visible at the native minimum height")
        verify(artist.height > 0 && rating.height > 0 && metadata.height > 0,
               "responsive metadata rows must retain a usable rendered height")
        verify(artist.text.split(" · ").length >= 2
               && artist.text.split(" · ").length <= 3
               && artist.text.indexOf("无标签") < 0,
               "metadata must omit an empty tag without hiding artist/album")
        verify(rating.mapToItem(artist.parent, 0, 0).x
               >= artist.mapToItem(artist.parent, 0, 0).x + artist.width,
               "rating stars must immediately follow the artist/album/tag text")
        verify(cover.height <= pane.height,
               "cover must scale with the available player height")
        verify(waveform.height >= 32,
               "responsive layout must keep the waveform usable")

        mainWindow.width = oldWidth
        mainWindow.height = oldHeight
        wait(20)
    }

    function test_settings_page_is_lazy_until_requested() {
        verify(!findChild(mainWindow, "settingsPage"),
               "settings page should not increase empty-startup cost")

        // A prior test failure must not leave the singleton edit transaction
        // active and change what this test snapshots on open.
        SettingsController.cancelEdit()
        SettingsController.themeMode = 1
        findChild(mainWindow, "settingsButton").clicked()
        tryVerify(function() {
            return findChild(mainWindow, "settingsPage") !== null
        })
        const settingsWindow = findChild(mainWindow, "settingsWindow")
        verify(settingsWindow, "settings must open in its own window")
        compare(settingsWindow.width, 860)
        verify(settingsWindow.height >= 640 && settingsWindow.height <= 900,
               "settings window must fit the available desktop")
        const page = findChild(mainWindow, "settingsPage")
        tryVerify(function() { return page.visible })
        compare(page.editResolved, false,
                "opening settings must start a cancellable edit session")
        const scroll = findChild(page, "settingsScroll")
        const sectionList = findChild(page, "settingsSectionList")
        const sidebar = findChild(page, "settingsSidebar")
        const contentColumn = findChild(page, "settingsContentColumn")
        verify(scroll, "settings content must expose a scroll viewport")
        verify(sectionList, "settings navigation must expose a scrollable list")
        verify(sidebar, "settings navigation must use the compact sidebar")
        verify(contentColumn, "settings must expose the single content column")
        var headerDragArea = findChild(page, "settingsHeaderDragArea")
        verify(headerDragArea, "settings header must expose a full-width native drag surface")
        const macButtons = Qt.platform.os === "osx"
                ? findChild(page, "macCloseButton").parent : null
        const leadingControlsWidth = macButtons ? macButtons.width + Theme.spacingLg : 0
        verify(headerDragArea.x >= leadingControlsWidth)
        verify(headerDragArea.width + leadingControlsWidth > settingsWindow.width * 0.50)
        compare(sidebar.width, 184)
        verify(contentColumn.width <= 760,
               "settings content must remain a readable single column")
        verify(contentColumn.x >= 24,
               "settings content must keep balanced horizontal breathing room")
        verify(contentColumn.spacing <= 6,
               "settings sections must use compact PC spacing")
        const generalSection = findChild(page, "generalSettingsSection")
        verify(generalSection, "general settings section must exist")
        verify(generalSection.spacing <= 6,
               "settings controls must not leave oversized vertical gaps")
        verify(scroll.contentHeight > 0,
               "the selected settings category must expose content")
        verify(generalSection.visible,
               "only the selected settings category should be visible")
        compare(sectionList.count, 7,
                "all settings sections must be present")
        verify(sectionList.contentHeight <= sectionList.height
               || sectionList.interactive,
               "settings sections must either fit or remain scrollable")
        const associationFlow = findChild(page, "fileAssociationFlow")
        const oggAssociation = findChild(page, "oggAssociationCheck")
        verify(associationFlow, "file associations must expose their layout")
        verify(oggAssociation, "the final OGG association must be reachable")
        tryVerify(function() {
            return oggAssociation.x + oggAssociation.width
                    <= associationFlow.width + 0.5
        }, 1000)
        SettingsController.themeMode = 2
        findChild(page, "settingsCancelButton").clicked()
        tryCompare(SettingsController, "themeMode", 1)

        page.open()
        SettingsController.themeMode = 2
        findChild(page, "settingsSaveButton").clicked()
        tryCompare(SettingsController, "themeMode", 2)
        SettingsController.themeMode = 0
    }

    function test_settings_z_output_device_controls_are_real() {
        var page = findChild(mainWindow, "settingsPage")
        verify(page, "settings page should already be loaded")
        page.open()
        page.selectedSection = 1
        wait(250)
        var combo = findChild(page, "outputDeviceCombo")
        var exclusive = findChild(page, "exclusiveModeSwitch")
        var sampleRate = findChild(page, "matchTrackSampleRateSwitch")
        var fallback = findChild(page, "exclusiveFallbackLabel")
        var fade = findChild(page, "transitionFadeCombo")
        verify(combo, "output device combo should exist")
        verify(exclusive, "exclusive mode control should exist")
        verify(sampleRate, "sample-rate matching control should exist")
        verify(fallback, "exclusive fallback status should exist")
        verify(fade, "transition fade selector should exist")
        compare(combo.contentItem.elide, Text.ElideRight)
        verify(combo.contentItem.rightPadding > 0,
               "long localized device names must not overlap the indicator")
        SettingsController.transitionFadeMs = 500
        tryCompare(fade, "currentValue", 500)
        SettingsController.transitionFadeMs = 0
        tryCompare(fade, "currentValue", 0)
        SettingsController.transitionFadeMs = 200
        SettingsController.matchTrackSampleRate = false
        mouseClick(sampleRate, sampleRate.width / 2, sampleRate.height / 2)
        tryCompare(SettingsController, "matchTrackSampleRate", true)
        verify(combo.valueModel.length
               === PlaybackController.outputDevices.length + 1,
               "device combo should expose system default plus enumerated devices")
        if (PlaybackController.outputDeviceIds.length > 0) {
            compare(combo.valueModel[1].value,
                    PlaybackController.outputDeviceIds[0])
        }

        SettingsController.exclusiveMode = false
        verify(exclusive.visible, "exclusive mode control should be visible")
        var exclusiveKnob = exclusive.indicator.children[0]
        var exclusiveOffX = exclusiveKnob.x
        mouseClick(exclusive, exclusive.width / 2, exclusive.height / 2)
        tryCompare(SettingsController, "exclusiveMode", true)
        tryVerify(function() { return exclusiveKnob.x > exclusiveOffX },
                  500, "enabled switch knob must slide right")
        compare(exclusive.indicator.color, Theme.cyan)
        mouseClick(exclusive, exclusive.width / 2, exclusive.height / 2)
        tryCompare(SettingsController, "exclusiveMode", false)
        tryCompare(exclusiveKnob, "x", exclusiveOffX)
        page.close()
    }

    function test_settings_language_combo_lists_only_plain_chinese_and_english() {
        var page = findChild(mainWindow, "settingsPage")
        var ownsPage = false
        if (!page) {
            page = settingsPageComponent.createObject(mainWindow.contentItem)
            ownsPage = true
        }
        verify(page)
        page.open()
        page.selectedSection = 0
        wait(150)
        var combo = findChild(page, "languageCombo")
        verify(combo)
        compare(combo.valueModel.length, 2)
        compare(combo.valueModel[0].text, "中文")
        compare(combo.valueModel[1].text, "English")
        const labels = combo.valueModel.map(function(entry) { return entry.text }).join("|")
        verify(!/CN|US|🇨🇳|🇺🇸|ไทย|Tiếng Việt/.test(labels))
        SettingsController.language = "en"
        tryCompare(combo, "currentIndex", 1)
        tryVerify(function() { return combo.contentItem.text === "English" })
        SettingsController.language = "zh"
        if (ownsPage) {
            page.saveAndClose()
            page.destroy()
        } else
            page.close()
    }

    function test_settings_transcode_controls_are_split_and_scroll_tracks_section() {
        var page = findChild(mainWindow, "settingsPage")
        if (!page) {
            findChild(mainWindow, "settingsButton").clicked()
            tryVerify(function() {
                return findChild(mainWindow, "settingsPage") !== null
            })
            page = findChild(mainWindow, "settingsPage")
        }
        verify(page)
        page.open()
        page.selectedSection = 3
        wait(250)

        var formatCombo = findChild(page, "transcodeFormatCombo")
        var bitrateCombo = findChild(page, "transcodeBitrateCombo")
        var sampleRateCombo = findChild(page, "transcodeSampleRateCombo")
        var channelCombo = findChild(page, "transcodeChannelCombo")
        verify(formatCombo)
        verify(bitrateCombo)
        verify(sampleRateCombo)
        verify(channelCombo)
        compare(formatCombo.valueModel.length, 8)
        compare(bitrateCombo.valueModel.length, 4)
        compare(sampleRateCombo.valueModel.length, 6)
        compare(sampleRateCombo.valueModel[0].value, 44100)
        compare(sampleRateCombo.valueModel[5].value, 192000)
        compare(channelCombo.valueModel.length, 2)

        SettingsController.transcodeFormat = "FLAC"
        tryCompare(bitrateCombo, "enabled", false)
        SettingsController.transcodeFormat = "MP3"
        tryCompare(bitrateCombo, "enabled", true)

        const scroll = findChild(page, "settingsScroll")
        const toolsSection = findChild(page, "audioToolsSettingsSection")
        const aboutSection = findChild(page, "aboutSettingsSection")
        verify(toolsSection.visible)
        verify(!aboutSection.visible)
        page.selectedSection = 6
        tryCompare(scroll.contentItem, "contentY", 0)
        verify(!toolsSection.visible)
        verify(aboutSection.visible)
        page.close()
    }

    function test_settings_x_cache_limit_accepts_gigabytes_and_actions_share_one_row() {
        var page = findChild(mainWindow, "settingsPage")
        if (!page) {
            findChild(mainWindow, "settingsButton").clicked()
            tryVerify(function() {
                return findChild(mainWindow, "settingsPage") !== null
            })
            page = findChild(mainWindow, "settingsPage")
        }
        verify(page)
        page.open()
        page.selectedSection = 5
        wait(250)
        var limit = findChild(page, "cacheSizeLimitField")
        var waveform = findChild(page, "clearWaveformCacheButton")
        var covers = findChild(page, "clearCoverCacheButton")
        var temp = findChild(page, "clearTempCacheButton")
        var all = findChild(page, "clearAllCacheButton")
        verify(limit && waveform && covers && temp && all)
        limit.text = "20"
        limit.editingFinished()
        tryCompare(SettingsController, "cacheSizeLimitMB", 20480)
        compare(waveform.parent, covers.parent)
        compare(waveform.parent, temp.parent)
        compare(waveform.parent, all.parent)
        page.close()
    }

    function test_settings_x_audio_presets_share_one_compact_row() {
        var page = findChild(mainWindow, "settingsPage")
        verify(page)
        page.open()
        page.selectedSection = 3
        wait(250)
        var keepPitch = findChild(page, "keepPitchPresetSwitch")
        var protectVoice = findChild(page, "vocalProtectionPresetSwitch")
        verify(keepPitch && protectVoice)
        compare(keepPitch.parent, protectVoice.parent)
        page.close()
    }

    function test_settings_y_about_separates_product_version_and_promise() {
        if (!findChild(mainWindow, "settingsPage")) {
            mainWindow.openSettingsPage()
            tryVerify(function() {
                return findChild(mainWindow, "settingsPage") !== null
            })
        }
        var page = findChild(mainWindow, "settingsPage")
        verify(page)
        page.open()
        page.selectedSection = 6
        wait(250)
        var productLine = findChild(page, "aboutProductLine")
        verify(productLine)
        compare(productLine.text, "AgPlayer")
        verify(productLine.font.weight >= Font.Bold)
        var versionLine = findChild(page, "aboutStandaloneVersion")
        var promiseLine = findChild(page, "aboutProductPromise")
        verify(versionLine && promiseLine)
        compare(versionLine.text, qsTr("版本号：") + SettingsController.version)
        compare(promiseLine.text, qsTr("免费、轻便、纯净"))
        var updateStatus = findChild(page, "aboutUpdateStatus")
        var checkUpdates = findChild(page, "aboutCheckUpdates")
        var downloadUpdate = findChild(page, "aboutDownloadUpdate")
        verify(updateStatus && checkUpdates && downloadUpdate)
        compare(updateStatus.text, SettingsController.updateChecker.statusText)
        verify(downloadUpdate.visible, "Official download fallback must remain available")
        if (SettingsController.updateChecker.state === "unconfigured") {
            verify(checkUpdates.enabled)
            checkUpdates.clicked()
            compare(SettingsController.updateChecker.state, "unconfigured")
            verify(downloadUpdate.visible)
        }
        page.close()
    }

    function test_settings_frequency_color_waveform_exposes_three_band_controls() {
        var page = findChild(mainWindow, "settingsPage")
        var ownsPage = false
        if (!page) {
            page = settingsPageComponent.createObject(mainWindow.contentItem)
            ownsPage = true
        }
        verify(page)
        var previousWaveformMode = SettingsController.waveformMode
        SettingsController.waveformMode = 3
        page.open()
        page.selectedSection = 2
        wait(0)
        tryCompare(page, "programmaticScroll", false, 1000)

        var lowField = findChild(page, "frequencyLowColor")
        var midField = findChild(page, "frequencyMidColor")
        var highField = findChild(page, "frequencyHighColor")
        var differenceSlider = findChild(page,
                                         "frequencyUnplayedOpacitySlider")
        var resetButton = findChild(page, "frequencyColorResetButton")
        var preview = findChild(page, "frequencyWaveformThumbnailPreview")
        var palette = findChild(page, "frequencyBandPalette")
        verify(lowField && midField && highField && differenceSlider
               && resetButton && preview && palette)
        compare(palette.bandCount, 3)
        compare(palette.bandNames.join("/"),
                "低频红色/中频绿色/高频蓝色")
        compare(differenceSlider.handle.width, Theme.sliderHandleExtent)
        compare(differenceSlider.handle.height, Theme.sliderHandleExtent)
        compare(preview.visualMode, 3)
        compare(preview.layers.mix.length, preview.layers.bass.length)
        compare(preview.layers.mix.length, preview.layers.mid.length)
        compare(preview.layers.mix.length, preview.layers.high.length)

        lowField.openPicker()
        var colorPicker = findChild(mainWindow, "colorFieldPicker")
        var hexField = findChild(mainWindow, "colorPickerHexField")
        var restoreButton = findChild(mainWindow, "colorPickerRestoreButton")
        var cancelButton = findChild(mainWindow, "colorPickerCancelButton")
        var confirmButton = findChild(mainWindow, "colorPickerConfirmButton")
        verify(colorPicker && hexField)
        verify(restoreButton && cancelButton && confirmButton)
        verify(colorPicker.width >= 288 && colorPicker.width <= 304)
        verify(colorPicker.height <= 332)
        verify(restoreButton.width <= 76)
        verify(cancelButton.width <= 60)
        verify(confirmButton.width <= 60)
        verify(hexField.selectByMouse)
        hexField.text = "#A1B2C3"
        hexField.selectAll()
        hexField.copy()
        hexField.clear()
        hexField.paste()
        compare(hexField.text, "#A1B2C3")
        hexField.accepted()
        compare(String(colorPicker.workingColor), "#a1b2c3")
        colorPicker.close()

        lowField.colorEdited("#112233")
        midField.colorEdited("#445566")
        highField.colorEdited("#778899")
        compare(String(SettingsController.frequencyColorWaveform.lowColor), "#112233")
        compare(String(SettingsController.frequencyColorWaveform.midColor), "#445566")
        compare(String(SettingsController.frequencyColorWaveform.highColor), "#778899")
        compare(String(preview.lowColor), "#112233")
        differenceSlider.value = 38
        differenceSlider.moved()
        compare(SettingsController.frequencyColorWaveform.unplayedDimness, 0.38)
        compare(SettingsController.frequencyColorWaveform.unplayedOpacity, 0.62)
        resetButton.clicked()
        compare(String(SettingsController.frequencyColorWaveform.lowColor), "#ff0000")
        compare(String(SettingsController.frequencyColorWaveform.midColor), "#00ff00")
        compare(String(SettingsController.frequencyColorWaveform.highColor), "#0000ff")
        compare(SettingsController.frequencyColorWaveform.unplayedDimness, 0.30)
        compare(SettingsController.frequencyColorWaveform.unplayedOpacity, 0.70)

        page.cancelAndClose()
        SettingsController.waveformMode = previousWaveformMode
        if (ownsPage)
            page.destroy()
    }

    function test_settings_list_waveform_brightness_is_live_and_defaults_to_50_percent() {
        var page = findChild(mainWindow, "settingsPage")
        var ownsPage = false
        if (!page) {
            page = settingsPageComponent.createObject(mainWindow.contentItem)
            ownsPage = true
        }
        verify(page)
        page.open()
        page.selectedSection = 2
        wait(0)
        tryCompare(page, "programmaticScroll", false, 1000)

        var slider = findChild(page, "trackWaveformBrightnessSlider")
        verify(slider, "song-list waveform brightness slider must exist")
        verify(slider.visible, "song-list waveform brightness slider must be visible")
        verify(slider.width > 0 && slider.height > 0,
               "song-list waveform brightness slider must have a usable size")
        compare(slider.handle.width, Theme.sliderHandleExtent)
        compare(slider.handle.height, Theme.sliderHandleExtent)
        var listWaveformCard = findChild(page, "listWaveformSettingsCard")
        verify(listWaveformCard, "song-list waveform settings card must exist")
        var sliderBottom = slider.mapToItem(listWaveformCard, 0, slider.height).y
        verify(sliderBottom <= listWaveformCard.height,
               "song-list waveform brightness slider must fit inside its settings card")
        compare(slider.from, 20)
        compare(slider.to, 100)
        compare(Math.round(slider.value), 50)
        slider.value = 74
        slider.moved()
        compare(SettingsController.trackWaveformBrightness, 0.74)

        page.cancelAndClose()
        if (ownsPage)
            page.destroy()
    }

    function test_settings_waveform_controls_are_live() {
        var page = findChild(mainWindow, "settingsPage")
        var ownsPage = false
        if (!page) {
            page = settingsPageComponent.createObject(mainWindow.contentItem)
            ownsPage = true
        }
        verify(page)
        var previousWaveformMode = SettingsController.waveformMode
        page.open()
        page.selectedSection = 2
        SettingsController.waveformMode = 3
        wait(0)
        tryCompare(page, "programmaticScroll", false, 1000)

        var heightStepper = findChild(page, "waveformHeightStepper")
        var densityStepper = findChild(page, "waveformDensityStepper")
        var thicknessStepper = findChild(page, "waveformThicknessStepper")
        var aggregationCombo = findChild(page, "waveformAggregationCombo")
        var resetButton = findChild(page, "waveformResetButton")
        var frequencyResetButton = findChild(page, "frequencyColorResetButton")
        var lowColorField = findChild(page, "frequencyLowColor")
        var differenceSlider = findChild(page,
                                         "frequencyUnplayedOpacitySlider")
        var frequencyPreview = findChild(
                    page, "frequencyWaveformThumbnailPreview")
        var listThumbnailSwitch = findChild(
                    page, "listWaveformThumbnailEnabledControl")
        var listThumbnailMode = findChild(
                    page, "listWaveformThumbnailModeControl")
        verify(heightStepper && densityStepper && thicknessStepper)
        verify(aggregationCombo && resetButton && frequencyResetButton)
        verify(lowColorField && differenceSlider && frequencyPreview)
        verify(listThumbnailSwitch && listThumbnailMode)
        compare(frequencyPreview.layers.mix.length,
                frequencyPreview.layers.bass.length)

        SettingsController.listWaveformThumbnailEnabled = true
        SettingsController.listWaveformThumbnailMode = "Spectral"
        tryCompare(listThumbnailSwitch, "checked", true)
        tryCompare(listThumbnailMode, "currentValue", "Spectral")
        listThumbnailMode.currentIndex = 1
        listThumbnailMode.activated(1)
        tryCompare(SettingsController, "listWaveformThumbnailMode", "Mono")

        SettingsController.waveformHeight = 1.2
        SettingsController.waveformDensity = 3.5
        SettingsController.waveformThickness = 2.2
        SettingsController.waveformPeakAlgorithm = 1
        tryCompare(heightStepper, "value", 1.2)
        tryCompare(densityStepper, "value", 3.5)
        tryCompare(thicknessStepper, "value", 2.2)
        tryCompare(aggregationCombo, "currentValue", 1)

        lowColorField.colorEdited("#112233")
        compare(String(SettingsController.frequencyColorWaveform.lowColor), "#112233")
        compare(String(frequencyPreview.lowColor), "#112233")
        differenceSlider.value = 38
        differenceSlider.moved()
        compare(SettingsController.frequencyColorWaveform.unplayedDimness, 0.38)
        compare(SettingsController.frequencyColorWaveform.unplayedOpacity, 0.62)
        frequencyResetButton.clicked()
        compare(String(SettingsController.frequencyColorWaveform.lowColor), "#ff0000")
        compare(SettingsController.frequencyColorWaveform.unplayedDimness, 0.30)
        compare(SettingsController.frequencyColorWaveform.unplayedOpacity, 0.70)

        resetButton.clicked()
        tryCompare(SettingsController, "waveformHeight", 0.8)
        tryCompare(SettingsController, "waveformDensity", 2.0)
        tryCompare(SettingsController, "waveformThickness", 1.0)
        tryCompare(SettingsController, "waveformPeakAlgorithm", 0)
        tryCompare(SettingsController, "listWaveformThumbnailEnabled", true)
        tryCompare(SettingsController, "listWaveformThumbnailMode", "Spectral")

        page.cancelAndClose()
        SettingsController.waveformMode = previousWaveformMode
        if (ownsPage)
            page.destroy()
    }

    function test_settings_exposes_dual_window_as_the_default_layout_skin() {
        var page = findChild(mainWindow, "settingsPage")
        var ownsPage = false
        if (!page) {
            page = settingsPageComponent.createObject(mainWindow.contentItem)
            ownsPage = true
        }
        verify(page)
        page.open()
        page.selectedSection = 2
        wait(150)
        var selector = findChild(page, "windowLayoutThemeCombo")
        verify(selector)
        compare(selector.valueModel.length, 3)
        compare(selector.valueModel[0].value, "dual-window")
        compare(selector.valueModel[1].value, "single-window")
        compare(selector.valueModel[2].value, "rolling-player")
        compare(selector.currentValue, "dual-window")
        compare(SettingsController.windowLayoutTheme, "dual-window")
        if (ownsPage) {
            page.saveAndClose()
            page.destroy()
        } else {
            page.close()
        }
    }

    function test_settings_theme_buttons_select_system_light_and_dark() {
        var page = findChild(mainWindow, "settingsPage")
        var ownsPage = false
        if (!page) {
            page = settingsPageComponent.createObject(mainWindow.contentItem)
            ownsPage = true
        }
        verify(page)
        SettingsController.themeMode = 0
        page.open()
        page.selectedSection = 2
        wait(250)

        var systemButton = findChild(page, "themeModeSystem")
        var lightButton = findChild(page, "themeModeLight")
        var darkButton = findChild(page, "themeModeDark")
        verify(systemButton && lightButton && darkButton)
        compare(darkButton.text, "深色")
        verify(!findChild(page, "themeModeCustom"))
        verify(!findChild(page, "themeColorSelector"))

        var cases = [
            { "button": systemButton, "beforeTheme": 0,
              "theme": 2 },
            { "button": lightButton, "beforeTheme": 0,
              "theme": 1 },
            { "button": darkButton, "beforeTheme": 1,
              "theme": 0 }
        ]
        for (var index = 0; index < cases.length; ++index) {
            SettingsController.themeMode = cases[index].beforeTheme
            wait(0)
            cases[index].button.clicked()
            tryCompare(SettingsController, "themeMode", cases[index].theme)
            verify(cases[index].button.checked)
        }

        page.cancelAndClose()
        if (ownsPage)
            page.destroy()
    }

    function test_settings_y_feedback_entry_is_removed() {
        var page = findChild(mainWindow, "settingsPage")
        verify(page)
        page.open()
        page.selectedSection = 6
        wait(500)
        verify(!findChild(page, "feedbackButton"))
        verify(!findChild(page, "feedbackDialog"))
        page.close()
    }

    function test_track_context_submenus_follow_dark_and_light_theme() {
        if (LibraryModel.count === 0)
            nativeDropHelper.ensureSortableTracks()
        var previousMode = SettingsController.themeMode
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        var rowItem = list.itemAtIndex(0)
        verify(rowItem)
        for (var mode = 0; mode <= 1; ++mode) {
            SettingsController.themeMode = mode
            tryCompare(Theme, "isLight", mode === 1)
            mouseClick(rowItem, rowItem.width / 2, rowItem.height / 2,
                       Qt.RightButton)
            var menu = findChild(list, "trackContextMenu")
            tryVerify(function() { return menu && menu.visible })
            var playlistEntry = menu.itemAt(2)
            var toolsEntry = menu.itemAt(3)
            compare(playlistEntry.contentItem.color.toString(),
                    (playlistEntry.enabled ? Theme.primaryText
                                           : Theme.secondaryText).toString())
            compare(toolsEntry.contentItem.color.toString(),
                    (toolsEntry.enabled ? Theme.primaryText
                                        : Theme.secondaryText).toString())
            compare(playlistEntry.background.color.toString(), "#00000000")
            compare(toolsEntry.background.color.toString(), "#00000000")
            menu.close()
        }
        SettingsController.themeMode = previousMode
        list.destroy()
    }

    function test_theme_mode_updates_surfaces_text_and_icons() {
        var previousMode = SettingsController.themeMode
        var previousWaveformMode = SettingsController.waveformMode
        var previousGuide = SettingsController.waveformPlaybackGuide
        SettingsController.frequencyColorWaveform.resetToDefault()
        SettingsController.waveformMode = 3
        var waveform = findChild(mainWindow, "mainWaveform")
        var playedClip = findChild(mainWindow, "waveformPlayedClip")
        var playbackGuide = findChild(mainWindow, "waveformPlaybackGuide")
        verify(waveform && playedClip && playbackGuide)

        SettingsController.themeMode = 0
        tryCompare(Theme, "isLight", false)
        var darkBackground = Theme.background.toString()
        var darkText = Theme.primaryText.toString()
        compare(darkBackground, "#181a1d")
        compare(findChild(mainWindow, "playButtonBody").border.color.toString(),
                (PlaybackController.state === PlaybackController.Playing
                 ? Theme.playRingPlaying : Theme.playRingPaused).toString())
        compare(String(waveform.lowColor), "#ff0000")
        compare(String(waveform.midColor), "#00ff00")
        compare(String(waveform.highColor), "#0000ff")
        compare(waveform.frequencyUnplayedOpacity,
                Theme.nonImmersiveSpectralUnplayedOpacity)
        compare(playedClip.visible, true)
        SettingsController.waveformPlaybackGuide = false
        compare(playbackGuide.visible, false)

        SettingsController.themeMode = 1
        compare(SettingsController.themeMode, 1)
        tryCompare(Theme, "isLight", true)
        compare(Theme.accentText.toString(), Theme.onCyanText.toString())
        compare(Theme.accentText.toString(), Theme.onBrandGradientText.toString())
        verify(Theme.background.toString() !== darkBackground)
        verify(Theme.primaryText.toString() !== darkText)
        compare(findChild(mainWindow, "settingsButton").icon.color.toString(),
                Theme.iconSecondary.toString())
        compare(String(waveform.lowColor), "#ff0000")
        compare(String(waveform.highColor), "#0000ff")

        SettingsController.themeMode = 2
        tryCompare(Theme, "followsSystem", true)
        compare(Theme.requestedMode, 2)
        compare(Theme.effectiveMode, Theme.systemIsLight ? 1 : 0)
        compare(Theme.background.toString(),
                Theme.systemIsLight ? "#fafafb" : "#181a1d")
        compare(Theme.cyan.toString(), Theme.accent.toString())
        compare(Theme.waveformCyan.toString(), "#00d4ff")

        SettingsController.themeMode = previousMode
        SettingsController.waveformMode = previousWaveformMode
        SettingsController.waveformPlaybackGuide = previousGuide
    }

    function test_ui_design_system_uses_approved_theme_and_metric_tokens() {
        var previousMode = SettingsController.themeMode

        SettingsController.themeMode = 0
        tryCompare(Theme, "isLight", false)
        compare(Theme.titleBarSurface.toString(), "#202329")
        compare(Theme.navigationSurface.toString(), "#24272d")
        compare(Theme.contentSurface.toString(), "#181a1d")
        compare(Theme.accent.toString(), "#7657e8")
        compare(Theme.selectedSurface.toString(), "#302a45")
        verify(colorContrast(Theme.accentText, Theme.accent) >= 4.5)

        SettingsController.themeMode = 1
        tryCompare(Theme, "isLight", true)
        compare(Theme.titleBarSurface.toString(), "#e5e8ed")
        compare(Theme.navigationSurface.toString(), "#eceef2")
        compare(Theme.contentSurface.toString(), "#fafafb")
        compare(Theme.accent.toString(), "#6d28d9")
        compare(Theme.selectedSurface.toString(), "#e8e8fb")
        verify(colorContrast(Theme.accentText, Theme.accent) >= 4.5)

        compare(Theme.fontSizeCaption, 12)
        compare(Theme.fontSizeMeta, 12)
        compare(Theme.fontSizeBody, 14)
        compare(Theme.fontSizeBodyStrong, 14)
        compare(Theme.fontSizeSection, 16)
        compare(Theme.fontSizePageTitle, 20)
        if (Qt.platform.os === "windows")
            compare(Theme.fontPrimary, "Microsoft YaHei UI")

        compare(Theme.radiusXs, 4)
        compare(Theme.radiusSm, 6)
        compare(Theme.radiusMd, 8)
        compare(Theme.radiusLg, 8)
        compare(Theme.spacingXs, 4)
        compare(Theme.spacingSm, 8)
        compare(Theme.spacingMd, 12)
        compare(Theme.spacingLg, 16)
        compare(Theme.spacingXl, 24)
        compare(Theme.spacing2Xl, 32)

        compare(Theme.titleBarHeight, 40)
        compare(Theme.navigationWidthCompact, 192)
        compare(Theme.navigationWidth, 216)
        compare(Theme.navigationWidthExpanded, 240)
        compare(Theme.controlHeightCompact, 28)
        compare(Theme.controlHeight, 32)
        compare(Theme.controlHeightProminent, 36)
        compare(Theme.navigationRowHeight, 36)
        compare(Theme.listRowHeight, 38)
        compare(Theme.mediaListRowHeight, 46)
        compare(Theme.settingsRowHeight, 48)
        compare(Theme.tableHeaderHeight, 36)
        compare(Theme.sliderTrackHeight, 2)
        compare(Theme.sliderHandleExtent, 10)
        compare(Theme.minimumInteractionExtent, 28)

        SettingsController.themeMode = previousMode
    }

    function test_rating_stars_use_one_solid_orange_color() {
        compare(Theme.ratingGold.toString(), "#ff9800")
        for (var index = 0; index < 5; ++index)
            compare(Theme.ratingColor(index).toString(), "#ff9800")
    }

    function test_integrated_player_control_order() {
        var previousLayoutTheme = SettingsController.windowLayoutTheme
        ignoreWarning(new RegExp(
            "This plugin does not support propagateSizeHints\\(\\)"))
        ignoreWarning(new RegExp(
            "This plugin does not support propagateSizeHints\\(\\)"))
        ignoreWarning(new RegExp(
            "This plugin does not support propagateSizeHints\\(\\)"))
        ignoreWarning(new RegExp(
            "This plugin does not support propagateSizeHints\\(\\)"))
        SettingsController.playerShellMode = 1
        SettingsController.windowLayoutTheme = "single-window"
        try {
            tryVerify(function() {
                return findChild(mainWindow, "integratedPlayerShell") !== null
            }, 1500)
            tryVerify(function() {
                return findChild(mainWindow, "integratedPlayerControls") !== null
            }, 1500)
            var controls = findChild(mainWindow, "integratedPlayerControls")
            var summary = findChild(mainWindow, "integratedTrackSummary")
            var listButton = findChild(controls, "listWindowButton")
            var centerGroup = findChild(controls, "integratedCenterControls")
            var transport = findChild(controls, "integratedTransportControls")
            var playButton = findChild(controls, "playPauseButton")
            var volume = findChild(controls, "mainVolumeControl")
            var rightActions = findChild(controls, "integratedRightActions")
            verify(summary && listButton && centerGroup
                   && transport && playButton && volume && rightActions)
            compare(listButton.visible, false)
            compare(findChild(controls, "lyricsActionButton"), null)
            verifyAscendingX(transport, [
                "equalizerButton", "waveformModeButton", "previousButton",
                "playPauseButton", "nextButton", "modeButton"
            ])
            verifyAscendingX(rightActions, [
                "themeModeButton", "immersiveActionButton", "miniPlayerButton"
            ])
            var summaryEndX = summary.mapToItem(controls, summary.width, 0).x
            var audioToolsStartX = findChild(controls, "audioToolsButton").mapToItem(
                        controls, 0, 0).x
            verify(summaryEndX <= audioToolsStartX + 1.0,
                   "summary end " + summaryEndX
                   + " overlaps audio tools at " + audioToolsStartX)
            verify(findChild(controls, "audioToolsButton").mapToItem(
                       controls, findChild(controls, "audioToolsButton").width, 0).x
                   <= transport.mapToItem(controls, 0, 0).x)
            verify(transport.mapToItem(
                       controls, transport.width, 0).x
                   <= volume.mapToItem(controls, 0, 0).x)
            verify(volume.mapToItem(
                       controls, volume.width, 0).x
                   <= rightActions.mapToItem(controls, 0, 0).x)
            compare(centerGroup.width, transport.width,
                    "volume must not contribute to the centered transport width")
            wait(300)
            var controlsCenter = controls.mapToItem(
                        mainWindow.contentItem, controls.width / 2, 0).x
            var playCenter = playButton.mapToItem(
                        mainWindow.contentItem, playButton.width / 2, 0).x
            compare(Math.round(playCenter), Math.round(controlsCenter))
            var playCenterBefore = playCenter
            volume.expandedForQa = true
            wait(220)
            var expandedControlsCenter = controls.mapToItem(
                        mainWindow.contentItem, controls.width / 2, 0).x
            var expandedPlayCenter = playButton.mapToItem(
                        mainWindow.contentItem, playButton.width / 2, 0).x
            compare(Math.round(expandedPlayCenter),
                    Math.round(expandedControlsCenter),
                    "expanded integrated play button must stay centered")
            if (Math.round(expandedControlsCenter) === Math.round(controlsCenter))
                compare(Math.round(expandedPlayCenter),
                        Math.round(playCenterBefore))
            volume.expandedForQa = false
        } finally {
            SettingsController.playerShellMode = 0
            SettingsController.windowLayoutTheme = previousLayoutTheme
            tryVerify(function() {
                var classicShell = findChild(mainWindow, "classicPlayerShell")
                return classicShell !== null && classicShell.visible
                        && findChild(mainWindow, "integratedPlayerControls") === null
            }, 1500)
        }
    }

    function test_list_lyrics_panel_uses_external_window() {
        var previousVisible = PlayerExperienceController.lyricsVisible
        PlayerExperienceController.lyricsVisible = true
        var window = listWindowComponent.createObject(null, {
            "filterModel": findChild(mainWindow, "filterModel")
        })
        verify(window)
        try {
            var lyricsWindow = findChild(window, "listLyricsWindow")
            var panel = findChild(window, "listLyricsPanel")
            verify(lyricsWindow && panel)
            tryCompare(panel, "visible", true, 500)
            compare(lyricsWindow.transientParent, window)
            compare(panel.Window.window, lyricsWindow)
            verify(panel.Window.window !== window,
                   "lyrics must not live inside the list window container")
            verify(lyricsWindow.height >= window.height,
                   "lyrics height follows the combined player and list host")
            verify(lyricsWindow.width >= 280 && lyricsWindow.width <= 420)
            compare(lyricsWindow.dockEdge, "right")
            compare(lyricsWindow.docked, true)
            verify(findChild(lyricsWindow, "lyricsWindowDragHandler"))
        } finally {
            window.destroy()
            PlayerExperienceController.lyricsVisible = previousVisible
        }
    }

    function test_list_lyrics_window_selects_all_four_magnetic_edges() {
        var window = listWindowComponent.createObject(null, {
            "filterModel": findChild(mainWindow, "filterModel")
        })
        verify(window)
        try {
            var host = Qt.rect(300, 200, 600, 500)
            compare(window.lyricsDockEdgeForGeometry(
                        112, 240, 180, 220, host, 52), "left")
            compare(window.lyricsDockEdgeForGeometry(
                        908, 240, 180, 220, host, 52), "right")
            compare(window.lyricsDockEdgeForGeometry(
                        360, -28, 240, 220, host, 52), "top")
            compare(window.lyricsDockEdgeForGeometry(
                        360, 708, 240, 220, host, 52), "bottom")
            compare(window.lyricsDockEdgeForGeometry(
                        20, 100, 180, 180, host, 32), "none")
        } finally {
            window.destroy()
        }
    }

    function test_normal_lyrics_panel_hides_chrome_but_keeps_text() {
        var previousVisible = PlayerExperienceController.lyricsVisible
        PlayerExperienceController.lyricsVisible = true
        var window = listWindowComponent.createObject(null, {
            "filterModel": findChild(mainWindow, "filterModel")
        })
        verify(window)
        try {
            var panel = findChild(window, "listLyricsPanel")
            verify(panel)
            tryCompare(panel, "visible", true, 500)
            var surface = findChild(panel, "lyricsPanelSurface")
            var controls = findChild(panel, "lyricsChromeControls")
            var currentLine = findChild(panel, "currentLyricLine")
            var timeline = findChild(panel, "lyricsTimelineList")
            var fontSlider = findChild(panel, "lyricsFontSizeSlider")
            var retry = findChild(panel, "lyricsRetryButton")
            verify(surface && controls && currentLine && timeline
                   && fontSlider && retry)
            compare(fontSlider.from, 60)
            compare(fontSlider.to, 140)

            panel.chromeAutoHideDelay = 20
            panel.revealChrome()
            compare(panel.chromeVisible, true)
            panel.scheduleChromeHide()
            tryCompare(panel, "chromeVisible", false, 200)
            compare(surface.visible, false)
            compare(controls.visible, false)
            compare(currentLine.visible, true)
            verify(retry.icon.source.toString().indexOf("restore-line") >= 0)
            compare(retry.Accessible.name, qsTr("刷新歌词"))
        } finally {
            window.destroy()
            PlayerExperienceController.lyricsVisible = previousVisible
        }
    }

    function test_lyrics_close_hides_only_the_current_host_and_preserves_service_state() {
        var previousVisible = PlayerExperienceController.lyricsVisible
        PlayerExperienceController.lyricsVisible = true
        var window = listWindowComponent.createObject(null, {
            "filterModel": findChild(mainWindow, "filterModel")
        })
        verify(window)
        try {
            var lyricsWindow = findChild(window, "listLyricsWindow")
            var panel = findChild(window, "listLyricsPanel")
            var closeButton = findChild(panel, "lyricsCloseButton")
            verify(lyricsWindow && panel && closeButton)
            tryCompare(lyricsWindow, "visible", true, 500)
            compare(closeButton.Accessible.name, qsTr("关闭歌词窗口"))

            var statusBefore = LyricsService.status
            var sourceBefore = LyricsService.sourceProvider
            var attemptsBefore = JSON.stringify(LyricsService.routeAttempts)
            mouseClick(closeButton)

            tryCompare(lyricsWindow, "visible", false, 500)
            compare(PlayerExperienceController.lyricsVisible, false)
            compare(LyricsService.enabled, false)
            compare(LyricsService.status, statusBefore)
            compare(LyricsService.sourceProvider, sourceBefore)
            compare(JSON.stringify(LyricsService.routeAttempts), attemptsBefore)

            PlayerExperienceController.lyricsVisible = true
            tryCompare(lyricsWindow, "visible", true, 500)
            compare(LyricsService.enabled, true)
            compare(LyricsService.status, statusBefore)
            compare(LyricsService.sourceProvider, sourceBefore)
        } finally {
            window.destroy()
            PlayerExperienceController.lyricsVisible = previousVisible
        }
    }

    function test_shared_lyrics_side_panel_close_collapses_without_changing_page() {
        var panel = lyricsSidePanelComponent.createObject(mainWindow.contentItem)
        verify(panel)
        try {
            var closeButton = findChild(panel, "lyricsCloseButton")
            verify(closeButton)
            compare(panel.currentPage, 1)
            compare(panel.expanded, true)
            closeButton.clicked()
            compare(panel.expanded, false)
            compare(panel.currentPage, 1)
        } finally {
            panel.destroy()
        }
    }

    function test_lyrics_route_summary_keeps_no_match_distinct_from_network_failure() {
        var previousVisible = PlayerExperienceController.lyricsVisible
        PlayerExperienceController.lyricsVisible = true
        var window = listWindowComponent.createObject(null, {
            "filterModel": findChild(mainWindow, "filterModel")
        })
        verify(window)
        try {
            var panel = findChild(window, "listLyricsPanel")
            verify(panel)
            compare(panel.routeReason("not-found"), qsTr("未找到匹配歌词"))
            compare(panel.routeReason("network-error"), qsTr("网络请求失败"))
            verify(panel.routeReason("not-found")
                   !== panel.routeReason("network-error"))
            var summary = panel.routeAttemptsSummary([
                { "providerName": "LRCLIB", "diagnostic": "not-found" },
                { "providerName": "Unison", "diagnostic": "timeout" },
                { "providerName": "lyrics.ovh", "diagnostic": "invalid-response" }
            ])
            verify(summary.indexOf("LRCLIB：未找到匹配歌词") >= 0)
            verify(summary.indexOf("Unison：请求超时") >= 0)
            verify(summary.indexOf("lyrics.ovh：返回内容无效") >= 0)
        } finally {
            window.destroy()
            PlayerExperienceController.lyricsVisible = previousVisible
        }
    }

    function test_title_buttons_use_compact_chinese_labels() {
        var settings = findChild(mainWindow, "settingsButton")
        var minimize = findChild(mainWindow, "minimizeButton")
        var maximize = findChild(mainWindow, "maximizeButton")
        var close = findChild(mainWindow, "closeButton")
        verify(settings && minimize && maximize && close)
        compare(settings.icon.width, 16)
        compare(minimize.icon.width, 16)
        compare(maximize.icon.width, 16)
        compare(close.icon.width, 16)
        compare(settings.text, "设置")
        compare(minimize.text, "最小化")
        compare(close.text, "关闭")
    }

    function test_z_hidden_video_loader_fullscreen_escape_return_and_audio_restore() {
        var originalPlayback = mainWindow.playback
        var originalVideoPlayback = mainWindow.videoPlayback
        var priorVisibility = mainWindow.visibility
        var videoState = videoPlaybackStateComponent.createObject(testCase)
        var transport = videoPlaybackTransportComponent.createObject(testCase, {
            "videoState": videoState,
            "fullscreenProbe": mainWindow
        })
        verify(videoState && transport)
        mainWindow.playback = transport
        mainWindow.videoPlayback = videoState
        transport.stopHidesVideo = false

        try {
            var shellLoader = findChild(mainWindow, "playerShellLoader")
            var loader = findChild(mainWindow, "videoPlaybackLoader")
            verify(shellLoader && loader)
            var originalShell = shellLoader.item
            compare(loader.active, false,
                    "audio-only state must not instantiate the video view")

            videoState.visible = true
            tryCompare(loader, "active", true)
            tryVerify(function() { return loader.item !== null })
            verify(findChild(loader.item, "videoFrameItem"))
            compare(shellLoader.item, originalShell,
                    "the audio shell must remain loaded under the video view")

            mainWindow.requestActivate()
            tryVerify(function() { return mainWindow.active })
            mainWindow.enterVideoFullscreen()
            tryCompare(mainWindow, "videoFullscreen", true)
            verify(nativeDropHelper.sendKey(mainWindow, Qt.Key_Escape))
            tryCompare(mainWindow, "videoFullscreen", false)
            compare(transport.stopCalls, 0,
                    "Escape must only exit fullscreen")

            verify(nativeDropHelper.sendKey(mainWindow, Qt.Key_Escape))
            compare(transport.stopCalls, 0,
                    "Escape while windowed must not stop video playback")

            mainWindow.enterVideoFullscreen()
            tryCompare(mainWindow, "videoFullscreen", true)
            findChild(loader.item, "videoReturnButton").clicked()
            tryCompare(mainWindow, "videoFullscreen", false)
            compare(transport.stopCalls, 1)
            compare(videoState.dismissCalls, 1,
                    "return must dismiss video even when core stop cannot hide it")
            compare(transport.stopFullscreenSamples.length, 1)
            compare(transport.stopFullscreenSamples[0], false,
                    "stop must observe an already-windowed main window")
            tryCompare(loader, "active", false)
            tryVerify(function() { return loader.item === null })
            compare(shellLoader.item, originalShell,
                    "return must reveal the same preserved audio shell")

            videoState.visible = true
            tryCompare(loader, "active", true)
            videoState.visible = false
            tryCompare(loader, "active", false)
            tryVerify(function() { return loader.item === null })
        } finally {
            if (mainWindow.videoFullscreen)
                mainWindow.exitVideoFullscreen()
            mainWindow.playback = originalPlayback
            mainWindow.videoPlayback = originalVideoPlayback
            if (priorVisibility === Window.Maximized)
                mainWindow.showMaximized()
            else
                mainWindow.showNormal()
            transport.destroy()
            videoState.destroy()
        }
    }
}
