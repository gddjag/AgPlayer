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
            integratedCompact: false
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

    function test_selection_hover_handles_and_glass_badges_match_contract() {
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
        verify(durationBadge.color.a < 0.85)
        verify(dragBadge.color.a < 0.85)
        compare(durationLabel.color.toString(),
                Theme.onBrandGradientText.toString())
        compare(dragLabel.color.toString(),
                Theme.onBrandGradientText.toString())

        mouseMove(testCase, 850, 20)
        tryCompare(standaloneSelection, "hoverPositionMs", -1)
    }

    function test_waveform_returns_after_spectrum_mode() {
        var shell = enterIntegratedShell()
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
        verify(waveform)
        verify(waveform.layers.mix && waveform.layers.mix.length === 4,
               "source=" + JSON.stringify(shell.waveformLayers)
               + " displayed=" + JSON.stringify(shell.displayedWaveformLayers)
               + " rendered=" + JSON.stringify(waveform.layers))

        SettingsController.waveformMode = 2
        wait(0)
        SettingsController.waveformMode = 1
        wait(0)
        verify(waveform.layers.mix && waveform.layers.mix.length === 4,
               "switching away from spectrum must restore the analysed waveform")
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

    function test_bottom_actions_use_uploaded_immersive_and_lyrics_icons() {
        var shell = enterIntegratedShell()
        var actions = findChild(shell, "playerSecondaryActions")
        var audioTools = findChild(shell, "audioToolsButton")
        var miniPlayer = findChild(shell, "miniPlayerButton")
        var immersive = findChild(shell, "immersiveActionButton")
        var lyrics = findChild(shell, "lyricsActionButton")
        verify(actions && audioTools && miniPlayer && immersive && lyrics)
        compare(findChild(shell, "playerShellModeButton"), null)
        compare(findChild(shell, "themeActionButton"), null)
        compare(audioTools.parent, actions)
        compare(miniPlayer.parent, actions)
        verify(immersive.icon.source.toString().endsWith(
                   "/immersive-visual-mode.svg"))
        verify(lyrics.icon.source.toString().endsWith("/lyrics.svg"))
    }

    function test_right_panel_uses_outlined_glass_controls() {
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
        verify(tagOutline.color.a > lyricsOutline.color.a,
               "the active tab needs a subtle filled highlight")
        verify(searchGlass.color.a > 0 && searchGlass.color.a < 0.35)
        verify(addGlass.color.a > 0 && addGlass.color.a < 0.35)
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

    function test_wave_navigator_uses_light_glass_material() {
        var shell = enterIntegratedShell()
        var track = findChild(shell, "integratedWaveformNavigatorTrack")
        var thumb = findChild(shell, "integratedWaveformNavigatorThumb")
        var highlight = findChild(shell,
                                  "integratedWaveformNavigatorHighlight")
        verify(track && thumb && highlight)
        verify(track.color.a > 0 && track.color.a <= 0.09)
        verify(thumb.color.a > track.color.a && thumb.color.a <= 0.32)
        verify(thumb.border.color.a <= 0.12)
        verify(highlight.color.a > 0 && highlight.color.a <= 0.18)
    }

    function test_integrated_track_header_is_compact_and_bold() {
        var shell = enterIntegratedShell()
        var list = findChild(shell, "integratedTrackList")
        var title = findChild(shell, "trackHeaderTitle")
        verify(list && list.headerItem && title)
        compare(list.headerItem.height, 48)
        compare(title.font.weight, Font.DemiBold)
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
        verify(keyword.border.color.a <= 0.12)
        verify(bpm.border.color.a <= 0.12)
        compare(sliderTrack.height, 3)
        compare(firstHandle.width, 12)
        compare(firstHandle.height, 12)
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

    function test_integrated_major_outlines_use_soft_border() {
        var shell = enterIntegratedShell()
        var names = ["integratedLibraryColumn", "integratedTrackColumn",
                     "integratedTagColumn", "integratedWaveformFrame",
                     "integratedBottomBar"]
        for (var index = 0; index < names.length; ++index) {
            var surface = findChild(shell, names[index])
            verify(surface, names[index] + " missing")
            verify(surface.border.color.a <= 0.12,
                   names[index] + " border is too strong")
        }
    }

    function test_classic_shared_components_keep_default_visual_contract() {
        var list = classicTrackListComponent.createObject(testCase)
        var filter = classicSearchFilterComponent.createObject(testCase)
        var tags = classicTagPanelComponent.createObject(testCase)
        verify(list && filter && tags)

        compare(list.headerHeight, 46)
        compare(list.headerFontWeight, Font.DemiBold)
        compare(filter.moduleBorder.toString(), Theme.controlSubtleBorder.toString())
        var range = findChild(filter, "bpmRange")
        var sliderTrack = findChild(range, "rangeSliderTrack")
        verify(range && sliderTrack)
        verify(!range.glassStyle)
        compare(sliderTrack.height, 3)
        compare(tags.controlBorder.toString(),
                Theme.controlSubtleBorder.toString())
        tryCompare(findChild(tags, "tagSearchField"), "height", 34)
        tryCompare(findChild(tags, "addTagButton"), "height", 34)

        list.destroy()
        filter.destroy()
        tags.destroy()
    }

    function test_bottom_bar_centers_track_controls_and_actions() {
        var shell = enterIntegratedShell()
        var bottom = findChild(shell, "integratedBottomBar")
        var summary = findChild(shell, "integratedTrackSummary")
        var cover = findChild(shell, "integratedTrackCover")
        var metadata = findChild(shell, "integratedTrackMetadata")
        var center = findChild(shell, "centerPlaybackControls")
        var actions = findChild(shell, "playerSecondaryActions")
        verify(bottom && summary && cover && metadata && center && actions)
        verify(cover.width >= 64 && cover.height >= 64)
        verify(metadata.visible)
        fuzzyCompare(summary.mapToItem(bottom, 0, 0).y
                     + summary.height / 2, bottom.height / 2, 1.0)
        fuzzyCompare(center.y + center.height / 2,
                     center.parent.height / 2, 1.0)
        fuzzyCompare(actions.y + actions.height / 2,
                     actions.parent.height / 2, 1.0)
    }


    function test_bottom_track_title_has_more_room() {
        var shell = enterIntegratedShell()
        var previousWidth = mainWindow.width
        mainWindow.width = 1672
        var summary = findChild(shell, "integratedTrackSummary")
        verify(summary)
        tryVerify(function() { return summary.width >= 500 }, 1000)
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
        verify(bottomBar.height >= 91,
               "transport canvas should gain height while closing the gap")
    }

    function test_integrated_waveform_keeps_progress_color_on_one_canvas() {
        var shell = enterIntegratedShell()
        shell.playbackController = fakePlayback
        shell.waveformDurationMs = 100000
        fakePlayback.positionMs = 25000
        var waveform = findChild(shell, "integratedWaveform")
        verify(waveform)
        tryCompare(waveform, "position", 25000)
        tryCompare(waveform, "cursorPosition", 25000)
        compare(findChild(shell, "integratedPlayedWaveform"), null,
                "progress colour must come from the main canvas, not a clipped duplicate")
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
