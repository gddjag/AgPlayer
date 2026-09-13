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
