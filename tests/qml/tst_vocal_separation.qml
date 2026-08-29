import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "VocalSeparationWorkbench"
    when: windowShown
    visible: true
    width: 1672
    height: 941

    VocalSeparationPage {
        id: page
        anchors.fill: parent
    }

    Component {
        id: viewportPage
        VocalSeparationPage { }
    }

    Component {
        id: navigationComponent
        ToolSidebar { width: 1200; height: 55 }
    }

    function test_navigationUsesStableVisibleToolIds() {
        const navigation = createTemporaryObject(navigationComponent, testCase)
        verify(navigation)
        compare(navigation.visibleTools.map(function(tool) { return tool.toolId }),
                [0, 4, 1, 2, 3])
        navigation.currentTool = 4
        compare(navigation.currentTool, 4)
    }

    function test_realEmptyStateAndRequiredRegions() {
        const input = findChild(page, "separationInputPanel")
        const waveform = findChild(page, "separationInputWaveform")
        const models = findChild(page, "separationModelDeck")
        const settings = findChild(page, "separationSettingsPanel")
        const stems = findChild(page, "separationStemSelector")
        const timeline = findChild(page, "separationTimeline")
        const history = findChild(page, "separationHistoryPanel")
        const bottom = findChild(page, "separationBottomBar")
        const primary = findChild(page, "separationPrimaryAction")
        const playlistAction = findChild(page, "separationPlaylistAction")
        verify(input && waveform && models && settings && stems && timeline
               && history && bottom && primary && playlistAction)
        compare(VocalSeparationController.inputInfo.name, undefined)
        verify(!primary.enabled)
        verify(!playlistAction.enabled)
        verify(primary.Accessible.name.length > 0)
    }

    function test_catalogStemCapabilitiesAreBoundToTheRealCatalog() {
        compare(VocalSeparationController.models.length, 3)
        verify(page.modelSupports("uvr-mdxnet-kara", "vocals"))
        verify(!page.modelSupports("uvr-mdxnet-kara", "drums"))
        verify(page.modelSupports("htdemucs-ft-fp16", "drums"))
        verify(page.modelSupports("htdemucs-ft-fp16", "other"))
    }

    function test_twoStemTimelineKeepsUnsupportedStemsVisibleAndLocalized() {
        compare(VocalSeparationController.stems.length, 5)
        const drums = page.stemInfo(VocalSeparationController.Drums)
        verify(!drums.supported && !drums.selected && !drums.available)
        compare(drums.path, "")
        compare(drums.waveform.length, 0)
        compare(page.stemLabel(VocalSeparationController.Vocals), "人声")
        compare(page.stemLabel(VocalSeparationController.Accompaniment), "伴奏")
        compare(page.stemLabel(VocalSeparationController.Drums), "鼓组")
        verify(findChild(page, "separationTimelineStem-" + VocalSeparationController.Drums))
        verify(findChild(page, "separationTimelineStem-" + VocalSeparationController.Other))
    }

    function test_playlistGateRequiresEverySelectedStemToBeAvailable() {
        verify(page.allSelectedStemsAvailable([
            { selected: true, available: true },
            { selected: false, available: false }
        ]))
        verify(!page.allSelectedStemsAvailable([
            { selected: true, available: true },
            { selected: true, available: false }
        ]))
        verify(!page.allSelectedStemsAvailable([]))
    }

    function test_nativeDropRoutesToTheRealSeparationController() {
        VocalSeparationController.clearInput()
        AudioToolsController.selectTool(4)
        verify(nativeDropHelper.sendUrls(page, [testAudioUrl]))
        tryVerify(function() {
            return VocalSeparationController.inputInfo.name !== undefined
                   && VocalSeparationController.inputInfo.name.length > 0
        }, 5000)
        VocalSeparationController.clearInput()
    }

    function test_responsiveViewportsKeepThePrimaryActionReachable() {
        const viewports = [[1672, 941], [1280, 720], [880, 560], [1920, 1080]]
        for (let index = 0; index < viewports.length; ++index) {
            const instance = createTemporaryObject(viewportPage, testCase,
                                                   { width: viewports[index][0],
                                                     height: viewports[index][1] })
            verify(instance)
            wait(0)
            const primary = findChild(instance, "separationPrimaryAction")
            const bottom = findChild(instance, "separationBottomBar")
            const models = findChild(instance, "separationModelDeck")
            verify(primary && bottom && models)
            const bottomPoint = bottom.mapToItem(instance, 0, 0)
            const actionPoint = primary.mapToItem(instance, 0, 0)
            verify(bottomPoint.y >= 0)
            verify(actionPoint.y + primary.height <= instance.height,
                   "primary CTA is clipped at " + viewports[index])
            if (viewports[index][0] >= 1100)
                verify(primary.width >= bottom.width * 0.25,
                       "desktop CTA stays prominent at " + viewports[index])
            if (viewports[index][0] < 1100)
                verify(bottom.height >= 96 && actionPoint.y >= bottomPoint.y + bottom.height / 2,
                       "compact actions use two rows at " + viewports[index])
        }
    }

    function test_desktopSideColumnKeepsTheReferenceThirtyPercentRatio() {
        const widths = [1440, 1672, 1920]
        for (let index = 0; index < widths.length; ++index) {
            const instance = createTemporaryObject(viewportPage, testCase,
                                                   { width: widths[index], height: 941 })
            verify(instance)
            wait(0)
            const side = findChild(instance, "separationSideColumn")
            verify(side)
            verify(side.width / instance.width >= 0.28
                   && side.width / instance.width <= 0.31,
                   "side column stays near 30% at " + widths[index])
        }
    }
}
