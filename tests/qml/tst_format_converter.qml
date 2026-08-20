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
        verify(findChild(page, "formatSearchField"))
        verify(findChild(page, "formatSelectAllCheck"))
        verify(findChild(page, "formatEncoderBox"))
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
