import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "FormatConverterWorkbench"
    when: windowShown
    visible: true
    width: 1672
    height: 942

    FormatConvertPage {
        id: page
        anchors.fill: parent
    }

    function init() {
        if (!FormatConverter.busy)
            FormatConverter.clear()
        tryCompare(FormatConverter, "fileCount", 0, 3000)
    }

    function test_referenceGeometryAndControls() {
        const toolbar = findChild(page, "formatToolbar")
        const taskPanel = findChild(page, "formatTaskPanel")
        const settingsPanel = findChild(page, "formatSettingsPanel")
        const bottomBar = findChild(page, "formatBottomBar")
        verify(toolbar && taskPanel && settingsPanel && bottomBar)
        compare(Math.round(toolbar.height), 60)
        compare(Math.round(bottomBar.height), 114)
        verify(settingsPanel.width >= 443 && settingsPanel.width <= 447)
        compare(Math.round(settingsPanel.x - (taskPanel.x + taskPanel.width)), 8)
        verify(findChild(page, "formatSearchField"))
        verify(findChild(page, "formatSelectAllCheck"))
        verify(findChild(page, "formatEncoderBox"))
        verify(findChild(page, "formatPresetBox"))
        verify(findChild(page, "formatBitrateModeBox"))
        verify(findChild(page, "formatBitRateBox"))
        verify(findChild(page, "formatAdvancedToggle"))
        verify(findChild(page, "formatOutputDirectoryRow"))
        verify(findChild(page, "formatTotalProgress"))
        const formatBox = findChild(page, "converterOutputFormatBox")
        verify(formatBox)
        compare(formatBox.count, 8)
    }

    function test_smart_profiles_keep_auto_channels_and_format_specific_rates() {
        const preset = findChild(page, "formatPresetBox")
        const bitRate = findChild(page, "formatBitRateBox")
        const channel = findChild(page, "formatChannelBox")
        verify(preset && bitRate && channel)
        compare(preset.currentValue, "recommended")
        compare(channel.currentValue, "")

        FormatConverter.selectedFormat = "opus"
        tryCompare(FormatConverter, "selectedFormat", "opus")
        tryCompare(bitRate, "currentValue", 192000)
        verify(bitRate.count >= 4)

        FormatConverter.selectedFormat = "flac"
        tryCompare(FormatConverter, "selectedFormat", "flac")
        compare(bitRate.enabled, false)
    }

    function test_realImportSelectionAndPreflight() {
        FormatConverter.addUrls([testAudioUrl])
        tryVerify(function() { return !FormatConverter.busy }, 5000)
        compare(FormatConverter.fileCount, 1)
        compare(FormatConverter.checkedCount, 1)
        FormatConverter.selectedFormat = "flac"
        const plan = FormatConverter.buildPreflight({
            outputFormat: "flac",
            outputDir: "",
            keepMetadata: true,
            keepCover: false,
            sampleFormat: "s16",
            channelLayout: "stereo"
        })
        verify(plan.ready)
        compare(plan.taskCount, 1)
        FormatConverter.rejectPendingPlan()
    }
}
