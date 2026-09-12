import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "IntegratedThemeAdjustments"
    when: windowShown
    visible: true
    width: 900
    height: 240

    property var mainWindow: null
    property int savedShellMode: 0
    property int savedWaveformMode: 0

    QtObject {
        id: fakePlayback
        property int positionMs: 0
        property int durationMs: 100000
        property int selectionStartMs: 0
        property int selectionEndMs: 0
        property bool selectionLoopEnabled: false
        property string currentTrackId: "test-track"
        property string lyrics: ""
        property var spectrum: []
        property int seekCount: 0
        property int playCount: 0
        property int disableCount: 0
        property int clearCount: 0

        function reset() {
            positionMs = 0
            durationMs = 100000
            selectionStartMs = 0
            selectionEndMs = 0
            selectionLoopEnabled = false
            seekCount = 0
            playCount = 0
            disableCount = 0
            clearCount = 0
        }
        function seek(value) { positionMs = value; ++seekCount }
        function play() { ++playCount }
        function disableSelectionLoopAndSeek(value) {
            selectionLoopEnabled = false
            positionMs = value
            ++disableCount
        }
        function commitSelection(startMs, endMs) {
            selectionStartMs = startMs
            selectionEndMs = endMs
            selectionLoopEnabled = true
        }
        function adjustSelection(startMs, endMs) {
            selectionStartMs = startMs
            selectionEndMs = endMs
        }
        function clearSelection() {
            selectionStartMs = 0
            selectionEndMs = 0
            selectionLoopEnabled = false
            ++clearCount
        }
    }

    QtObject {
        id: fakeWaveformProvider
        property real analysisProgress: 0.625
    }

    QtObject {
        id: fakeLyricsService
        property bool enabled: true
        property int status: LyricsService.Ready
        property string previousLine: "上一句共享歌词"
        property string currentLine: "当前共享歌词"
        property string nextLine: "下一句共享歌词"
        property int offsetMs: 0
        property int pauseCalls: 0
        function retry() {}
        function pauseFollow(milliseconds) { ++pauseCalls }
        function importLrc(url) { return true }
    }

    WaveSelectionOverlay {
        id: standaloneSelection
        objectName: "standaloneSelection"
        width: 800
        height: 100
        y: 120
        durationMs: 100000
        visibleStartMs: 0
        visibleEndMs: 100000
    }

    Component {
        id: classicTrackListComponent
        TrackList {
            width: 720
            height: 180
            layoutProfile: "classic"
        }
    }

    Component {
        id: classicSearchFilterComponent
        SearchFilter {
            width: 720
            height: 54
            integratedStyle: false
        }
    }

    Component {
        id: classicTagPanelComponent
        TagManagementPanel {
            width: 300
            height: 220
            compact: false
        }
    }

    function integratedShell() {
        return findChild(mainWindow, "integratedPlayerShell")
    }

    function enterIntegratedShell() {
        SettingsController.playerShellMode = 1
        tryVerify(function() { return integratedShell() !== null }, 1500)
        return integratedShell()
    }

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined")
        mainWindow = testMainWindow
        verify(mainWindow)
        savedShellMode = SettingsController.playerShellMode
        savedWaveformMode = SettingsController.waveformMode
    }

    function init() {
        fakePlayback.reset()
        standaloneSelection.clearSelection()
        if (mainWindow["integratedSidePanelPage"] !== undefined)
            mainWindow.integratedSidePanelPage = 0
        if (mainWindow["integratedSidePanelExpanded"] !== undefined)
            mainWindow.integratedSidePanelExpanded = true
        SettingsController.waveformMode = 0
    }

    function cleanup() {
        SettingsController.playerShellMode = 0
        wait(0)
    }

    function cleanupTestCase() {
        SettingsController.waveformMode = savedWaveformMode
        SettingsController.playerShellMode = savedShellMode
    }

    function test_selection_clicks_play_and_right_click_clears() {
        var shell = enterIntegratedShell()
        shell.playbackController = fakePlayback
        var overlay = findChild(shell, "integratedWaveSelectionOverlay")
        verify(overlay)
        overlay.setSelection(20000, 40000)
        fakePlayback.selectionStartMs = 20000
        fakePlayback.selectionEndMs = 40000
        fakePlayback.selectionLoopEnabled = true

        mouseClick(overlay, overlay.timeToX(30000), overlay.height / 2,
                   Qt.LeftButton)
        compare(fakePlayback.seekCount, 1)
        compare(fakePlayback.playCount, 1)
        compare(fakePlayback.disableCount, 0)

        mouseClick(overlay, overlay.timeToX(70000), overlay.height / 2,
                   Qt.LeftButton)
        compare(fakePlayback.disableCount, 1)
        compare(fakePlayback.playCount, 2)
        verify(overlay.selectionActive)

        mouseClick(overlay, overlay.timeToX(30000), overlay.height / 2,
                   Qt.RightButton)
        compare(fakePlayback.clearCount, 1)
        verify(!overlay.selectionActive)
    }

    function test_selection_hover_handles_and_opaque_badges_match_contract() {
        standaloneSelection.setSelection(20000, 60000)
        mouseMove(standaloneSelection, 400, 50)
        compare(standaloneSelection.hoverPositionMs, 50000)

        var leftVisual = findChild(standaloneSelection,
                                   "waveSelectionLeftHandleVisual")
        var durationBadge = findChild(standaloneSelection,
                                      "waveSelectionDuration")
        var durationLabel = findChild(standaloneSelection,
                                      "waveSelectionDurationLabel")
        var dragBadge = findChild(standaloneSelection,
                                  "waveSelectionDragClipButton")
        var dragLabel = findChild(standaloneSelection,
                                  "waveSelectionDragClipLabel")
        verify(leftVisual && durationBadge && durationLabel
               && dragBadge && dragLabel)
        compare(leftVisual.width, 2)
        compare(durationBadge.color.toString(), Theme.accent.toString())
        compare(dragBadge.color.toString(), Theme.accentHover.toString())
        compare(durationLabel.color.toString(),
                Theme.onBrandGradientText.toString())
        compare(dragLabel.color.toString(),
                Theme.onBrandGradientText.toString())

        mouseMove(testCase, 850, 20)
        tryCompare(standaloneSelection, "hoverPositionMs", -1)
    }

    function test_waveform_returns_after_spectrum_mode() {
        var shell = enterIntegratedShell()
        shell.playbackController = fakePlayback
        shell.waveformDurationMs = 100000
        shell.waveformLayers = {
            "mix": [0.2, 0.5, 0.8, 0.4],
            "_sampleRate": 48000,
            "_totalSamples": 4800000,
            "_peakCount": 4
        }
        SettingsController.waveformMode = 0
        wait(0)
        var waveform = findChild(shell, "integratedWaveform")
        var played = findChild(shell, "integratedPlayedWaveform")
        verify(waveform && played)
        verify(waveform.layers.mix && waveform.layers.mix.length === 4,
               "source=" + JSON.stringify(shell.waveformLayers)
               + " displayed=" + JSON.stringify(shell.displayedWaveformLayers)
               + " rendered=" + JSON.stringify(waveform.layers))
        verify(played.layers.mix && played.layers.mix.length === 4,
               "played=" + JSON.stringify(played.layers))

        fakePlayback.spectrum = [0.04, 0.16, 0.36, 0.64]
        SettingsController.waveformMode = 2
        tryVerify(function() {
            return waveform.peaks.length > 0 && played.peaks.length > 0
        }, 1000)
        compare(played.peaks.length, waveform.peaks.length)
        compare(played.peaks[0], waveform.peaks[0])

        SettingsController.waveformMode = 1
        wait(0)
        verify(waveform.layers.mix && waveform.layers.mix.length === 4,
               "switching away from spectrum must restore the analysed waveform")
        verify(played.layers.mix && played.layers.mix.length === 4,
               "played waveform must restore analysed layers after spectrum mode")
    }

    function test_track_change_restores_full_waveform_viewport() {
        var shell = enterIntegratedShell()
        shell.playbackController = fakePlayback
        fakePlayback.currentTrackId = "viewport-track-a"
        shell.waveformDurationMs = 100000
        var waveform = findChild(shell, "integratedWaveform")
        verify(waveform)
        waveform.setVisibleRange(25000, 75000)
        compare(waveform.visibleStartMs, 25000)
        compare(waveform.visibleEndMs, 75000)

        fakePlayback.currentTrackId = "viewport-track-b"
        shell.waveformDurationMs = 120000

        tryCompare(waveform, "visibleStartMs", 0)
        tryCompare(waveform, "visibleEndMs", 120000)
    }

    function test_right_panel_tabs_collapse_and_persist() {
        var shell = enterIntegratedShell()
        shell.lyricsService = fakeLyricsService
        var tagTab = findChild(shell, "integratedTagTabButton")
        var lyricsTab = findChild(shell, "integratedLyricsTabButton")
        var tagContent = findChild(shell, "integratedTagContent")
        var lyricsContent = findChild(shell, "integratedLyricsContent")
        var lyricsPanel = findChild(shell, "integratedLyricsPanel")
        var toggle = findChild(shell, "integratedSidePanelToggleButton")
        var column = findChild(shell, "integratedTagColumn")
        verify(tagTab && lyricsTab && tagContent && lyricsContent
               && lyricsPanel && toggle && column)
        verify(tagContent.visible)
        verify(!lyricsContent.visible)

        mouseClick(lyricsTab)
        verify(!tagContent.visible)
        verify(lyricsContent.visible)
        compare(findChild(lyricsPanel, "previousLyricLine").text,
                "上一句共享歌词")
        compare(findChild(lyricsPanel, "currentLyricLine").text,
                "当前共享歌词")
        compare(findChild(lyricsPanel, "nextLyricLine").text,
                "下一句共享歌词")
        mouseClick(toggle)
        tryCompare(column, "width", 42)

        SettingsController.playerShellMode = 0
        wait(0)
        shell = enterIntegratedShell()
        column = findChild(shell, "integratedTagColumn")
        lyricsContent = findChild(shell, "integratedLyricsContent")
        compare(shell.sidePanelPage, 1)
        tryCompare(column, "width", 42)
        var toggleAfterReload = findChild(shell,
                                          "integratedSidePanelToggleButton")
        mouseClick(toggleAfterReload)
        verify(lyricsContent.visible)
    }

    function test_lyrics_service_follows_panel_and_global_requests() {
        const savedLyricsVisible = PlayerExperienceController.lyricsVisible

        try {
            PlayerExperienceController.lyricsVisible = false
            SettingsController.playerShellMode = 0
            mainWindow.integratedSidePanelPage = 0
            mainWindow.integratedSidePanelExpanded = true
            tryCompare(LyricsService, "enabled", false)

            const shell = enterIntegratedShell()
            const tagTab = findChild(shell, "integratedTagTabButton")
            const lyricsTab = findChild(shell, "integratedLyricsTabButton")
            const toggle = findChild(shell,
                                     "integratedSidePanelToggleButton")
            verify(tagTab && lyricsTab && toggle)
            tryCompare(LyricsService, "enabled", false)

            mouseClick(lyricsTab)
            tryCompare(LyricsService, "enabled", true)
            mouseClick(toggle)
            tryCompare(LyricsService, "enabled", false)
            mouseClick(toggle)
            tryCompare(LyricsService, "enabled", true)
            mouseClick(tagTab)
            tryCompare(LyricsService, "enabled", false)

            PlayerExperienceController.lyricsVisible = true
            tryCompare(LyricsService, "enabled", true)
            SettingsController.playerShellMode = 0
            tryCompare(LyricsService, "enabled", true)
            PlayerExperienceController.lyricsVisible = false
            tryCompare(LyricsService, "enabled", false)
        } finally {
            PlayerExperienceController.lyricsVisible = savedLyricsVisible
            wait(0)
        }
    }

    function test_right_panel_header_and_content_fill_from_the_top() {
        var shell = enterIntegratedShell()
        var column = findChild(shell, "integratedTagColumn")
        var header = findChild(shell, "integratedSidePanelHeader")
        var content = findChild(shell, "integratedSidePanelContent")
        verify(column && header && content)
        tryVerify(function() {
            return header.mapToItem(column, 0, 0).y <= 12
                    && content.height > column.height * 0.75
        }, 1000)
    }

    function test_bottom_actions_use_uploaded_immersive_icon_without_duplicate_lyrics() {
        var shell = enterIntegratedShell()
        var controls = findChild(shell, "integratedPlayerControls")
        var audioTools = findChild(shell, "audioToolsButton")
        var immersive = findChild(shell, "immersiveActionButton")
        var rightActions = findChild(shell, "integratedRightActions")
        verify(controls && audioTools && immersive && rightActions)
        compare(findChild(shell, "lyricsActionButton"), null)
        compare(findChild(shell, "playerShellModeButton"), null)
        compare(findChild(shell, "themeActionButton"), null)
        verify(findChild(shell, "miniPlayerButton"))
        compare(audioTools.parent,
                findChild(shell, "integratedCenterControls"))
        var immersiveIcon = findChild(immersive, "animatedImmersiveIcon")
        verify(immersiveIcon)
        compare(immersiveIcon.width, 20)
        compare(immersiveIcon.height, 20)
        var theme = findChild(shell, "themeModeButton")
        verify(theme)
        verify(theme.icon.source.toString().endsWith("/theme-skin.svg"))
    }

    function test_right_panel_uses_outlined_native_controls() {
        var shell = enterIntegratedShell()
        var tagOutline = findChild(shell, "integratedTagTabOutline")
        var lyricsOutline = findChild(shell, "integratedLyricsTabOutline")
        var searchGlass = findChild(shell, "tagSearchGlassBackground")
        var addGlass = findChild(shell, "tagAddGlassBackground")
        var toggleIcon = findChild(shell, "integratedSidePanelToggleIcon")
        var tagTab = findChild(shell, "integratedTagTabButton")
        verify(tagOutline && lyricsOutline && searchGlass
               && addGlass && toggleIcon && tagTab)
        mouseClick(tagTab)
        compare(tagOutline.border.width, 1)
        compare(lyricsOutline.border.width, 1)
        compare(tagOutline.color.toString(), Theme.selectedSurface.toString())
        compare(lyricsOutline.color.toString(), Theme.surfaceElevated.toString())
        compare(searchGlass.color.toString(), Theme.surfaceElevated.toString())
        compare(addGlass.color.toString(), Theme.surfaceElevated.toString())
        compare(toggleIcon.width, 22)
        compare(toggleIcon.height, 22)
    }

    function test_right_panel_uses_compact_tag_controls() {
        var shell = enterIntegratedShell()
        compare(findChild(shell, "tagSearchField").height, 34)
        compare(findChild(shell, "addTagButton").height, 34)
    }

    function test_collapsed_right_panel_centers_toggle() {
        var shell = enterIntegratedShell()
        var column = findChild(shell, "integratedTagColumn")
        var toggle = findChild(shell, "integratedSidePanelToggleButton")
        mouseClick(toggle)
        tryCompare(column, "width", 42)
        fuzzyCompare(toggle.mapToItem(column, 0, 0).x + toggle.width / 2,
                     column.width / 2, 1.0)
    }

    function test_wave_navigator_uses_flat_native_material() {
        var shell = enterIntegratedShell()
        var track = findChild(shell, "integratedWaveformNavigatorTrack")
        var thumb = findChild(shell, "integratedWaveformNavigatorThumb")
        var highlight = findChild(shell,
                                  "integratedWaveformNavigatorHighlight")
        verify(track && thumb && highlight)
        compare(track.color.toString(), Theme.opaqueDivider.toString())
        compare(thumb.color.toString(), Theme.accentSoft.toString())
        compare(thumb.border.color.toString(), Theme.opaqueBorder.toString())
        compare(highlight.color.toString(), Theme.surfaceHover.toString())
    }

    function test_integrated_track_header_is_compact_and_bold() {
        var shell = enterIntegratedShell()
        var list = findChild(shell, "integratedTrackList")
        var title = findChild(shell, "trackHeaderTitle")
        verify(list && list.headerItem && title)
        compare(list.layoutProfile, "integrated")
        compare(list.headerItem.height, Theme.tableHeaderHeight)
        compare(title.font.weight, Font.DemiBold)
    }

    function test_integrated_default_window_is_1386_wide_and_shows_ten_rows() {
        var shell = enterIntegratedShell()
        mainWindow.width = shell.defaultWindowWidth
        mainWindow.height = shell.defaultWindowHeight
        wait(0)

        var list = findChild(shell, "integratedTrackList")
        verify(list && list.headerItem)
        compare(mainWindow.width, 1386)
        compare(list.mapToItem(shell, 0, 0).x, 233)
        compare(list.width, 856)
        compare(list.height, shell.defaultTrackListHeight)
        compare((list.height - list.headerItem.height) / list.rowHeight, 10)
    }

    function test_integrated_host_keeps_trailing_cells_inside_content_width() {
        var shell = enterIntegratedShell()
        mainWindow.width = 1180
        wait(0)
        var list = findChild(shell, "integratedTrackList")
        verify(list && list.width > 0)
        var names = ["trackHeaderDuration", "trackHeaderRating",
                     "trackHeaderFavorite"]
        for (var index = 0; index < names.length; ++index) {
            var cell = findChild(list, names[index])
            verify(cell && cell.visible)
            verify(cell.mapToItem(list, cell.width, 0).x <= list.width + 1)
        }
    }

    function test_integrated_list_ignores_classic_thumbnail_switch() {
        var previousEnabled = SettingsController.listWaveformThumbnailEnabled
        try {
            SettingsController.listWaveformThumbnailEnabled = false
            var shell = enterIntegratedShell()
            var list = findChild(shell, "integratedTrackList")
            verify(list)
            compare(list.waveformThumbnailsVisible, true)
            compare(list.rowHeight, Theme.mediaListRowHeight)
        } finally {
            SettingsController.listWaveformThumbnailEnabled = previousEnabled
        }
    }

    function test_integrated_filter_is_soft_and_uses_apple_handle() {
        var shell = enterIntegratedShell()
        var filter = findChild(shell, "integratedSearchFilter")
        var keyword = findChild(filter, "keywordModule")
        var bpm = findChild(filter, "bpmModule")
        var range = findChild(filter, "bpmRange")
        var firstHandle = findChild(range, "rangeSliderFirstHandle")
        var sliderTrack = findChild(range, "rangeSliderTrack")
        var clear = findChild(filter, "clearFiltersButton")
        verify(filter && keyword && bpm && range && firstHandle
               && sliderTrack && clear)
        compare(keyword.border.color.toString(), Theme.opaqueBorder.toString())
        compare(bpm.border.color.toString(), Theme.opaqueBorder.toString())
        compare(sliderTrack.height, Theme.sliderTrackHeight)
        compare(firstHandle.width, Theme.sliderHandleExtent)
        compare(firstHandle.height, Theme.sliderHandleExtent)
        verify(firstHandle.color.a > 0.70)

    }

    function test_integrated_filter_keeps_clear_near_bpm() {
        var shell = enterIntegratedShell()
        var filter = findChild(shell, "integratedSearchFilter")
        var bpm = findChild(filter, "bpmModule")
        var clear = findChild(filter, "clearFiltersButton")
        verify(filter && bpm && clear)
        tryVerify(function() {
            var bpmRight = bpm.mapToItem(filter, bpm.width, 0).x
            var clearLeft = clear.mapToItem(filter, 0, 0).x
            return clearLeft >= bpmRight && clearLeft - bpmRight <= 18
        }, 1000)
    }

    function test_search_input_debounces_and_cancels_stale_queries() {
        var component = Qt.createComponent(Qt.resolvedUrl(
                    "../../app/qml/AgPlayer/components/SearchFilter.qml"))
        compare(component.status, Component.Ready, component.errorString())
        var filter = component.createObject(testCase, { width: 640, height: 32 })
        verify(filter)
        try {
            var field = findChild(filter, "librarySearchField")
            verify(field)
            field.text = "a"
            field.text = "artist"
            compare(filter.searchText, "")
            compare(field.text, "artist")
            tryCompare(filter, "searchText", "artist", 1000)
            field.text = "pending"
            filter.clearFilters()
            wait(220)
            compare(filter.searchText, "")
            compare(field.text, "")
            field.text = "stale"
            filter.searchText = "external"
            wait(220)
            compare(filter.searchText, "external")
            compare(field.text, "external")
            field.text = "enter"
            field.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(filter.searchText, "enter")
        } finally {
            filter.destroy()
        }
    }

    function test_search_filter_compacts_without_hiding_controls() {
        var previousAutoRating = SettingsController.autoReadRating
        SettingsController.autoReadRating = true
        var component = Qt.createComponent(Qt.resolvedUrl(
                    "../../app/qml/AgPlayer/components/SearchFilter.qml"))
        if (component.status === Component.Loading)
            tryCompare(component, "status", Component.Ready, 3000)
        compare(component.status, Component.Ready, component.errorString())
        var filter = component.createObject(testCase, {
            "width": 543,
            "height": Theme.controlHeight,
            "integratedStyle": true
        })
        verify(filter)
        try {
            wait(0)
            var keyword = findChild(filter, "keywordModule")
            var rating = findChild(filter, "ratingModule")
            var bpm = findChild(filter, "bpmModule")
            var range = findChild(filter, "bpmRange")
            var clear = findChild(filter, "clearFiltersButton")
            verify(keyword && rating && bpm && range && clear)
            compare(filter.compactLayout, true)
            verify(keyword.width >= 112)
            verify(rating.visible && rating.width >= 88)
            verify(bpm.width >= 176)
            compare(range.width, 72)
            verify(clear.mapToItem(filter, clear.width, 0).x
                   <= filter.width + 0.5,
                   "all filter controls must fit the narrow center column")
        } finally {
            filter.destroy()
            SettingsController.autoReadRating = previousAutoRating
        }
    }

    function test_lyrics_diagnostics_and_actions_fit_narrow_panel() {
        var service = Qt.createQmlObject(
                    'import QtQuick; import AgPlayer; QtObject {'
                    + ' property bool enabled: true;'
                    + ' property int status: LyricsService.Error;'
                    + ' property string previousLine: "";'
                    + ' property string currentLine: "";'
                    + ' property string nextLine: "";'
                    + ' property var lines: []; property int currentLineIndex: -1;'
                    + ' property bool synchronizedLyrics: false;'
                    + ' property bool instrumental: false;'
                    + ' property string untimedLyrics: "";'
                    + ' property string sourceProvider: "";'
                    + ' property string sourceAttribution: "";'
                    + ' property var routeNotice: ({ providerName: "A very long lyrics provider route name", diagnostic: "network-error" });'
                    + ' property var routeAttempts: [{ providerName: "Another very long provider route name", diagnostic: "timeout" }];'
                    + ' property int offsetMs: 0;'
                    + ' function retry() {} function pauseFollow(value) {}'
                    + ' function importLrc(url) { return true }'
                    + '}', testCase)
        var component = Qt.createComponent(Qt.resolvedUrl(
                    "../../app/qml/AgPlayer/components/LyricsPanel.qml"))
        compare(component.status, Component.Ready, component.errorString())
        var panel = component.createObject(testCase, {
            "width": 280,
            "height": 360,
            "service": service
        })
        verify(panel)
        try {
            wait(0)
            var notice = findChild(panel, "lyricsRouteNotice")
            var noticeText = findChild(panel, "lyricsRouteNoticeText")
            var attempt = findChild(panel, "lyricsRouteAttemptText")
            var close = findChild(panel, "lyricsCloseButton")
            verify(notice && noticeText && attempt && close)
            verify(notice.width <= panel.width - 24 + 0.5)
            verify(notice.mapToItem(panel, 0, 0).y
                   >= close.mapToItem(panel, 0, close.height).y,
                   "route diagnostics must start below the close action")
            verify(noticeText.width <= notice.width)
            compare(noticeText.maximumLineCount, 2)
            compare(noticeText.font.family, Theme.fontPrimary)
            compare(attempt.font.family, Theme.fontPrimary)
            verify(attempt.width <= panel.width - 24 + 0.5)
            compare(close.focusPolicy, Qt.StrongFocus)
            verify(close.background)

            var actions = ["lyricsOffsetEarlierButton",
                           "lyricsOffsetLaterButton",
                           "lyricsRetryButton", "lyricsImportButton"]
            for (var index = 0; index < actions.length; ++index) {
                var action = findChild(panel, actions[index])
                verify(action && action.background)
                compare(action.focusPolicy, Qt.StrongFocus)
                verify(action.hoverEnabled)
                verify(action.icon.source.toString().length > 0)
            }
        } finally {
            panel.destroy()
            service.destroy()
        }
    }

    function createFallbackCover(properties) {
        var component = Qt.createComponent(Qt.resolvedUrl(
                    "../../app/qml/AgPlayer/components/FallbackCoverImage.qml"))
        if (component.status === Component.Loading)
            tryCompare(component, "status", Component.Ready, 3000)
        compare(component.status, Component.Ready, component.errorString())
        var cover = component.createObject(testCase, properties || {})
        verify(cover)
        return cover
    }

    function test_cover_falls_back_after_a_real_image_decode_error() {
        var invalidImage = Qt.resolvedUrl("tst_integrated_theme.qml")
        var validImage = Qt.resolvedUrl("../../assets/brand/logo-mark.png")
        var cover = createFallbackCover({
            "width": 96,
            "height": 96,
            "fallbackSource": validImage,
            "requestedSource": invalidImage
        })
        try {
            tryCompare(cover, "usingFallback", true, 3000)
            tryCompare(cover, "status", Image.Ready, 3000)
            compare(cover.source.toString(), validImage.toString())
        } finally {
            cover.destroy()
        }
    }

    function test_stale_cover_error_cannot_replace_a_new_valid_source() {
        var invalidImage = Qt.resolvedUrl("tst_integrated_theme.qml")
        var fallbackImage = Qt.resolvedUrl("../../assets/brand/logo-mark.png")
        var validImage = Qt.resolvedUrl("../../assets/brand/logo-lockup.png")
        var cover = createFallbackCover({
            "width": 96,
            "height": 96,
            "fallbackSource": fallbackImage,
            "requestedSource": invalidImage
        })
        try {
            // Prime the invalid URL so its next decode failure can be delivered
            // immediately while the deferred fallback is still pending.
            tryCompare(cover, "usingFallback", true, 3000)
            tryCompare(cover, "status", Image.Ready, 3000)
            cover.requestedSource = ""
            cover.requestedSource = invalidImage
            cover.requestedSource = validImage
            tryCompare(cover, "requestedSource", validImage, 3000)
            tryCompare(cover, "status", Image.Ready, 3000)
            wait(0)
            compare(cover.usingFallback, false,
                    "the deferred error from the old URL must be ignored")
            compare(cover.source.toString(), validImage.toString())
        } finally {
            cover.destroy()
        }
    }

    function test_integrated_hierarchy_uses_flat_columns_and_framed_media() {
        var shell = enterIntegratedShell()
        var flatNames = ["integratedLibraryColumn", "integratedTrackColumn"]
        for (var flatIndex = 0; flatIndex < flatNames.length; ++flatIndex) {
            var flatSurface = findChild(shell, flatNames[flatIndex])
            verify(flatSurface, flatNames[flatIndex] + " missing")
            compare(flatSurface.border.width, 0, flatNames[flatIndex])
        }
        var framedNames = ["integratedWaveformFrame", "integratedBottomBar"]
        for (var index = 0; index < framedNames.length; ++index) {
            var surface = findChild(shell, framedNames[index])
            verify(surface, framedNames[index] + " missing")
            compare(surface.border.color.toString(),
                    Theme.opaqueBorder.toString(), framedNames[index])
            compare(surface.border.width, 1, framedNames[index])
        }
    }

    function test_classic_shared_components_keep_default_visual_contract() {
        var list = classicTrackListComponent.createObject(testCase)
        var filter = classicSearchFilterComponent.createObject(testCase)
        var tags = classicTagPanelComponent.createObject(testCase)
        verify(list && filter && tags)

        compare(list.headerHeight, Theme.tableHeaderHeight)
        compare(list.headerFontWeight, Font.DemiBold)
        compare(filter.moduleBorder.toString(), Theme.controlSubtleBorder.toString())
        var range = findChild(filter, "bpmRange")
        var sliderTrack = findChild(range, "rangeSliderTrack")
        verify(range && sliderTrack)
        verify(!range.glassStyle)
        compare(sliderTrack.height, Theme.sliderTrackHeight)
        compare(tags.controlBorder.toString(),
                Theme.controlSubtleBorder.toString())
        tryCompare(findChild(tags, "tagSearchField"), "height", 34)
        tryCompare(findChild(tags, "addTagButton"), "height", 34)

        list.destroy()
        filter.destroy()
        tags.destroy()
    }

    function test_bottom_bar_hosts_the_integrated_control_layout() {
        var shell = enterIntegratedShell()
        var bottom = findChild(shell, "integratedBottomBar")
        var summary = findChild(shell, "integratedTrackSummary")
        var cover = findChild(shell, "integratedTrackCover")
        var metadata = findChild(shell, "integratedTrackMetadata")
        var controls = findChild(shell, "integratedPlayerControls")
        var listWindow = findChild(shell, "listWindowButton")
        var centerGroup = findChild(shell, "integratedCenterControls")
        var transport = findChild(shell, "integratedTransportControls")
        var volume = findChild(shell, "mainVolumeControl")
        var rightActions = findChild(shell, "integratedRightActions")
        var playPause = findChild(shell, "playPauseButton")
        var theme = findChild(shell, "themeModeButton")
        var mini = findChild(shell, "miniPlayerButton")
        verify(bottom && summary && cover && metadata && controls
               && listWindow && centerGroup && transport && volume && rightActions
               && playPause && theme && mini)
        verify(cover.width >= 56 && cover.height >= 56)
        verify(metadata.visible)
        fuzzyCompare(summary.mapToItem(bottom, 0, 0).y
                     + summary.height / 2, bottom.height / 2, 1.0)
        fuzzyCompare(controls.y + controls.height / 2,
                     controls.parent.height / 2, 1.0)
        verify(!listWindow.visible && playPause.visible && theme.visible
               && mini.visible)
        var summaryRight = summary.mapToItem(bottom, summary.width, 0).x
        var transportLeft = transport.mapToItem(bottom, 0, 0).x
        var transportRight = transport.mapToItem(
                    bottom, transport.width, 0).x
        var volumeLeft = volume.mapToItem(bottom, 0, 0).x
        var volumeRight = volume.mapToItem(bottom, volume.width, 0).x
        var rightActionsLeft = rightActions.mapToItem(bottom, 0, 0).x
        verify(summaryRight <= transportLeft,
               "track summary must stay outside the centered transport")
        verify(transportRight <= volumeLeft,
               "volume must follow the center transport")
        verify(volumeRight <= rightActionsLeft,
               "center transport/volume must not overlap right-side tools")
        var centeredLeft = bottom.width / 2 - transport.playButtonCenterX
        var expectedLeft = centeredLeft
        fuzzyCompare(centerGroup.mapToItem(bottom, 0, 0).x,
                     expectedLeft, 2.0)
    }


    function test_bottom_track_title_has_more_room() {
        var shell = enterIntegratedShell()
        var previousWidth = mainWindow.width
        mainWindow.width = 1672
        var summary = findChild(shell, "integratedTrackSummary")
        verify(summary)
        tryVerify(function() { return summary.width >= 400 }, 1000)
        verify(summary.width <= 420,
               "track summary must leave the centered transport unobstructed")
        mainWindow.width = previousWidth
    }

    function test_waveform_and_bottom_bar_use_standard_spacing() {
        var shell = enterIntegratedShell()
        var waveformFrame = findChild(shell, "integratedWaveformFrame")
        var bottomBar = findChild(shell, "integratedBottomBar")
        verify(waveformFrame && bottomBar)
        var waveformBottom = waveformFrame.mapToItem(
                    shell, 0, waveformFrame.height).y
        var bottomTop = bottomBar.mapToItem(shell, 0, 0).y
        verify(bottomTop - waveformBottom <= 5,
               "waveform and transport gap should match the 4px upper gap")
        verify(bottomBar.height >= 80,
               "transport canvas should gain height while closing the gap")
    }

    function test_integrated_waveform_progress_uses_continuous_pixel_clip() {
        var shell = enterIntegratedShell()
        var previousMode = SettingsController.waveformMode
        var previousGuide = SettingsController.waveformPlaybackGuide
        SettingsController.waveformMode = 3
        shell.playbackController = fakePlayback
        shell.waveformDurationMs = 100000
        fakePlayback.durationMs = 100000
        shell.waveformLayers = {
            "mix": [0.2, 0.5, 0.8, 0.4],
            "_peakCount": 4
        }

        var base = findChild(shell, "integratedWaveform")
        var clip = findChild(shell, "integratedWaveformPlayedClip")
        var played = findChild(shell, "integratedPlayedWaveform")
        verify(base && clip && played)
        tryVerify(function() {
            return base.layers.mix && base.layers.mix.length === 4
        }, 1000)
        compare(base.position, 0)
        compare(played.width, base.width)
        compare(played.position, played.duration)

        var fractions = [0.001, 0.0011, 0.1234, 0.5005, 0.999]
        var previousWidth = -1
        for (var index = 0; index < fractions.length; ++index) {
            fakePlayback.positionMs = Math.round(fractions[index] * 100000)
            tryVerify(function() {
                return Math.abs(clip.width - base.waveformCursorX) < 0.001
            }, 1000)
            verify(clip.width > previousWidth,
                   "pixel clip must advance for fractional progress "
                   + fractions[index])
            previousWidth = clip.width
        }

        var pausedCursor = base.cursorPosition
        wait(34)
        compare(base.cursorPosition, pausedCursor,
                "a paused position must keep the waveform cursor stable")
        compare(clip.width, base.waveformCursorX,
                "a paused position must keep the clip bound to the cursor")

        fakePlayback.positionMs = 0
        tryVerify(function() { return clip.width === 0 }, 1000)

        shell.waveformDurationMs = 0
        fakePlayback.durationMs = 0
        tryVerify(function() {
            return base.duration === 0 && clip.width === 0
        }, 1000)
        var playbackGuide = findChild(shell, "integratedWaveformPlaybackGuide")
        verify(playbackGuide)
        SettingsController.waveformPlaybackGuide = true
        tryCompare(playbackGuide, "visible", true)
        verify(Math.abs(playbackGuide.x
                        - (base.x + base.waveformCursorX)) <= 0.5)
        SettingsController.waveformPlaybackGuide = false
        tryCompare(playbackGuide, "visible", false)
        compare(findChild(shell, "integratedWaveformPlaybackFocusDot"), null)
        SettingsController.waveformMode = previousMode
        SettingsController.waveformPlaybackGuide = previousGuide
    }

    function test_integrated_waveform_syncs_visible_range_and_frequency_mix() {
        var shell = enterIntegratedShell()
        shell.playbackController = fakePlayback
        shell.waveformProvider = fakeWaveformProvider
        shell.waveformDurationMs = 100000
        fakePlayback.durationMs = 100000
        shell.waveformLayers = {
            "mix": [0.2, 0.5, 0.8, 0.4],
            "bass": [0.8, 0.3, 0.6, 0.2],
            "mid": [0.4, 0.7, 0.2, 0.9],
            "high": [0.6, 0.1, 0.9, 0.5],
            "_sampleRate": 48000,
            "_totalSamples": 4800000,
            "_peakCount": 4
        }
        SettingsController.waveformMode = 3
        wait(0)

        var base = findChild(shell, "integratedWaveform")
        var played = findChild(shell, "integratedPlayedWaveform")
        var frequencySettings = SettingsController.frequencyColorWaveform
        verify(base && played)
        tryVerify(function() {
            return played.layers.mix && played.layers.mix.length === 4
        }, 1000)
        compare(played.layers.mix[2], 0.8)
        compare(played.layers.bass[0], 0.8)
        compare(played.layers.mid[1], 0.7)
        compare(played.layers.high[2], 0.9)
        compare(played.visualMode, base.visualMode)
        compare(played.analysisProgress, base.analysisProgress)
        compare(played.baseColor.toString(), base.baseColor.toString())
        compare(played.progressColor.toString(), base.progressColor.toString())
        compare(played.gradientStartColor.toString(), base.gradientStartColor.toString())
        compare(played.gradientMiddleColor.toString(), base.gradientMiddleColor.toString())
        compare(played.gradientEndColor.toString(), base.gradientEndColor.toString())
        compare(String(base.lowColor), String(frequencySettings.lowColor))
        compare(String(base.midColor), String(frequencySettings.midColor))
        compare(String(played.highColor), String(base.highColor))
        compare(played.frequencyUnplayedOpacity,
                base.frequencyUnplayedOpacity)
        compare(played.rgbProgress, base.rgbProgress)
        compare(played.amplitudeScale, base.amplitudeScale)
        compare(played.density, base.density)
        compare(played.lineWidth, base.lineWidth)

        base.zoomAt(base.width / 2, 2.0)
        tryVerify(function() {
            return played.visibleStartMs === base.visibleStartMs
                    && played.visibleEndMs === base.visibleEndMs
        }, 1000)
    }

    function test_dense_bottom_bar_expands_volume_on_hover_and_allows_dragging() {
        var shell = enterIntegratedShell()
        var previousWidth = mainWindow.width
        mainWindow.width = 1180
        wait(50)

        var controls = findChild(shell, "integratedPlayerControls")
        var listWindow = findChild(shell, "listWindowButton")
        var centerGroup = findChild(shell, "integratedCenterControls")
        var transport = findChild(shell, "integratedTransportControls")
        var volume = findChild(shell, "mainVolumeControl")
        var rightActions = findChild(shell, "integratedRightActions")
        verify(controls && listWindow && centerGroup && transport
               && volume && rightActions)
        compare(controls.denseLayout, true)
        compare(volume.emptyMode, false)
        compare(volume.width, 44)
        verify(!listWindow.visible)
        verify(transport.mapToItem(
                   controls, transport.width, 0).x
               <= volume.mapToItem(controls, 0, 0).x)
        verify(volume.mapToItem(controls, volume.width, 0).x
               <= rightActions.mapToItem(controls, 0, 0).x)

        var mute = findChild(volume, "muteButton")
        var slider = findChild(volume, "volumeSlider")
        var handle = findChild(volume, "volumeSliderHandle")
        verify(mute && slider && handle)
        var originalVolume = PlaybackController.volume
        var originalMuted = PlaybackController.muted
        if (originalMuted)
            PlaybackController.toggleMuted()
        PlaybackController.setVolume(0.2)
        tryVerify(function() { return Math.abs(slider.value - 0.2) < 0.01 }, 500)
        mouseMove(mute, mute.width / 2, mute.height / 2)
        tryVerify(function() { return slider.width >= 64 }, 800,
                  "the dense player must expose a draggable volume slider")
        tryCompare(slider, "width", volume.expandedSliderWidth, 800)
        verify(volume.mapToItem(controls, volume.width, 0).x
               <= rightActions.mapToItem(controls, 0, 0).x)
        mousePress(handle, handle.width / 2, handle.height / 2)
        mouseMove(slider, slider.width - slider.rightPadding - handle.width / 2,
                  slider.height / 2, 30)
        mouseRelease(slider, slider.width - slider.rightPadding - handle.width / 2,
                     slider.height / 2)
        tryVerify(function() { return PlaybackController.volume > 0.9 }, 500)
        mouseMove(controls, 1, 1)
        tryCompare(volume, "expanded", false, 800)
        PlaybackController.setVolume(originalVolume)
        if (originalMuted)
            PlaybackController.toggleMuted()

        mainWindow.width = previousWidth
        wait(20)
    }

    function test_integrated_reference_action_geometry_omits_duplicate_playlist_and_lyrics() {
        var shell = enterIntegratedShell()
        mainWindow.width = 1672
        wait(20)
        var controls = findChild(shell, "integratedPlayerControls")
        verify(controls)
        var names = [
            "audioToolsButton", "equalizerButton",
            "waveformModeButton", "previousButton", "playPauseButton",
            "nextButton", "modeButton",
            "mainVolumeControl", "themeModeButton",
            "immersiveActionButton", "miniPlayerButton"
        ]
        compare(findChild(controls, "lyricsActionButton"), null)
        compare(findChild(controls, "listWindowButton").visible, false)
        var transport = findChild(controls, "integratedTransportControls")
        verify(transport)
        compare(transport.waveformPlacement, "beforePrevious")
        compare(transport.showWaveformMode, true)
        var previousX = -1
        for (var index = 0; index < names.length; ++index) {
            var action = findChild(controls, names[index])
            verify(action && action.visible, "missing " + names[index])
            var actionX = action.mapToItem(controls, 0, 0).x
            verify(actionX > previousX, names[index] + " is out of order")
            previousX = actionX
        }
    }

    function test_integrated_theme_popup_opens_above_icon_at_real_dpr_positions() {
        var shell = enterIntegratedShell()
        mainWindow.width = 1672
        wait(20)
        var controls = findChild(shell, "integratedPlayerControls")
        var button = findChild(controls, "themeModeButton")
        var menu = findChild(controls, "playerShellMenu")
        verify(controls && button && menu)
        var classic = findChild(controls, "classicShellMenuItem")
        compare(classic.labelPixelSize, 12)
        compare(classic.implicitHeight, 28)
        compare(menu.background.color, Theme.surfaceElevated)
        compare(classic.contentItem.color, Theme.primaryText)
        compare(menu.parent, controls.Window.window.contentItem)
        for (var index = 0; index < 2; ++index) {
            var dpr = index === 0 ? 1.0 : 1.5
            controls.popupDevicePixelRatioOverrideForTesting = dpr
            mouseClick(button)
            tryVerify(function() { return menu.visible && menu.height > 0 }, 500)
            var point = controls.themePopupPositionForDpr(dpr)
            verify(isFinite(point.x) && isFinite(point.y))
            verify(Math.abs(point.x * dpr - Math.round(point.x * dpr)) < 0.01)
            verify(Math.abs(point.y * dpr - Math.round(point.y * dpr)) < 0.01)
            var surface = controls.Window.window.contentItem
            var buttonPoint = button.mapToItem(surface, 0, 0)
            var menuPoint = menu.parent.mapToItem(surface, menu.x, menu.y)
            verify(menuPoint.y + menu.height <= buttonPoint.y + 1 / dpr)
            var buttonCenter = buttonPoint.x + button.width / 2
            verify(menuPoint.x <= buttonCenter
                   && buttonCenter <= menuPoint.x + menu.width)
            verify(menuPoint.x >= 0)
            verify(menuPoint.x + menu.width <= surface.width + 1 / dpr)
            menu.close()
            wait(0)
        }
        controls.popupDevicePixelRatioOverrideForTesting = 0
    }

    function test_shell_switch_is_removed_from_transport() {
        var shell = enterIntegratedShell()
        compare(findChild(shell, "playerShellModeButton"), null)
        compare(findChild(shell, "playerExperienceModeMenu"), null)
        compare(findChild(shell, "themeActionButton"), null)
    }

    function test_zoom_navigator_tracks_and_moves_visible_window() {
        var shell = enterIntegratedShell()
        shell.waveformDurationMs = 100000
        var waveform = findChild(shell, "integratedWaveform")
        var navigator = findChild(shell, "integratedWaveformNavigator")
        var thumb = findChild(shell, "integratedWaveformNavigatorThumb")
        verify(waveform && navigator && thumb)
        waveform.zoomAt(waveform.width / 2, 2.0)
        compare(waveform.visibleEndMs - waveform.visibleStartMs, 50000)
        tryVerify(function() { return thumb.width < navigator.width })

        mouseDrag(thumb, thumb.width / 2, thumb.height / 2,
                  navigator.width * 0.25, 0, Qt.LeftButton)
        verify(waveform.visibleStartMs > 25000)
        compare(waveform.visibleEndMs - waveform.visibleStartMs, 50000)
    }
}
