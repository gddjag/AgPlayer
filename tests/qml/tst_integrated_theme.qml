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
        compare(durationLabel.color.toString(), "#ffffff")
        compare(dragLabel.color.toString(), "#ffffff")

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

    function test_right_panel_tabs_collapse_and_persist() {
        var shell = enterIntegratedShell()
        var tagTab = findChild(shell, "integratedTagTabButton")
        var lyricsTab = findChild(shell, "integratedLyricsTabButton")
        var tagContent = findChild(shell, "integratedTagContent")
        var lyricsContent = findChild(shell, "integratedLyricsContent")
        var lyricsText = findChild(shell, "integratedLyricsText")
        var toggle = findChild(shell, "integratedSidePanelToggleButton")
        var column = findChild(shell, "integratedTagColumn")
        verify(tagTab && lyricsTab && tagContent && lyricsContent
               && lyricsText && toggle && column)
        verify(tagContent.visible)
        verify(!lyricsContent.visible)

        mouseClick(lyricsTab)
        verify(!tagContent.visible)
        verify(lyricsContent.visible)
        compare(lyricsText.text, "当前歌曲暂无内嵌歌词")
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

    function test_bottom_actions_share_uploaded_theme_icon() {
        var shell = enterIntegratedShell()
        var actions = findChild(shell, "playerSecondaryActions")
        var themeButton = findChild(shell, "playerShellModeButton")
        var audioTools = findChild(shell, "audioToolsButton")
        var miniPlayer = findChild(shell, "miniPlayerButton")
        verify(actions && themeButton && audioTools && miniPlayer)
        compare(themeButton.parent, actions)
        compare(audioTools.parent, actions)
        compare(miniPlayer.parent, actions)
        verify(themeButton.icon.source.toString().endsWith(
                   "/player-shell-mode.svg"))

        SettingsController.playerShellMode = 0
        tryVerify(function() {
            return findChild(mainWindow, "classicPlayerShell") !== null
        }, 1500)
        themeButton = findChild(mainWindow, "playerShellModeButton")
        verify(themeButton)
        verify(themeButton.icon.source.toString().endsWith(
                   "/player-shell-mode.svg"))
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
        compare(firstHandle.width, 14)
        compare(firstHandle.height, 14)
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

        compare(list.headerHeight, 56)
        compare(list.headerFontWeight, Font.Medium)
        compare(filter.moduleBorder.toString(), Theme.border.toString())
        var range = findChild(filter, "bpmRange")
        var sliderTrack = findChild(range, "rangeSliderTrack")
        verify(range && sliderTrack)
        verify(!range.glassStyle)
        compare(sliderTrack.height, 4)
        compare(tags.controlBorder.toString(),
                Theme.subtleGlassBorder.toString())
        tryCompare(findChild(tags, "tagSearchField"), "height", 38)
        tryCompare(findChild(tags, "addTagButton"), "height", 38)

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

    function test_shell_button_opens_real_mode_menu() {
        var shell = enterIntegratedShell()
        var button = findChild(shell, "playerShellModeButton")
        var menu = findChild(shell, "playerExperienceModeMenu")
        var classic = findChild(shell, "classicShellModeMenuItem")
        var integrated = findChild(shell, "integratedShellModeMenuItem")
        var immersive = findChild(shell, "immersiveVisualModeMenuItem")
        verify(button && menu && classic && integrated && immersive)

        button.clicked()
        tryCompare(menu, "opened", true)
        compare(immersive.enabled, false)
        verify(immersive.text.indexOf("待集成") >= 0)

        integrated.clicked()
        compare(SettingsController.playerShellMode, 1)

        button.clicked()
        tryCompare(menu, "opened", true)
        classic.clicked()
        tryCompare(SettingsController, "playerShellMode", 0)

        var classicShell = null
        tryVerify(function() {
            classicShell = findChild(mainWindow, "classicPlayerShell")
            return classicShell !== null
        }, 1500)
        var classicButton = findChild(classicShell, "playerShellModeButton")
        var classicMenu = findChild(classicShell,
                                    "playerExperienceModeMenu")
        var switchToIntegrated = findChild(
                    classicShell, "integratedShellModeMenuItem")
        verify(classicButton && classicMenu && switchToIntegrated)
        compare(switchToIntegrated.text, "单窗口模式")
        SettingsController.playerShellMode = 1
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
