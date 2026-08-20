import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "FormatConverterWorkbench"
    when: windowShown
    visible: true
    width: 1672
    height: 941

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
        compare(testCase.height, 941)
        compare(Math.round(toolbar.height), 60)
        compare(Math.round(bottomBar.height), 114)
        verify(settingsPanel.width >= 443 && settingsPanel.width <= 447)
        compare(Math.round(settingsPanel.x - (taskPanel.x + taskPanel.width)), 8)
        verify(findChild(page, "formatStatusFilters"))
        compare(findChild(page, "formatSearchField"), null)
        compare(findChild(page, "converterParallelJobsBox"), null)
        verify(findChild(page, "formatSelectAllCheck"))
        verify(findChild(page, "formatEncoderBox"))
        verify(findChild(page, "formatPresetBox"))
        verify(findChild(page, "formatBitrateModeBox"))
        verify(findChild(page, "formatBitRateBox"))
        verify(findChild(page, "formatAdvancedToggle"))
        verify(findChild(page, "formatOutputDirectoryRow"))
        verify(findChild(page, "formatTotalProgress"))
        const localProcessingHint = findChild(page, "formatLocalProcessingHint")
        verify(localProcessingHint.visible)
        const hintPosition = localProcessingHint.mapToItem(settingsPanel, 0, 0)
        verify(hintPosition.y + localProcessingHint.height <= settingsPanel.height)
        verify(findChild(page, "formatSettingsAdvancedToggle"))
        verify(findChild(page, "formatTaskContextMenu"))
        verify(!findChild(page, "formatFooterParallelJobs"))
        verify(!findChild(page, "formatFooterOutputDirectory"))
        const formatBox = findChild(page, "converterOutputFormatBox")
        verify(formatBox)
        compare(formatBox.count, 8)
    }

    function test_compact_geometry_keeps_all_workbench_regions_visible() {
        testCase.width = 880
        testCase.height = 457
        wait(0)
        const taskPanel = findChild(page, "formatTaskPanel")
        const settingsPanel = findChild(page, "formatSettingsPanel")
        const bottomBar = findChild(page, "formatBottomBar")
        const start = findChild(page, "convertAllButton")
        const cancel = findChild(page, "cancelAllButton")
        verify(taskPanel && settingsPanel && bottomBar && start && cancel)
        verify(taskPanel.width >= 420)
        verify(settingsPanel.width <= 380)
        verify(start.x + start.width <= bottomBar.width)
        verify(cancel.x + cancel.width <= bottomBar.width)
        testCase.width = 1672
        testCase.height = 941
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

    function test_outputDirectoryTracksSettingsController() {
        const originalDirectory = SettingsController.defaultOutputDirectory
        compare(page.outputDirectory, originalDirectory)

        const testDirectory = originalDirectory + "/format-converter-qml-test"
        page.outputDirectory = testDirectory
        compare(SettingsController.defaultOutputDirectory, testDirectory)

        SettingsController.defaultOutputDirectory = originalDirectory
        compare(page.outputDirectory, originalDirectory)
    }

    function test_settingsPanelChevronCollapsesWorkbench() {
        const taskPanel = findChild(page, "formatTaskPanel")
        const settingsPanel = findChild(page, "formatSettingsPanel")
        verify(taskPanel && settingsPanel)

        settingsPanel.expanded = false
        tryCompare(settingsPanel, "width", 40, 1000)
        verify(taskPanel.width > settingsPanel.width)

        settingsPanel.expanded = true
        tryVerify(function() { return settingsPanel.width >= 443 }, 1000)
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
