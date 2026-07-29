import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "LightEditor"
    when: windowShown

    width: 1350
    height: 900

    Component {
        id: pageComponent
        LightEditPage {
            width: 1350
            height: 900
        }
    }

    Component {
        id: toolsWindowComponent
        AudioToolsWindow {
            visible: false
        }
    }

    Component {
        id: formatPageComponent
        FormatConvertPage {
            width: 1350
            height: 900
        }
    }

    Component {
        id: speedPageComponent
        SpeedAdjustPage {
            width: 1350
            height: 900
        }
    }

    property var page

    function init() {
        page = createTemporaryObject(pageComponent, testCase)
        verify(page)
        wait(50)
    }

    function collectByObjectName(item, name, result) {
        if (!item) return
        if (item.objectName === name) result.push(item)
        const childItems = item.children || []
        for (let i = 0; i < childItems.length; ++i)
            collectByObjectName(childItems[i], name, result)
    }

    function test_newEditorControlsAndSixLanesExist() {
        verify(findChild(page, "targetBpmField"))
        verify(findChild(page, "snapGridBox"))
        verify(findChild(page, "unifyBpmButton"))
        verify(findChild(page, "alignBpmButton"))
        verify(findChild(page, "keepPitchSwitch"))
        verify(findChild(page, "outputFormatBox"))
        verify(findChild(page, "outputSampleRateBox"))
        verify(findChild(page, "outputChannelBox"))
        verify(findChild(page, "exportAudioButton"))
        verify(findChild(page, "cutClipButton"))
        verify(findChild(page, "copyClipButton"))
        verify(findChild(page, "pasteClipButton"))
        verify(findChild(page, "deleteClipButton"))
        verify(findChild(page, "splitClipButton"))
        verify(findChild(page, "mergeClipButton"))
        verify(findChild(page, "cropClipButton"))
        verify(findChild(page, "lightPreviewButton"))
        verify(findChild(page, "lightPreviewVolume"))

        compare(findChild(page, "cutClipButton").enabled, false)
        compare(findChild(page, "pasteClipButton").enabled, false)

        const lanes = []
        collectByObjectName(page, "trackLane", lanes)
        compare(lanes.length, 6)
    }

    function test_wheelZoomEntryPointChangesScale() {
        compare(page.zoomScale, 1.0)
        verify(typeof page.applyWheelZoom === "function")
        page.applyWheelZoom(120, page.width / 2)
        verify(page.zoomScale > 1.0)
    }

    function test_audioToolsWindowUsesReferenceSize() {
        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        compare(toolsWindow.width, 1536)
        compare(toolsWindow.height, 1024)
    }

    function test_formatConverterOptionsUseIndependentDefaults() {
        SettingsController.preserveMetadata = false
        const formatPage = createTemporaryObject(formatPageComponent, testCase)
        verify(formatPage)
        const keepMetadata = findChild(formatPage, "keepMetadataCheck")
        const extractAudio = findChild(formatPage, "extractAudioCheck")
        verify(keepMetadata)
        verify(extractAudio)
        compare(keepMetadata.checked, false)
        compare(extractAudio.checked, false)
        SettingsController.preserveMetadata = true
        tryCompare(keepMetadata, "checked", true)
        compare(extractAudio.checked, false)
    }

    function test_keepPitchSettingUpdatesOpenToolPages() {
        const speedPage = createTemporaryObject(speedPageComponent, testCase)
        verify(speedPage)

        SettingsController.keepPitchWhileSpeedChange = false
        tryCompare(LightEditor, "keepPitch", false)
        tryCompare(SpeedAdjuster, "keepPitch", false)

        SettingsController.keepPitchWhileSpeedChange = true
        tryCompare(LightEditor, "keepPitch", true)
        tryCompare(SpeedAdjuster, "keepPitch", true)
    }
}
