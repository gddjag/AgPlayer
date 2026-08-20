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
        testCase.width = 1672
        testCase.height = 941
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
        verify(!findChild(bottomBar, "converterParallelJobsBox"))
        verify(!findChild(bottomBar, "formatOutputDirectoryRow"))
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
        const toggle = findChild(page, "formatSettingsAdvancedToggle")
        verify(taskPanel && settingsPanel && toggle)

        mouseClick(toggle, toggle.width / 2, toggle.height / 2, Qt.LeftButton)
        tryCompare(settingsPanel, "width", 40, 1000)
        verify(taskPanel.width > settingsPanel.width)

        mouseClick(toggle, toggle.width / 2, toggle.height / 2, Qt.LeftButton)
        tryVerify(function() { return settingsPanel.width >= 443 }, 1000)
    }

    function test_parallelJobsPersistThroughSettingsController() {
        const parallelBox = findChild(page, "converterParallelJobsBox")
        verify(parallelBox)
        SettingsController.parallelJobs = 3
        tryCompare(FormatConverter, "parallelJobs", 3, 1000)
        compare(parallelBox.currentValue, 3)
        SettingsController.parallelJobs = 4
        tryCompare(FormatConverter, "parallelJobs", 4, 1000)
    }

    function test_rowContextMenuRemovesExactlyOneTask() {
        const secondUrl = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(secondUrl.toString().length > 0)
        FormatConverter.addUrls([testAudioUrl, secondUrl])
        tryVerify(function() { return !FormatConverter.busy }, 5000)
        tryCompare(FormatConverter, "fileCount", 2, 3000)

        const table = findChild(page, "formatTaskTableView")
        const menu = findChild(page, "formatTaskContextMenu")
        const remove = findChild(page, "formatTaskRemoveMenuItem")
        verify(table && menu && remove)
        mouseClick(table, 100, 22, Qt.RightButton)
        tryVerify(function() { return menu.visible }, 1000)
        mouseClick(remove, remove.width / 2, remove.height / 2, Qt.LeftButton)
        tryCompare(FormatConverter, "fileCount", 1, 3000)
    }

    function test_fatalPreflightOpensErrorDialog() {
        const errorDialog = findChild(page, "formatErrorDialog")
        verify(errorDialog)
        page.requestPlan()
        tryVerify(function() { return errorDialog.visible }, 1000)
        verify(errorDialog.summary.length > 0)
        errorDialog.close()
    }

    function test_minimumWindowKeepsCoreActionsAndTableReachable() {
        const taskPanel = findChild(page, "formatTaskPanel")
        const addFile = findChild(page, "formatAddFileButton")
        const convert = findChild(page, "convertAllButton")
        const table = findChild(page, "formatTaskTableView")
        verify(taskPanel && addFile && convert && table)
        verify(page.usesCompactLayout(880))
        verify(!page.usesCompactLayout(1672))
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
