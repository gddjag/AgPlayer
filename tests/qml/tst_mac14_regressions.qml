import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: suite
    name: "Mac14Regressions"
    when: windowShown
    property var main: testMainWindow
    SignalSpy { id: searchSpy; target: WindowController; signalName: "searchRequested" }
    Component { id: losslessPage; LosslessIdentifyPage { width: 1100; height: 700 } }
    Component { id: title; TitleBar { width: 860; height: Theme.titleBarHeight; showBrand: true } }
    Component { id: settingsPage; SettingsPage { width: 1040; height: 720 } }
    Component { id: listPage; ListWindow { width: 1000; height: 620 } }
    Component { id: mini; MiniPlayerWindow {} }
    Component { id: editorPage; AudioEditorPage { width: 980; height: 720 } }
    Component { id: metadataPage; MetadataEditPage { width: 980; height: 720 } }
    Component { id: filenamePage; FilenameProcessPage { width: 980; height: 720 } }
    Component { id: evidencePage; LosslessIdentifyPage { width: 980; height: 720 } }

    function test_compactMacToolsUseOneScrollingPage() {
        if (Qt.platform.os !== "osx") skip("macOS compact tool layout")
        const editor = createTemporaryObject(editorPage, main.contentItem)
        const metadata = createTemporaryObject(metadataPage, main.contentItem)
        const filename = createTemporaryObject(filenamePage, main.contentItem)
        const lossless = createTemporaryObject(evidencePage, main.contentItem)
        verify(editor && metadata && filename && lossless)
        verify(editor.macStackedLayout)
        const tracks = findChild(editor, "editorTrackScroller")
        verify(tracks && tracks.height >= tracks.contentHeight)
        const inspector = findChild(editor, "editorInspector")
        verify(inspector.y >= findChild(editor, "editorMainColumn").height)
        verify(findChild(editor, "editorPageScroller").contentHeight > editor.height)

        verify(metadata.macStackedLayout)
        verify(!findChild(metadata, "metadataCompactTabs").visible)
        const files = findChild(metadata, "metadataFilePanel")
        const fields = findChild(metadata, "metadataInspectorPanel")
        verify(files.visible && fields.visible && fields.y >= files.y + files.height)
        verify(findChild(metadata, "metadataWorkbenchScroller").contentHeight > 0)

        verify(filename.macStackedLayout)
        verify(!findChild(filename, "filenameCompactTabs").visible)
        verify(findChild(filename, "filenameFilePanel").visible)
        verify(findChild(filename, "filenameRulesPanel").visible)
        verify(findChild(filename, "filenameWorkspaceScroller").contentHeight > filename.height)

        verify(lossless.macStackedLayout)
        verify(!findChild(lossless, "losslessCompactViewSwitch").visible)
        verify(findChild(lossless, "losslessEvidencePanel").visible)
        verify(findChild(lossless, "losslessConclusionPanel").visible)
        verify(findChild(lossless, "losslessContentScroller").contentHeight > lossless.height)
    }

    function test_miniBrandStaysCenteredOnMac() {
        if (Qt.platform.os !== "osx") skip("macOS title alignment")
        const window = createTemporaryObject(mini, null, { visible: true })
        verify(window)
        const brand = findChild(window, "miniTitleBrand")
        verify(brand)
        for (const width of [480, 588, 900]) {
            window.width = width
            tryVerify(function() {
                return brand.width > 0 && Math.abs(brand.x + brand.width / 2 - brand.parent.width / 2) < 1
            })
        }
    }

    function init() {
        WindowController.hideAudioTools()
        main.showNormal()
        main.requestActivate()
        main.contentItem.forceActiveFocus()
        searchSpy.clear()
        wait(100)
    }

    function test_shortcutsParseAndActivate() {
        const search = findChild(main, "searchShortcut")
        const tools = findChild(main, "audioToolsShortcut")
        verify(search && tools)
        compare(search.portableText, "Ctrl+F")
        compare(tools.portableText, "Alt+D")
        verify(nativeDropHelper.sendKey(main, Qt.Key_F, Qt.ControlModifier))
        tryCompare(searchSpy, "count", 1)
        main.requestActivate()
        main.contentItem.forceActiveFocus()
        verify(nativeDropHelper.sendKey(main, Qt.Key_D, Qt.AltModifier))
        tryCompare(WindowController, "audioToolsVisible", true)
        WindowController.hideAudioTools()
        main.contentItem.forceActiveFocus()
        main.requestActivate()
        const oldMode = SettingsController.waveformMode
        verify(nativeDropHelper.sendKey(main, Qt.Key_Tab))
        tryVerify(function() { return SettingsController.waveformMode !== oldMode })
    }

    function test_brandCenterOnMac() {
        if (Qt.platform.os !== "osx") skip("macOS title positioning")
        const bar = createTemporaryObject(title, main.contentItem)
        const brand = findChild(bar, "titleBrand")
        verify(brand)
        tryVerify(function() { return Math.abs(brand.x + brand.width / 2 - bar.width / 2) < 1 })
        if (visualFixtureOutput) {
            let saved = false
            bar.grabToImage(function(result) { saved = result.saveToFile(visualFixtureOutput) })
            tryVerify(function() { return saved }, 5000)
        }
    }

    function test_searchShortcutFocusesVisibleShell_data() {
        return [
            {tag: "classic-default", mode: 0, combo: "Ctrl + F", key: Qt.Key_F, modifiers: Qt.ControlModifier, target: "librarySearchFilter"},
            {tag: "integrated-default", mode: 1, combo: "Ctrl + F", key: Qt.Key_F, modifiers: Qt.ControlModifier, target: "integratedSearchFilter"},
            {tag: "rolling-default", mode: 2, combo: "Ctrl + F", key: Qt.Key_F, modifiers: Qt.ControlModifier, target: "rollingSearchFilter"},
            {tag: "classic-custom", mode: 0, combo: "Ctrl + Shift + J", key: Qt.Key_J, modifiers: Qt.ControlModifier | Qt.ShiftModifier, target: "librarySearchFilter"},
            {tag: "integrated-custom", mode: 1, combo: "Ctrl + Shift + J", key: Qt.Key_J, modifiers: Qt.ControlModifier | Qt.ShiftModifier, target: "integratedSearchFilter"},
            {tag: "rolling-custom", mode: 2, combo: "Ctrl + Shift + J", key: Qt.Key_J, modifiers: Qt.ControlModifier | Qt.ShiftModifier, target: "rollingSearchFilter"}
        ]
    }

    function test_searchShortcutFocusesVisibleShell(data) {
        const oldMode = SettingsController.playerShellMode
        const oldShortcut = SettingsController.hkSearch
        const filter = findChild(main, "filterModel")
        const oldSearch = filter.searchText
        let list = null
        try {
            SettingsController.playerShellMode = data.mode
            SettingsController.hkSearch = data.combo
            filter.searchText = "search probe"
            list = createTemporaryObject(listPage, null, {filterModel: filter, visible: data.mode === 0})
            verify(list)
            const targetRoot = data.mode === 0 ? list : main
            tryVerify(function() { return findChild(targetRoot, data.target) !== null })
            const search = findChild(targetRoot, data.target)
            const field = findChild(search, "librarySearchField")
            verify(field && field.visible)
            // The QML harness owns the actual list window separately from the
            // controller's native-window fixture. Activate the current host.
            targetRoot.requestActivate()
            targetRoot.contentItem.forceActiveFocus()
            wait(100)
            searchSpy.clear()
            verify(nativeDropHelper.sendKey(targetRoot, data.key, data.modifiers))
            tryCompare(searchSpy, "count", 1)
            tryCompare(field, "activeFocus", true, 1000)
            compare(field.selectedText, "search probe")
            wait(50)
            compare(searchSpy.count, 1)
            if (data.mode !== 0) verify(!list.visible)
        } finally {
            SettingsController.hkSearch = oldShortcut
            SettingsController.playerShellMode = oldMode
            filter.searchText = oldSearch
            if (list) list.close()
            main.requestActivate()
        }
    }

    function test_shortcutCaptureUsesKeyCodeInsteadOfGeneratedText() {
        const page = createTemporaryObject(settingsPage, main.contentItem)
        verify(page)
        compare(page.shortcutText({key: Qt.Key_D, modifiers: Qt.AltModifier, text: "∂"}), "Alt + D")
        compare(page.shortcutText({key: Qt.Key_E, modifiers: Qt.AltModifier, text: ""}), "Alt + E")
        compare(page.shortcutText({key: Qt.Key_F, modifiers: Qt.ControlModifier, text: "\u0006"}), "Ctrl + F")
        compare(page.shortcutText({key: Qt.Key_A, modifiers: Qt.ControlModifier | Qt.ShiftModifier, text: "A"}), "Ctrl + Shift + A")
        compare(page.shortcutText({key: Qt.Key_MediaPlay, modifiers: Qt.ShiftModifier, text: ""}), "Shift + MediaPlayPause")
    }

    function test_selectedFileEntersRealLosslessQueue() {
        LosslessAnalysisController.clear()
        const page = createTemporaryObject(losslessPage, main.contentItem)
        page.importSelectedFiles([testAudioUrl])
        tryCompare(LosslessAnalysisController, "totalCount", 1, 5000)
        compare(LosslessAnalysisController.error, "")
        LosslessAnalysisController.start()
        tryCompare(LosslessAnalysisController, "completedCount", 1, 15000)
        LosslessAnalysisController.clear()
    }
}
