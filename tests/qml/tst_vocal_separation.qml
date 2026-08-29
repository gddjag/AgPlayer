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
        compare(navigation.visibleToolOrder, [0, 4, 1, 2, 3])
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
        verify(input && waveform && models && settings && stems && timeline
               && history && bottom && primary)
        compare(VocalSeparationController.inputInfo.name, undefined)
        verify(!primary.enabled)
        verify(primary.Accessible.name.length > 0)
    }

    function test_catalogStemCapabilitiesAreBoundToTheRealCatalog() {
        compare(VocalSeparationController.models.length, 3)
        verify(page.modelSupports("uvr-mdxnet-kara", "vocals"))
        verify(!page.modelSupports("uvr-mdxnet-kara", "drums"))
        verify(page.modelSupports("htdemucs-ft-fp16", "drums"))
        verify(page.modelSupports("htdemucs-ft-fp16", "other"))
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
            verify(primary && bottom)
            const bottomPoint = bottom.mapToItem(instance, 0, 0)
            const actionPoint = primary.mapToItem(instance, 0, 0)
            verify(bottomPoint.y >= 0)
            verify(actionPoint.y + primary.height <= instance.height,
                   "primary CTA is clipped at " + viewports[index])
        }
    }
}
