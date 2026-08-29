import QtQuick
import QtQuick.Layouts
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MainWindowControls"
    when: windowShown

    property var mainWindow: null
    property var task4StateSnapshot: null
    property var task4TemporaryTagKeys: []
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

    Component {
        id: recursiveBackdropDecoyComponent
        Item {
            width: 8
            height: 8
            SkinBackdrop { anchors.fill: parent }
        }
    }

    Component {
        id: nonFillMainFrameSiblingComponent
        DockedWindowFrame {
            width: 8
            height: 8
            windowRole: "main"
            showBorders: false
            showFill: false
        }
    }

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined", "testMainWindow context property should exist")
        mainWindow = testMainWindow
        verify(mainWindow, "Failed to create Main window")
        wait(50)
    }

    function init() {
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

    function cleanup() {
        for (var index = 0; index < task4TemporaryTagKeys.length; ++index)
            TagModel.removeTag(task4TemporaryTagKeys[index])
        if (!task4StateSnapshot)
            return
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

    function countObjectsNamed(parentObject, expectedName) {
        if (!parentObject)
            return 0
        var total = parentObject.objectName === expectedName ? 1 : 0
        var childItems = parentObject.children || []
        for (var index = 0; index < childItems.length; ++index)
            total += countObjectsNamed(childItems[index], expectedName)
        return total
    }

    function mainBackdropFrame() {
        var childItems = mainWindow.contentItem.children || []
        for (var index = 0; index < childItems.length; ++index) {
            var child = childItems[index]
            if (typeof child.windowRole !== "undefined"
                    && child.windowRole === "main"
                    && typeof child.showBorders !== "undefined"
                    && !child.showBorders
                    && typeof child.showFill !== "undefined"
                    && child.showFill === true)
                return child
        }
        return null
    }

    function backdropForFrame(frame) {
        if (!frame)
            return null
        var childItems = frame.children || []
        for (var index = 0; index < childItems.length; ++index) {
            var child = childItems[index]
            if (typeof child.item !== "undefined" && child.item
                    && child.item.objectName === "skinBackdrop")
                return child.item
        }
        return null
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
        var path = decodeURIComponent(testAudioUrl.toString()
                                      .replace(/^file:\/\/\//, ""))
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
               "equalizerButton should expose the real ten-band EQ")
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

    function test_equalizer_opens_compact_real_control_window() {
        var button = findChild(mainWindow, "equalizerButton")
        verify(button)
        mouseClick(button)
        var window = findChild(mainWindow, "equalizerWindow")
        tryVerify(function() { return window && window.visible }, 1000)
        compare(window.width, 520)
        compare(window.height, 307)
        compare(window.minimumWidth, 520)
        compare(window.minimumHeight, 307)
        var equalizerTitle = findChild(window, "equalizerTitle")
        var equalizerContent = findChild(window, "equalizerContent")
        verify(equalizerTitle,
               "compact EQ must retain system-readable title text")
        verify(equalizerContent,
               "EQ must lay out at native size instead of shrinking a large canvas")
        compare(equalizerContent.scale, 1)
        verify(equalizerTitle.font.pixelSize >= 14)
        compare(findChild(window, "equalizerHeaderPanel").height, 38)
        compare(findChild(window, "equalizerMinimizeButton").width, 30)
        verify(findChild(window, "equalizerEnabledSwitch"))
        compare(button.contentItem.rotation, 90)
        var previousThemeMode = SettingsController.themeMode
        SettingsController.themeMode = 0
        compare(button.icon.color.toString(), Theme.iconPrimary.toString())
        SettingsController.themeMode = 1
        compare(button.icon.color.toString(), Theme.iconPrimary.toString())
        SettingsController.themeMode = previousThemeMode
        verify(findChild(window, "equalizerResponseCurve"))
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
        compare(bands.count, 10)
        verify(findChild(window, "equalizerPreampSlider"))
        verify(findChild(window, "equalizerAutoProtection"))
        verify(findChild(window, "equalizerBypassButton"))
        verify(findChild(window, "equalizerResetButton"))
        EqualizerController.resetAll()
        var firstBand = bands.itemAt(0)
        verify(firstBand)
        firstBand.setGain(3.2)
        tryCompare(firstBand, "gainDb", 3.2)
        compare(EqualizerController.bandGain(0), 3.2)
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
        firstBand.setGain(6)
        tryCompare(firstBand, "gainDb", 6)
        var firstBandLabel = findChild(firstBand, "eqBandSlider-0-frequency")
        verify(firstBandLabel)
        mouseDoubleClickSequence(firstBandLabel, firstBandLabel.width / 2,
                                 firstBandLabel.height / 2)
        wait(80)
        compare(firstBand.gainDb, 0,
                "double-clicking an EQ frequency label must reset it to 0 dB")
        EqualizerController.bypassed = true
        tryCompare(findChild(window, "equalizerBypassButton"), "checked", true)
        EqualizerController.resetAll()
        EqualizerController.bypassed = false
        window.hide()
    }

    function test_empty_library_shows_startup_actions() {
        PlaybackController.pause()
        nativeDropHelper.clearTracks()
        tryVerify(function() { return LibraryModel.count === 0 }, 500)
        var startup = findChild(mainWindow, "emptyStartup")
        verify(startup, "empty startup surface should exist")
        verify(startup.visible, "empty startup surface should be visible")
        verify(findChild(startup, "openFileButton"), "open file action should exist")
        verify(findChild(startup, "importFolderButton"), "import folder action should exist")
        verify(findChild(mainWindow, "waveformModeButton"),
               "waveform mode action should exist in the bottom control bar")
        verify(findChild(mainWindow, "audioToolsButton").visible,
               "audio tools action should be visible in the bottom control bar")
        verify(findChild(mainWindow, "listWindowButton").visible,
               "playlist action should be visible in the bottom control bar")
        verify(!findChild(mainWindow, "miniPlayerButton").visible,
               "empty startup must not expose the mini player action")
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
        compare(formats.wrapMode, Text.NoWrap)
        compare(formats.lineCount, 1,
                "the supported-format explanation must never wrap")
        verify(formats.paintedWidth <= empty.width - 24,
               "the single format line must tighten to fit the minimum width; "
               + "painted=" + formats.paintedWidth + ", width=" + formats.width
               + ", font=" + formats.font.pixelSize)
        verify(formats.font.pixelSize < 13,
               "the minimum-width state must tighten typography before wrapping")
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
        compare(libraryFormats.font.pixelSize, 13,
                "the regular empty-library typography must stay unchanged")
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
        verify(!findChild(mainWindow, "emptyStartup").visible,
               "startup actions should not cover import errors")
    }

    function test_import_files_reaches_real_controller() {
        verify(testAudioUrl.toString().length > 0,
               "generated audio fixture should be available")
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
        compare(Math.round(center.y + center.height / 2),
                Math.round(listButton.y + listButton.height / 2))
        compare(Math.round(center.y + center.height / 2),
                Math.round(miniButton.y + miniButton.height / 2))
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
            ["trackMenuRename", "重命名"],
            ["trackMenuMoveFile", "移动到指定文件夹"],
            ["trackMenuCopyFile", "复制到指定文件夹"],
            ["trackMenuRemove", "从列表删除"],
            ["trackMenuTrash", "彻底删除至回收站"],
            ["trackMenuRelocate", "重新定位文件"]
        ]
        for (var index = 0; index < expected.length; ++index) {
            var action = findChild(menu, expected[index][0])
            verify(action, "missing context action " + expected[index][0])
            compare(action.text !== undefined ? action.text : action.title,
                    expected[index][1])
        }
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
        compare(list.rowHeight, 42)
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
            var expectedRowHeight = enabled ? 62 : 42
            var expectedHeight = 38 + 56 + 10 * expectedRowHeight + 54
            compare(listWindow.height, expectedHeight)
            compare(filter.height, 54)
            tryCompare(trackList, "height", 56 + 10 * expectedRowHeight)
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

    function test_qa_track_details_capture_opens_real_details_panel() {
        var trackIds = nativeDropHelper.ensureSortableTracks()
        compare(trackIds.length, 3)
        var filterModel = findChild(mainWindow, "filterModel")
        filterModel.category = "all"
        filterModel.tagKey = ""
        filterModel.resourceFolder = ""
        filterModel.searchText = ""
        var listWindow = createTemporaryObject(defaultListWindowComponent,
                                               testCase,
                                               { "filterModel": filterModel })
        verify(listWindow)
        var trackList = findChild(listWindow, "sharedTrackList")
        verify(trackList)
        tryVerify(function() { return trackList.count > 0 }, 500)

        trackList.openFirstDetailsForQa()

        var detailsPanel = findChild(trackList, "trackDetailsPanel")
        verify(detailsPanel,
               "QA capture must expose the real track-details popup")
        tryVerify(function() { return detailsPanel.visible }, 500)
        verify(String(detailsPanel.details.fileName || "").length > 0,
               "the opened popup must contain a real track filename")
        verify(String(detailsPanel.details.format || "").length > 0,
               "the opened popup must contain a real track format")
        detailsPanel.close()
        listWindow.destroy()
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
        var playablePath = decodeURIComponent(testAudioUrl.toString()
                                              .replace(/^file:\/\/\//, ""))
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
        var playablePath = decodeURIComponent(testAudioUrl.toString()
                                              .replace(/^file:\/\/\//, ""))
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
        var playablePath = decodeURIComponent(testAudioUrl.toString()
                                              .replace(/^file:\/\/\//, ""))
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
        list.destroy()
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
        var initialFolderCount = LibraryManagerController.monitoredFolders.length
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
        compare(LibraryManagerController.monitoredFolders.length,
                initialFolderCount + 1,
                "audio import state must not block resource directories")

        tryCompare(importFinished, "count", 1, 5000)
        compare(LibraryModel.count, initialLibraryCount + 1,
                "busy drops must not queue or import audio silently")
        var firstImportedId = ImportController.importedTrackIds[0]
        verify(firstImportedId)

        compare(nativeDropHelper.sendUrls(
                    dropTarget, [rejectedAudio, mixedAudio]), false,
                "resource audio must remain rejected after imports finish")
        wait(0)
        compare(importFinished.count, 1)
        compare(LibraryModel.count, initialLibraryCount + 1)

        var folderPath = decodeURIComponent(busyFolder.toString()
                                           .replace(/^file:\/\/\//, ""))
        folderPath = folderPath.replace(/\\/g, "/")
        verify(LibraryManagerController.removeMonitoredFolder(folderPath))
        LibraryModel.removeTrack(firstImportedId)
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
        var rootPath = LibraryManagerController.classifyDropUrl(rootUrl).path
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
        var waveform = findChild(mainWindow, "mainWaveform")
        var seekSurface = waveform
        verify(waveform, "main waveform should exist after importing audio")
        verify(seekSurface, "visible waveform surface should own seeking")

        var playablePath = decodeURIComponent(testAudioUrl.toString()
                                              .replace(/^file:\/\/\//, ""))
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
        compare(playbackGuide.color.toString(), "#002fa7")
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
        tryCompare(SettingsController, "waveformMode", 1)
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

    function test_waveform_hover_surface_covers_played_and_unplayed_regions() {
        var previousPreview = SettingsController.waveformHoverTimePreview
        SettingsController.waveformHoverTimePreview = true
        var surface = findChild(mainWindow, "mainWaveform")
        var interactionSurface = findChild(mainWindow, "waveformInteractionSurface")
        var guide = findChild(mainWindow, "waveformHoverGuide")
        verify(surface)
        verify(interactionSurface)
        verify(guide)
        verify(surface.enabled)
        compare(surface.timeForX(surface.width * 0.15),
                Math.round(surface.duration * 0.15))
        compare(surface.timeForX(surface.width * 0.85),
                Math.round(surface.duration * 0.85))
        compare(surface.pixelForTime(surface.duration), surface.width)
        interactionSurface.updatePreviewAt(interactionSurface.width * 0.15)
        tryVerify(function() { return guide.visible }, 300)
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
        verify(guide && playedClip)
        SettingsController.waveformPlaybackGuide = true
        tryCompare(guide, "visible", true)
        SettingsController.waveformPlaybackGuide = false
        tryCompare(guide, "visible", false)
        verify(playedClip.visible,
               "disabling the guide must retain the played-color region")
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

    function test_generated_skin_routes_shared_backdrop_and_play_ring() {
        var savedMode = SettingsController.skinColorMode
        var savedPreset = SettingsController.skinPreset
        var savedKind = SettingsController.skinCustomKind
        var savedStart = SettingsController.skinCustomColor
        var savedMiddle = SettingsController.skinCustomColorMiddle
        var savedEnd = SettingsController.skinCustomColorEnd

        try {
            SettingsController.selectDefaultSkin()
            wait(0)
            compare(ThemeManager.backdropStart.toString(),
                    ThemeManager.backdropMiddle.toString())
            compare(ThemeManager.backdropMiddle.toString(),
                    ThemeManager.backdropEnd.toString())
            compare(Theme.playRingPlaying.toString(), "#00e676")
            compare(Theme.playRingPaused.toString(), "#ffb020")

            var frame = mainBackdropFrame()
            verify(frame, "main fill DockedWindowFrame must exist")
            var backdrop = backdropForFrame(frame)
            verify(backdrop, "main window must render one shared SkinBackdrop")

            SettingsController.selectSkinPreset("aurora")
            wait(0)
            verify(ThemeManager.backdropStart.toString()
                   !== ThemeManager.backdropMiddle.toString())
            verify(ThemeManager.backdropMiddle.toString()
                   !== ThemeManager.backdropEnd.toString())
            var gradientPaint = findChild(backdrop, "skinBackdropGradient")
            verify(gradientPaint, "SkinBackdrop must expose its rendered gradient")
            compare(gradientPaint.gradient.stops[0].color.toString(),
                    ThemeManager.backdropStart.toString())
            compare(gradientPaint.gradient.stops[1].color.toString(),
                    ThemeManager.backdropMiddle.toString())
            compare(gradientPaint.gradient.stops[2].color.toString(),
                    ThemeManager.backdropEnd.toString())
            compare(Theme.playRingPlaying.toString(), Theme.accent.toString())
            compare(Theme.playRingPaused.toString(), Theme.accent.toString())
            compare(findChild(mainWindow, "playButtonBody").border.color.toString(),
                    Theme.accent.toString())
        } finally {
            SettingsController.setSkinCustomConfiguration(
                        savedKind, savedStart, savedMiddle, savedEnd)
            if (savedMode === 0)
                SettingsController.selectDefaultSkin()
            else if (savedMode === 1)
                SettingsController.selectSkinPreset(savedPreset)
        }
    }

    function test_main_skin_lookup_rejects_recursive_decoy() {
        var frame = mainBackdropFrame()
        verify(frame, "main fill DockedWindowFrame must exist")
        var decoy = null

        try {
            frame.showFill = false
            wait(0)
            compare(backdropForFrame(frame), null)

            decoy = recursiveBackdropDecoyComponent.createObject(
                        mainWindow.contentItem)
            verify(decoy, "recursive backdrop decoy must be created")
            verify(findChild(mainWindow, "skinBackdrop"),
                   "fixture must prove broad recursive lookup accepts the decoy")
            compare(backdropForFrame(frame), null,
                    "scoped lookup must reject a backdrop outside Main fill frame")
        } finally {
            if (decoy)
                decoy.destroy()
            frame.showFill = true
            wait(0)
        }
    }

    function test_main_skin_lookup_requires_fill_frame() {
        var frame = mainBackdropFrame()
        verify(frame, "main fill DockedWindowFrame must exist")
        var sibling = null

        try {
            frame.showFill = false
            wait(0)
            sibling = nonFillMainFrameSiblingComponent.createObject(
                        mainWindow.contentItem)
            verify(sibling, "non-fill Main frame sibling must be created")
            compare(sibling.windowRole, "main")
            compare(sibling.showBorders, false)
            compare(sibling.showFill, false)
            compare(mainBackdropFrame(), null,
                    "selector must reject every non-fill Main frame")
        } finally {
            if (sibling)
                sibling.destroy()
            frame.showFill = true
            wait(0)
        }
    }

    function test_volume_control_uses_compact_white_handle_and_percentage() {
        var mute = findChild(mainWindow, "muteButton")
        var slider = findChild(mainWindow, "volumeSlider")
        var percent = findChild(mainWindow, "volumePercentLabel")
        verify(mute)
        verify(slider)
        verify(percent)
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
            var coreCenter = core.mapToItem(
                        mainWindow.contentItem, core.width / 2, 0).x
            compare(Math.round(coreCenter), Math.round(controlsCenter),
                    "the core group geometric center must equal the player center")
        }

        volume.expandedForQa = false
        wait(260)
        verifyCoreCentered()
        var widthBefore = core.width
        var playCenterBefore = play.mapToItem(controls,
                                              play.width / 2,
                                              play.height / 2).x
        volume.expandedForQa = true
        wait(220)
        compare(core.width, widthBefore,
                "the right-side volume flyout must not enter core layout width")
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

    function test_main_volume_flyout_stays_open_for_two_seconds_after_leave() {
        var control = findChild(mainWindow, "mainVolumeControl")
        var closeTimer = findChild(mainWindow, "mainVolumeCloseTimer")
        var slider = findChild(mainWindow, "volumeSlider")
        verify(control && closeTimer && slider)
        control.expandedForQa = true
        slider.forceActiveFocus()
        closeTimer.restart()
        wait(1600)
        verify(control.expandedForQa,
               "main volume must stay open for the two-second pointer transfer")
        wait(550)
        tryVerify(function() { return !control.expandedForQa }, 300)
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
        compare(findChild(filter, "keywordModule").width, 184)
        compare(findChild(filter, "librarySearchField").placeholderText,
                "歌曲 · 艺术家 · 专辑 · 标签")
        verify(findChild(filter, "librarySearchIcon"))
        compare(findChild(filter, "bpmModule").width, 216)
        var bpmRange = findChild(filter, "bpmRange")
        compare(bpmRange.first.handle.width, 14)
        compare(bpmRange.second.handle.width, 14)
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
        var previousMode = SettingsController.waveformMode
        SettingsController.waveformMode = 0
        tryVerify(function() {
            return waveform.layers.mix && waveform.layers.mix.length > 0
        }, 5000)

        tryVerify(function() {
            return waveform.layers.mix.length > 0
                    && waveform.sampleRate > 0
                    && waveform.totalSamples > 0
                    && waveform.peakCount === waveform.layers.mix.length
        })
        compare(waveform.visualMode, 0)
        compare(waveform.baseColor.toString(),
                SettingsController.waveformSolidBaseColor)
        compare(waveform.progressColor.toString(),
                SettingsController.waveformSolidProgressColor)

        SettingsController.waveformMode = 1
        tryVerify(function() {
            return waveform.layers.mix.length > 0
                    && waveform.sampleRate > 0
                    && waveform.totalSamples > 0
                    && waveform.peakCount === waveform.layers.mix.length
        })
        compare(waveform.visualMode, 1)

        PlaybackController.play()
        SettingsController.waveformMode = 2
        tryVerify(function() {
            if (waveform.visualMode !== 2
                    || waveform.peaks.length < 16
                    || waveform.peaks.length % 2 !== 0)
                return false
            for (var index = 0; index < waveform.peaks.length; ++index) {
                if (waveform.peaks[index] > 0.05)
                    return true
            }
            return false
        }, 2000)

        SettingsController.waveformMode = previousMode
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

    function test_z_playing_row_uses_three_independent_spectrum_bars() {
        verify(PlaybackController.currentTrackId.length > 0)
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        tryVerify(function() { return list.count > 0 })

        var indicator = findChild(list, "playingBarsIndicator")
        verify(indicator)
        var currentRow = findChild(list, "currentTrackRow")
        verify(currentRow)
        compare(currentRow.color.toString(), Theme.currentTrackSelection.toString())
        compare(indicator.barCount, 3)
        compare(indicator.barColor.toString(), Theme.waveformMagenta.toString())
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
        compare(workspace.leftColumnWidth, 208)
        compare(workspace.rightColumnWidth, 248)
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
        verify(findChild(trackList, "trackHeaderArtist").x
               < findChild(trackList, "trackHeaderAlbum").x,
               "艺术家列必须在专辑列之前")
        compare(findChild(trackList, "trackHeaderDuration").text, "时长")

        var tagPanel = findChild(workspace, "tagManagementPanel")
        verify(tagPanel)
        tryCompare(tagPanel, "width", 248)
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

        var names = ["好", "中文", "好听", "音乐", "摇滚", "流行",
                     "民谣", "电子", "古典", "爵士", "轻音乐",
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
        verify(firstPill.resolvedSurface.toString()
               === Theme.tagPillSelectedSurface.toString(),
               "selected tags must use the low-saturation blue token")

        var pointer = findChild(firstPill, "tagPillPointerArea-" + keys[0])
        verify(pointer)
        panel.selectTag(keys[0])
        tryVerify(function() { return !firstPill.selectedVisual }, 500)
        mouseClick(pointer, pointer.width / 2, pointer.height / 2)
        tryVerify(function() { return pointer.activeFocus }, 500,
                  "clicking a tag pill must give it focus")
        panel.selectTag(keys[0])
        tryVerify(function() { return !firstPill.selectedVisual }, 500)
        mouseMove(pointer, pointer.width / 2, pointer.height / 2)
        tryVerify(function() { return firstPill.hoveredVisual }, 500)
        verify(firstPill.resolvedSurface.toString()
               === Theme.tagPillHoverSurface.toString())

        var previousMode = SettingsController.themeMode
        SettingsController.themeMode = 1
        verify(Theme.tagPillSurface !== Theme.tagPillSelectedSurface)
        verify(Theme.tagPillText !== Theme.tagPillSecondaryText)
        verify(colorContrast(Theme.tagPillText, Theme.tagPillSurface) >= 3)
        verify(colorContrast(Theme.tagPillSecondaryText, Theme.tagPillSurface) >= 3)
        SettingsController.themeMode = 0
        verify(Theme.tagPillSurface !== Theme.tagPillSelectedSurface)
        verify(Theme.tagPillText !== Theme.tagPillSecondaryText)
        verify(colorContrast(Theme.tagPillText, Theme.tagPillSurface) >= 3)
        verify(colorContrast(Theme.tagPillSecondaryText, Theme.tagPillSurface) >= 3)
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
        var colorDialog = findChild(panel, "tagColorDialog")
        verify(colorDialog)
        ignoreWarning("qrc:/qt-project.org/imports/QtQuick/Dialogs/quickimpl/qml/ColorDialog.qml:12:1: QML ColorDialog: Binding loop detected for property \"implicitWidth\"")
        mouseClick(colorAction, colorAction.width / 2,
                   colorAction.height / 2)
        colorDialog.selectedColor = "#123456"
        colorDialog.reject()
        compare(TagModel.data(TagModel.index(renamedRow, 0),
                              TagModel.ColorRole).toString(), beforeColor)
        compare(colorRequested.count, 0,
                "cancel must not emit a color mutation signal")

        mouseClick(pointerArea, pointerArea.width / 2,
                   pointerArea.height / 2, Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        mouseClick(colorAction, colorAction.width / 2,
                   colorAction.height / 2)
        colorDialog.selectedColor = "#123456"
        colorDialog.accept()
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

        verify(stackCountBefore === 4 && stackCountAfter === 4
               && stackIndexBefore === 0 && stackIndexAfter === 0
               && dragStarted && dragFollowed,
               "ListWindow drag input must preserve the four page stack and "
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
        var initialFolderCount = LibraryManagerController.monitoredFolders.length
        var initialLibraryCount = LibraryModel.count

        var centralFolder = nativeDropHelper.createDropDirectory()
        verify(nativeDropHelper.sendUrls(centerTarget, [centralFolder]))
        tryVerify(function() { return !ImportController.busy }, 3000)
        compare(LibraryManagerController.monitoredFolders.length,
                initialFolderCount,
                "a central directory drop must not become a monitored root")
        ImportController.clearErrors()

        var tagFolder = nativeDropHelper.createDropDirectory()
        verify(nativeDropHelper.sendUrls(tagTarget, [tagFolder]))
        tryVerify(function() { return !ImportController.busy }, 3000)
        compare(LibraryManagerController.monitoredFolders.length,
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
            "target": LibraryManagerController,
            "signalName": "monitoredFoldersChanged"
        })
        verify(importFinished && rootsChanged)

        var audioAccepted = nativeDropHelper.sendUrls(dropTarget,
                                                       [copiedAudio])
        tryVerify(function() { return !ImportController.busy }, 5000)
        var audioOnlyLibraryCount = LibraryModel.count
        if (audioOnlyLibraryCount > initialLibraryCount) {
            for (var importedId of ImportController.importedTrackIds)
                LibraryModel.removeTrack(importedId)
        }
        compare(audioOnlyLibraryCount, initialLibraryCount,
                "resource audio drops must not enter the music library")
        compare(audioAccepted, false,
                "the resource area must reject audio-only drops")

        verify(nativeDropHelper.sendUrls(dropTarget,
                                         [folderUrl, folderUrl,
                                          copiedAudio, copiedAudio,
                                          ignoredFile, invalidUrl]))
        tryVerify(function() {
            return LibraryManagerController.monitoredFolders.length
                    === initialFolderCount + 1
        }, 1000)
        compare(ImportController.busy, false,
                "resource drops must not start an audio import")
        compare(rootsChanged.count, 1,
                "duplicate directory URLs must add one monitored root")
        compare(importFinished.count, 0,
                "resource drops must ignore audio files in mixed batches")
        verify(ImportController.errors.length === 0)
        compare(LibraryModel.count, initialLibraryCount,
                "a mixed resource drop must leave the music library unchanged")
        var folderPath = decodeURIComponent(folderUrl.toString()
                                           .replace(/^file:\/\/\//, ""))
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
            return LibraryManagerController.monitoredFolders.length
                    === initialFolderCount + 2
        }, 1000)
        var secondFolderPath = decodeURIComponent(secondFolderUrl.toString()
                .replace(/^file:\/\/\//, "")).replace(/\\/g, "/")
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
            return LibraryManagerController.monitoredFolders.length
                    === initialFolderCount + 1
        }, 1000)
        verify(nativeDropHelper.pathExists(secondFolderUrl))
        verify(LibraryManagerController.monitoredFolders.some(function(path) {
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
        tryVerify(function() { return !LibraryManagerController.scanning }, 3000)
        var scanFinished = signalSpyComponent.createObject(testCase, {
            "target": LibraryManagerController,
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
            return LibraryManagerController.monitoredFolders.length
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
        var list = trackListComponent.createObject(mainWindow.contentItem)
        verify(list)
        tryVerify(function() { return list.count > 0 })
        compare(list.rowHeight, 42)
        compare(list.thumbnailItemCount, 0)
        verify(!findChild(list, "trackWaveformThumbnail"))

        TrackWaveformThumbnailProvider.refresh()
        var readsBefore = TrackWaveformThumbnailProvider.diagnostics().cacheReadAttempts
        SettingsController.listWaveformThumbnailEnabled = true
        tryCompare(list, "rowHeight", 62)
        tryVerify(function() { return list.thumbnailItemCount > 0 })
        verify(findChild(list.itemAtIndex(0), "trackWaveformThumbnail"))
        compare(findChild(list.itemAtIndex(0), "trackCover").width, 34)
        compare(findChild(list.itemAtIndex(0), "trackCover").height, 34)
        tryVerify(function() {
            return TrackWaveformThumbnailProvider.diagnostics().cacheReadAttempts
                    > readsBefore
        }, 3000)
        wait(100)
        var settledReads = TrackWaveformThumbnailProvider.diagnostics().cacheReadAttempts
        SettingsController.listWaveformThumbnailMode = "Mono"
        wait(100)
        compare(TrackWaveformThumbnailProvider.diagnostics().cacheReadAttempts,
                settledReads,
                "mode changes must recolor without reading waveform data again")

        SettingsController.listWaveformThumbnailEnabled = false
        tryCompare(list, "rowHeight", 42)
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
            compare(tagPill.height, 26)

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
            compare(chevron.icon.width, 18)
            compare(chevron.icon.height, 18)
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
                        "mode": "Color36"
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

    function test_empty_startup_uses_compact_reference_structure() {
        compare(mainWindow.width, 960)
        compare(mainWindow.height, 298)

        var startup = findChild(mainWindow, "emptyStartup")
        var controls = findChild(mainWindow, "playerControls")
        verify(startup.visible)
        verify(controls.emptyMode)
        verify(findChild(mainWindow, "titleBrand").visible,
               "brand should be visible in the empty title bar")
        compare(findChild(mainWindow, "titleBrandText").font.italic, false)
        verify(findChild(startup, "startupTitle"),
               "compact startup title should exist")
        verify(findChild(startup, "startupActionArea"),
               "compact startup actions should exist")
        verify(!findChild(startup, "startupHeroArtwork"),
               "the superseded hero artwork should not exist")
    }

    function test_empty_startup_never_overlaps_the_bottom_controls() {
        nativeDropHelper.clearLibrary()
        tryVerify(function() { return LibraryModel.count === 0 })
        var startup = findChild(mainWindow, "emptyStartup")
        var controls = findChild(mainWindow, "playerControls")
        var actionArea = findChild(startup, "startupActionArea")
        verify(startup && controls && actionArea)
        verify(actionArea.y + actionArea.height <= controls.y,
               "startup actions and format hint must stay above playback controls")
    }

    function test_main_window_allows_a_smaller_responsive_native_size() {
        compare(mainWindow.minimumWidth, 612)
        compare(mainWindow.minimumHeight, 228)
    }

    function test_main_window_keeps_player_content_visible_at_minimum_size() {
        if (LibraryModel.count === 0)
            nativeDropHelper.ensureSortableTracks()
        var oldWidth = mainWindow.width
        var oldHeight = mainWindow.height
        mainWindow.width = mainWindow.minimumWidth
        mainWindow.height = mainWindow.minimumHeight
        wait(50)

        var pane = findChild(mainWindow, "playerPane")
        var controls = findChild(mainWindow, "playerControls")
        var cover = findChild(mainWindow, "playerCover")
        var waveform = findChild(mainWindow, "mainWaveform")
        verify(pane && controls && cover && waveform)
        tryVerify(function() { return pane.visible }, 1000)
        verify(pane.y + pane.height <= controls.y + 1,
               "player metadata/waveform must not cover playback controls")
        verify(controls.y + controls.height <= mainWindow.contentItem.height + 1,
               "playback controls must remain inside the small window")
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
        verify(headerDragArea.width > settingsWindow.width * 0.50)
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
        verify(scroll.contentHeight > scroll.availableHeight,
               "settings content must remain reachable in the compact main window")
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

    function test_settings_language_combo_renders_flag_and_language() {
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
        compare(combo.valueModel.length, 4)
        compare(combo.valueModel[0].text, "🇨🇳 中文")
        compare(combo.valueModel[1].text, "🇺🇸 English")
        compare(combo.valueModel[2].text, "🇹🇭 ภาษาไทย")
        compare(combo.valueModel[3].text, "🇻🇳 Tiếng Việt")
        SettingsController.language = "vi"
        tryCompare(combo, "currentIndex", 3)
        tryVerify(function() { return combo.contentItem.text === "🇻🇳 Tiếng Việt" })
        SettingsController.language = "zh"
        if (ownsPage) {
            page.saveAndClose()
            page.destroy()
        } else
            page.close()
    }

    function test_settings_transcode_controls_are_split_and_scroll_tracks_section() {
        var page = findChild(mainWindow, "settingsPage")
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
        scroll.contentItem.contentY = Math.max(
                    0, scroll.contentHeight - scroll.availableHeight)
        tryCompare(page, "selectedSection", 6)
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

    function test_settings_y_about_uses_one_name_and_version_line() {
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
        compare(productLine.text, "AgPlayer " + SettingsController.version)
        verify(productLine.font.weight >= Font.Bold)
        verify(!findChild(page, "aboutStandaloneVersion"),
               "about page must not repeat the product or version")
        page.close()
    }

    function test_settings_waveform_controls_are_live() {
        var page = findChild(mainWindow, "settingsPage")
        verify(page)
        page.open()
        page.selectedSection = 2
        wait(250)

        var heightStepper = findChild(page, "waveformHeightStepper")
        var densityStepper = findChild(page, "waveformDensityStepper")
        var thicknessStepper = findChild(page, "waveformThicknessStepper")
        var aggregationCombo = findChild(page, "waveformAggregationCombo")
        var resetButton = findChild(page, "waveformResetButton")
        var listThumbnailSwitch = findChild(
                    page, "listWaveformThumbnailEnabledControl")
        var listThumbnailMode = findChild(
                    page, "listWaveformThumbnailModeControl")
        verify(heightStepper)
        verify(densityStepper)
        verify(thicknessStepper)
        verify(aggregationCombo)
        verify(resetButton)
        verify(listThumbnailSwitch)
        verify(listThumbnailMode)

        SettingsController.listWaveformThumbnailEnabled = true
        SettingsController.listWaveformThumbnailMode = "Color36"
        tryCompare(listThumbnailSwitch, "checked", true)
        tryCompare(listThumbnailMode, "currentValue", "Color36")
        mouseClick(listThumbnailSwitch,
                   listThumbnailSwitch.width / 2,
                   listThumbnailSwitch.height / 2)
        tryCompare(SettingsController, "listWaveformThumbnailEnabled", false)
        tryCompare(listThumbnailMode, "enabled", false)
        SettingsController.listWaveformThumbnailEnabled = true
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

        heightStepper.increase()
        densityStepper.decrease()
        thicknessStepper.increase()
        tryCompare(SettingsController, "waveformHeight", 1.3)
        tryCompare(SettingsController, "waveformDensity", 3.0)
        tryCompare(SettingsController, "waveformThickness", 2.3)

        resetButton.clicked()
        tryCompare(SettingsController, "waveformHeight", 0.8)
        tryCompare(SettingsController, "waveformDensity", 2.0)
        tryCompare(SettingsController, "waveformThickness", 1.0)
        tryCompare(SettingsController, "waveformPeakAlgorithm", 0)
        tryCompare(SettingsController, "listWaveformThumbnailEnabled", true)
        tryCompare(SettingsController, "listWaveformThumbnailMode", "Color36")
        page.close()
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

        SettingsController.themeMode = 0
        tryCompare(Theme, "isLight", false)
        var darkBackground = Theme.background.toString()
        var darkText = Theme.primaryText.toString()
        compare(darkBackground, "#101114")
        compare(findChild(mainWindow, "playButtonBody").border.color.toString(),
                (PlaybackController.state === PlaybackController.Playing
                 ? Theme.playRingPlaying : Theme.playRingPaused).toString())

        SettingsController.themeMode = 1
        compare(SettingsController.themeMode, 1)
        tryCompare(Theme, "isLight", true)
        compare(Theme.accentText.toString(), Theme.onCyanText.toString())
        verify(Theme.background.toString() !== darkBackground,
               "light mode should replace the dark surface")
        verify(Theme.primaryText.toString() !== darkText,
               "light mode should replace the dark text color")
        compare(findChild(mainWindow, "settingsButton").icon.color.toString(),
                Theme.iconSecondary.toString())
        compare(findChild(mainWindow, "playButtonBody").border.color.toString(),
                (PlaybackController.state === PlaybackController.Playing
                 ? Theme.playRingPlaying : Theme.playRingPaused).toString())

        SettingsController.themeMode = 2
        tryCompare(Theme, "followsSystem", true)
        compare(Theme.requestedMode, 2)
        compare(Theme.effectiveMode, Theme.systemIsLight ? 1 : 0)
        compare(Theme.background.toString(),
                Theme.systemIsLight ? "#f5f5f7" : "#101114")
        compare(Theme.cyan.toString(), Theme.accent.toString())
        compare(Theme.waveformCyan.toString(), "#00d4ff")

        SettingsController.themeMode = previousMode
    }

    function test_rating_stars_use_one_solid_orange_color() {
        compare(Theme.ratingGold.toString(), "#ff9800")
        for (var index = 0; index < 5; ++index)
            compare(Theme.ratingColor(index).toString(), "#ff9800")
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
}
