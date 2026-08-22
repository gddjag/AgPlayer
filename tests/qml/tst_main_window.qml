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
        id: libraryManagerComponent
        LibraryManagerPage {
            width: 1200
            height: 760
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
        id: trackWaveformThumbnailItemComponent
        TrackWaveformThumbnailItem {
            width: 128
            height: 10
        }
    }

    Component {
        id: fakeThumbnailProviderComponent
        QtObject {
            property int requestCount: 0
            property int cancelCount: 0
            property var requests: []
            property var cancellations: []
            property string lastTrackId: ""
            property string lastSourcePath: ""
            property int lastGeneration: -1
            signal thumbnailReady(string trackId, int generation, var peaks)

            function request(trackId, sourcePath, generation) {
                requestCount += 1
                var nextRequests = requests.slice()
                nextRequests.push({ "trackId": trackId,
                                    "sourcePath": sourcePath,
                                    "generation": generation })
                requests = nextRequests
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

    function tagRowForKey(key) {
        for (var row = 0; row < TagModel.rowCount(); ++row) {
            var modelIndex = TagModel.index(row, 0)
            if (TagModel.data(modelIndex, TagModel.KeyRole) === key)
                return row
        }
        return -1
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
        // application-owned model graph. The list integration now owns visible
        // thumbnail requests, so only the disabled state is required to be idle.
        verify(TagModel === expectedTagModel)
        verify(LibraryNavigationModel === expectedLibraryNavigationModel)
        verify(LibraryManagerController === expectedLibraryManagerController)
        verify(TrackWaveformThumbnailProvider === expectedThumbnailProvider)

        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        SettingsController.listWaveformThumbnailEnabled = false
        tryVerify(function() {
            return TrackWaveformThumbnailProvider.diagnostics().queuedJobs === 0
        })

        var item = trackWaveformThumbnailItemComponent.createObject(
                    mainWindow.contentItem)
        verify(item)
        item.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
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
        verify(equalizerTitle.font.pixelSize >= 15)
        compare(button.contentItem.rotation, 90)
        var previousThemeMode = Theme.mode
        Theme.mode = 0
        compare(button.icon.color, "#ffffff")
        Theme.mode = 1
        compare(button.icon.color, "#000000")
        Theme.mode = previousThemeMode
        verify(findChild(window, "equalizerResponseCurve"))
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
            "playlistMode": true
        })
        verify(empty)
        compare(findChild(empty, "emptyLibraryTitle").text, "Import music")
        verify(findChild(empty, "emptyLibraryFormats").text.indexOf("MP3") >= 0)
        compare(findChild(empty, "emptyImportButton").text, "Import music")
        empty.destroy()
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

    function test_library_manager_summary_cards_are_real_filters() {
        var page = libraryManagerComponent.createObject(mainWindow.contentItem)
        verify(page)
        var manager = page.manager
        verify(findChild(page, "libraryManagerController"),
               "the existing controller object name must remain available")
        var backup = findChild(page, "libraryBackupButton")
        var backupMenu = findChild(page, "libraryBackupMenu")
        var backupAction = findChild(page, "libraryBackupAction")
        var importAction = findChild(page, "libraryImportBackupAction")
        verify(manager && backup && backupMenu && backupAction && importAction,
               "library manager must expose backup and import actions")
        compare(backup.text, "备份/导入")
        compare(backupAction.text, "备份曲库")
        compare(importAction.text, "导入备份")
        verify(manager.defaultBackupUrl.toString().endsWith(".db"))
        var storage = findChild(page, "librarySummaryCard-storage")
        var missing = findChild(page, "librarySummaryCard-missing")
        verify(storage)
        verify(missing)
        mouseClick(storage)
        tryCompare(storage.border, "color", Theme.accent)
        mouseClick(missing)
        tryCompare(missing.border, "color", Theme.accent)
        verify(storage.border.color.toString() !== Theme.accent.toString())
        mouseClick(storage)
        tryCompare(storage.border, "color", Theme.accent)

        var trackView = findChild(page, "libraryManagerTrackList")
        verify(trackView)
        tryVerify(function() {
            return !manager.scanning && trackView.count > 0
        }, 5000)
        var firstRow = trackView.itemAtIndex(0)
        verify(firstRow)
        var favoriteButton = findChild(firstRow, "libraryTrackFavorite")
        var ratingStar = findChild(firstRow, "libraryTrackRatingStar")
        verify(favoriteButton && ratingStar)
        var favoriteTrackId = firstRow.trackId
        var favoriteBefore = firstRow.favorite
        mouseClick(favoriteButton)
        tryVerify(function() {
            var refreshed = trackView.itemAtIndex(0)
            return refreshed && refreshed.trackId === favoriteTrackId
                   && refreshed.favorite !== favoriteBefore
        }, 500)
        firstRow = trackView.itemAtIndex(0)
        favoriteButton = findChild(firstRow, "libraryTrackFavorite")
        mouseClick(favoriteButton)
        tryVerify(function() {
            var refreshed = trackView.itemAtIndex(0)
            return refreshed && refreshed.trackId === favoriteTrackId
                   && refreshed.favorite === favoriteBefore
        }, 500)
        firstRow = trackView.itemAtIndex(0)
        ratingStar = findChild(firstRow, "libraryTrackRatingStar")
        var ratingBefore = ratingStar.icon.source.toString()
        mouseClick(ratingStar)
        tryVerify(function() {
            var refreshed = trackView.itemAtIndex(0)
            var refreshedStar = refreshed
                    ? findChild(refreshed, "libraryTrackRatingStar") : null
            return refreshedStar
                   && refreshedStar.icon.source.toString() !== ratingBefore
        }, 500)
        mouseClick(firstRow, firstRow.width / 2, firstRow.height / 2,
                   Qt.RightButton)
        var trackMenu = findChild(page, "libraryManagerTrackMenu")
        tryVerify(function() { return trackMenu && trackMenu.visible }, 500)
        var expectedActions = [
            "libraryTrackPlay", "libraryTrackPlayNext",
            "libraryTrackAddToPlaylist", "libraryTrackAudioTools",
            "libraryTrackShowFolder", "libraryTrackCopyPath",
            "libraryTrackTag",
            "libraryTrackRename", "libraryTrackMoveFile",
            "libraryTrackCopyFile", "libraryTrackRemove",
            "libraryTrackTrash", "libraryTrackRelocate"
        ]
        for (var actionIndex = 0; actionIndex < expectedActions.length;
             ++actionIndex) {
            verify(findChild(trackMenu, expectedActions[actionIndex]),
                   "missing library context action "
                   + expectedActions[actionIndex])
        }
        trackMenu.close()

        var detailsPanel = findChild(page, "libraryTrackDetailsPanel")
        var detailsContent = findChild(page, "libraryTrackDetailsContent")
        verify(detailsPanel && detailsContent,
               "library track details must be a non-scrolling full-height panel")
        verify(detailsContent.height <= detailsPanel.height,
               "library details must fit without an internal scrollbar")
        verify(!findChild(page, "libraryDetailsPlay"))
        verify(!findChild(page, "libraryDetailsQueue"))
        verify(!findChild(page, "libraryDetailsFavorite"))
        verify(findChild(page, "libraryDetailsFormat"))
        verify(findChild(page, "libraryDetailsBitrate"))
        verify(page.preferredWindowHeight >= 840)
        verify(page.preferredWindowHeight
               >= detailsContent.implicitHeight + 250,
               "library window height must follow the complete details content")
        var managerBpmRange = findChild(page, "libraryManagerBpmRange")
        verify(managerBpmRange)
        compare(managerBpmRange.first.handle.width, 12)
        compare(managerBpmRange.second.handle.width, 12)
        page.destroy()
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
        var dragProxy = findChild(firstRow, "trackDragProxy")
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
        var listWindow = createTemporaryObject(defaultListWindowComponent,
                                               testCase)
        verify(listWindow)
        compare(listWindow.height, 570)
        listWindow.destroy()
    }

    function test_library_manager_uses_ten_row_scroll_viewport() {
        var manager = libraryManagerComponent.createObject(mainWindow.contentItem)
        verify(manager)
        manager.height = manager.preferredWindowHeight
        compare(manager.visibleRowCount, 10)
        compare(manager.trackRowHeight, 42)
        compare(manager.preferredTrackViewportHeight, 420)
        var trackView = findChild(manager, "libraryManagerTrackList")
        verify(trackView)
        tryCompare(trackView, "height", manager.preferredTrackViewportHeight)
        manager.destroy()
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

    function test_track_context_audio_tool_opens_the_real_tool_window() {
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
        var editorAction = findChild(toolsMenu, "trackMenuLightEditor")
        verify(editorAction && editorAction.enabled)
        // Qt Quick renders nested menus in a separate popup window under the
        // test platform; invoke the visible child after the real row
        // right-click and native submenu-chain checks above.
        editorAction.triggered()
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
        compare(Math.round(album.width), 136)
        compare(Math.round(artist.width), 130)
        verify(album.overflowing && artist.overflowing)
        compare(album.textOffset, 0)
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
        compare(Math.round(findChild(row, "trackAlbumCell").width), 136)
        compare(Math.round(findChild(row, "trackArtistCell").width), 130)
        list.destroy()
    }

    function test_track_context_add_to_playlist_uses_real_submenu_click() {
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
        targetAction.triggered()
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
        var proxy = findChild(firstRow, "trackDragProxy")
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
        mouseMove(firstArea, targetPoint.x, targetPoint.y,
                  60, Qt.LeftButton)
        compare(list.dragPreviewCreationCount, 1)
        tryVerify(function() { return target.containsDrag }, 500)
        mouseRelease(firstArea, targetPoint.x, targetPoint.y,
                     Qt.LeftButton)

        tryVerify(function() {
            return PlaylistModel.containsTrack(playlistId, ids[0])
        }, 500)
        verify(PlaylistModel.containsTrack(playlistId, ids[1]),
               "all Ctrl-selected rows must arrive in the target playlist")
        tryVerify(function() { return !findChild(list, "trackDragPreview") }, 500)
        PlaylistModel.removePlaylist(playlistId)
        list.destroy()
        navigation.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
    }

    function test_playlist_context_actions_use_real_mouse_and_keep_playlist_id() {
        var playlistId = PlaylistModel.createPlaylist(
                    "QML context target " + Date.now())
        verify(playlistId.length > 0)
        var navigation = sideNavigationComponent.createObject(mainWindow.contentItem)
        verify(navigation)
        var category = findChild(navigation, "playlistCategory-" + playlistId)
        verify(category, "custom playlist needs a stable context target")
        mouseClick(category, category.width / 2, category.height / 2,
                   Qt.RightButton)
        var menu = findChild(navigation, "playlistContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        var renameAction = findChild(menu, "playlistMenuRename")
        verify(renameAction && renameAction.enabled)
        mouseClick(renameAction, renameAction.width / 2,
                   renameAction.height / 2)
        tryCompare(navigation, "renameRequestCount", 1)
        compare(navigation.lastRequestedPlaylistId, playlistId)

        mouseClick(category, category.width / 2, category.height / 2,
                   Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        var exportAction = findChild(menu, "playlistMenuExport")
        verify(exportAction && exportAction.enabled)
        mouseClick(exportAction, exportAction.width / 2,
                   exportAction.height / 2)
        tryCompare(navigation, "exportRequestCount", 1)
        compare(navigation.lastRequestedPlaylistId, playlistId)

        mouseClick(category, category.width / 2, category.height / 2,
                   Qt.RightButton)
        tryVerify(function() { return menu.visible }, 500)
        var deleteAction = findChild(menu, "playlistMenuDelete")
        verify(deleteAction && deleteAction.enabled)
        mouseClick(deleteAction, deleteAction.width / 2,
                   deleteAction.height / 2)
        tryCompare(navigation, "deleteRequestCount", 1)
        compare(navigation.lastRequestedPlaylistId, playlistId)

        PlaylistModel.removePlaylist(playlistId)
        navigation.destroy()
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
        var seekSurface = findChild(mainWindow, "waveformHoverSurface")
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

        var expected = Math.round(PlaybackController.durationMs * 0.75)
        PlaybackController.pause()
        mouseClick(seekSurface, seekSurface.width * 0.75,
                   seekSurface.height / 2)
        tryVerify(function() {
            return Math.abs(PlaybackController.positionMs - expected) < 150
        }, 1000, "seek mismatch: actual=" + PlaybackController.positionMs
                 + ", expected=" + expected
                 + ", width=" + seekSurface.width)
        var guide = findChild(mainWindow, "waveformPlaybackGuide")
        verify(guide)
        var expectedWidth = waveform.width * waveform.position
                            / waveform.duration
        verify(Math.abs(waveform.position - expected) < 150,
               "rendered waveform and playback controller must share one position")
        verify(Math.abs(guide.x - Math.round(expectedWidth - guide.width / 2)) < 2)
        var originalWidth = mainWindow.width
        mainWindow.width = Math.max(mainWindow.minimumWidth, originalWidth - 160)
        wait(30)
        expectedWidth = waveform.width * waveform.position
                        / waveform.duration
        verify(Math.abs(waveform.position - PlaybackController.positionMs) < 2,
               "waveform position must remain authoritative after resizing")
        verify(Math.abs(guide.x - Math.round(expectedWidth - guide.width / 2)) < 2,
               "playback guide must stay aligned after resizing")
        mainWindow.width = originalWidth
    }

    function test_waveform_time_axis_prefers_exact_decoded_waveform_duration() {
        var pane = findChild(mainWindow, "playerPane")
        verify(pane)
        verify(PlaybackController.durationMs > 0)
        // Decoder/playback duration is authoritative. Analysis duration can
        // differ for VBR padding and must never create a seekable visual tail.
        pane.waveformDurationMs = PlaybackController.durationMs * 1.25
        compare(pane.effectiveDurationMs, PlaybackController.durationMs)
        var guide = findChild(mainWindow, "waveformPlaybackGuide")
        verify(guide)
        compare(guide.width, 1)
        compare(guide.color, "#002fa7")
    }

    function test_waveform_mode_button_cycles_the_live_setting() {
        var button = findChild(mainWindow, "waveformModeButton")
        verify(button)
        verify(button.icon.source.toString().endsWith("/waveform-switch.svg"),
               "waveform switch must use the supplied waveform icon")
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
        verify(findChild(mainWindow, "waveformProgressFeather"))
        var playbackGuide = findChild(mainWindow, "waveformPlaybackGuide")
        verify(playbackGuide)
        compare(playbackGuide.width, 1)
        compare(playbackGuide.color.toString(), "#002fa7")
        compare(previous.icon.width, 24)
        compare(next.icon.width, 24)
        compare(mode.icon.width, 24)
        compare(play.width, 52)
        compare(play.height, 52)
        compare(play.icon.width, 24)
        compare(hoverGuide.color.toString(), "#54ff84")
        var hoverSurface = findChild(mainWindow, "waveformHoverSurface")
        verify(hoverSurface,
               "the full waveform canvas must own hover preview input")
        compare(hoverSurface.cursorShape, Qt.ArrowCursor)
    }

    function test_waveform_hover_surface_covers_played_and_unplayed_regions() {
        var previousPreview = SettingsController.waveformHoverTimePreview
        SettingsController.waveformHoverTimePreview = true
        var surface = findChild(mainWindow, "waveformHoverSurface")
        var guide = findChild(mainWindow, "waveformHoverGuide")
        verify(surface)
        verify(guide)
        verify(surface.enabled)
        verify(surface.hoverEnabled)

        surface.updatePreview(Math.max(2, surface.width * 0.15))
        tryVerify(function() { return guide.visible && guide.x < surface.width * 0.30 })

        surface.updatePreview(Math.max(2, surface.width * 0.85))
        tryVerify(function() { return guide.visible && guide.x > surface.width * 0.70 })
        SettingsController.waveformHoverTimePreview = previousPreview
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

    function test_volume_control_uses_compact_white_handle_and_percentage() {
        var mute = findChild(mainWindow, "muteButton")
        var slider = findChild(mainWindow, "volumeSlider")
        var percent = findChild(mainWindow, "volumePercentLabel")
        verify(mute)
        verify(slider)
        verify(percent)
        compare(mute.icon.color.toString(), Theme.iconPrimary.toString())
        compare(mute.icon.width, 24)
        verify(slider.handle.width <= 10)

        var previousVolume = PlaybackController.volume
        PlaybackController.setVolume(0.42)
        tryCompare(percent, "text", "42%")
        PlaybackController.setVolume(previousVolume)
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
                "歌曲/艺术家/专辑/标签/")
        compare(findChild(filter, "bpmModule").width, 216)
        compare(findChild(filter, "bpmRange").first.handle.width, 12)
        compare(findChild(filter, "bpmRange").second.handle.width, 12)
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
                    === Theme.icon("heart-outline").toString()
        })

        LibraryModel.setFavorite(row, true)
        tryVerify(function() {
            return favoriteButton.icon.source.toString()
                    === Theme.icon("heart-fill").toString()
        })
        LibraryModel.setFavorite(row, false)
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
        compare(currentRow.color.toString(), Theme.activeSelection.toString())
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
        compare(findChild(list, "trackHeaderAlbum").width, 136)
        compare(findChild(list, "trackHeaderArtist").width, 130)
        compare(findChild(list, "trackHeaderRating").width, 110)
        compare(findChild(list, "trackHeaderBpm").width, 64)
        compare(findChild(list, "trackHeaderDuration").width, 72)
        compare(findChild(list, "trackHeaderFavoriteAlbumGap").width, 10)
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
        compare(workspace.leftColumnWidth, 256)
        compare(workspace.rightColumnWidth, 328)
        compare(workspace.dividerWidth, 1)
        verify(workspace.centerWidth >= 680)
        verify(window.minimumWidth >= 1284)
        window.width = window.minimumWidth
        tryVerify(function() { return workspace.centerWidth >= 680 })
        compare(countObjectsNamed(workspace, "sharedTrackList"), 1)

        var navigation = findChild(workspace, "referenceSideNavigation")
        verify(navigation)
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
        compare(tagPanel.gridColumnCount, 3)
        var tagGrid = findChild(tagPanel, "tagGrid")
        verify(tagGrid)
        compare(tagGrid.cellWidth, tagGrid.width / 3)

        window.destroy()
        SettingsController.listWaveformThumbnailEnabled = previousEnabled
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
        tryCompare(TagModel, "selectedKey", "")
        compare(filterModel.tagKey, "")
        compare(filterModel.searchText, "keep-search")
        compare(filterModel.exactRating, 4)
        compare(filterModel.minBpm, 88)
        compare(filterModel.maxBpm, 144)
        task4TemporaryTagKeys = []
        window.destroy()
    }

    function test_z_task5_tag_menu_rename_color_and_cancel_are_real() {
        var filterModel = findChild(mainWindow, "filterModel")
        var window = tagManagementPanelWindowComponent.createObject(
                    null, { "filterModel": filterModel })
        verify(window)
        var panel = findChild(window, "tagManagementPanel")
        verify(panel)
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
        menu.close()
        colorDialog.selectedColor = "#123456"
        colorDialog.reject()
        compare(TagModel.data(TagModel.index(renamedRow, 0),
                              TagModel.ColorRole).toString(), beforeColor)

        colorDialog.selectedColor = "#123456"
        colorDialog.accepted()
        compare(TagModel.data(TagModel.index(renamedRow, 0),
                              TagModel.ColorRole).toString(), "#123456")

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
        var proxy = findChild(firstRow, "trackDragProxy")
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
        list.destroy()
    }

    function test_z_task5_resource_folder_drop_and_remove_preserve_disk_files() {
        var folderUrl = nativeDropHelper.createDropDirectory()
        verify(folderUrl && nativeDropHelper.pathExists(folderUrl))
        var copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio)
        var initialFolderCount = LibraryManagerController.monitoredFolders.length
        var initialLibraryCount = LibraryModel.count
        var navigationWindow = resourceNavigationWindowComponent.createObject(null)
        verify(navigationWindow)
        navigationWindow.requestActivate()
        tryVerify(function() { return navigationWindow.active }, 1000)
        var navigation = findChild(navigationWindow, "resourceTestNavigation")
        verify(navigation)
        var navigationList = findChild(navigation, "libraryNavigationList")
        verify(navigationList)
        navigationList.positionViewAtEnd()
        wait(0)
        var dropTarget = findChild(navigation, "resourceFolderDropTarget")
        verify(dropTarget)
        nativeDropHelper.sendUrls(dropTarget, [folderUrl, copiedAudio])
        tryVerify(function() {
            return LibraryManagerController.monitoredFolders.length
                    === initialFolderCount + 1
        }, 1000)
        tryVerify(function() { return !ImportController.busy }, 5000)
        verify(ImportController.errors.length === 0)
        verify(LibraryModel.count > initialLibraryCount,
               "mixed drop must keep audio-file import routing")
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
        mouseClick(rootNode, rootNode.width / 2, rootNode.height / 2,
                   Qt.RightButton)
        var menu = findChild(navigation, "resourceFolderContextMenu")
        tryVerify(function() { return menu && menu.visible }, 500)
        wait(500)
        tryVerify(function() { return !LibraryManagerController.scanning }, 3000)
        var scanFinished = signalSpyComponent.createObject(testCase, {
            "target": LibraryManagerController,
            "signalName": "scanFinished"
        })
        verify(scanFinished)
        scanFinished.clear()
        var rescanAction = findChild(menu, "resourceFolderMenuRescan")
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
        var confirm = findChild(navigation, "removeResourceFolderDialog")
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
        navigationWindow.destroy()
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
        compare(mainWindow.width, 1104)
        compare(mainWindow.height, 342)

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
        compare(settingsWindow.width, 1228)
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
        verify(headerDragArea.width > settingsWindow.width * 0.60)
        compare(sidebar.width, 208)
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
        compare(formatCombo.valueModel.length, 3)
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
        var page = findChild(mainWindow, "settingsPage")
        verify(page)
        page.open()
        page.selectedSection = 6
        wait(250)
        var productLine = findChild(page, "aboutProductLine")
        verify(productLine)
        compare(productLine.text, "AgPlayer v1.0")
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

    function test_theme_mode_updates_surfaces_text_and_icons() {
        var previousMode = SettingsController.themeMode

        SettingsController.themeMode = 0
        tryCompare(Theme, "isLight", false)
        var darkBackground = Theme.background.toString()
        var darkText = Theme.primaryText.toString()
        compare(darkBackground, "#202020")
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
                Theme.systemIsLight ? "#f3f3f3" : "#202020")
        compare(Theme.cyan.toString(), Theme.accent.toString())
        compare(Theme.waveformCyan.toString(), "#00d4ff")

        SettingsController.themeMode = previousMode
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
