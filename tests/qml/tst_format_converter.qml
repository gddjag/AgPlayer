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
        tryVerify(function() {
            return Math.round(taskPanel.mapToItem(page, 0, 0).y) === 65
                   && Math.round(settingsPanel.mapToItem(page, 0, 0).y) === 65
                   && Math.round(bottomBar.mapToItem(page, 0, 0).y) === 827
        }, 1000)
        verify(settingsPanel.width >= 443 && settingsPanel.width <= 447)
        compare(Math.round(settingsPanel.x - (taskPanel.x + taskPanel.width)), 8)
        let filenameHeaderCell = null
        tryVerify(function() {
            filenameHeaderCell = findChild(page, "formatHeaderCell-1")
            return filenameHeaderCell && filenameHeaderCell.width > 0
        }, 1000)
        const filenameHeaderPosition = filenameHeaderCell.mapToItem(page, 30, 0)
        verify(Math.round(filenameHeaderPosition.x) >= 92
               && Math.round(filenameHeaderPosition.x) <= 96)
        verify(findChild(page, "formatSearchField"))
        verify(findChild(page, "formatSelectAllCheck"))
        const metadataCheck = findChild(page, "keepMetadataCheck")
        const extractAudioCheck = findChild(page, "extractAudioCheck")
        const metadataIndicator = findChild(page, "keepMetadataCheckIndicator")
        const metadataMark = findChild(page, "keepMetadataCheckMark")
        const extractIndicator = findChild(page, "extractAudioCheckIndicator")
        verify(metadataCheck && extractAudioCheck && metadataIndicator && metadataMark && extractIndicator)
        compare(Math.round(metadataIndicator.width), 20)
        compare(Math.round(metadataIndicator.radius), 3)
        compare(metadataIndicator.color.toString(), "#1688ff")
        verify(metadataMark.visible)
        verify(metadataMark.source.toString().indexOf("check-line") >= 0)
        compare(extractIndicator.color.toString(), "#0c1821")
        verify(findChild(page, "formatEncoderBox"))
        verify(findChild(page, "formatOutputDirectoryRow"))
        verify(findChild(page, "formatTotalProgress"))
        const outputFormatGrid = findChild(page, "formatOutputFormatGrid")
        const firstFormatButton = findChild(page, "formatOutputFormatButton-mp3")
        const fourthFormatButton = findChild(page, "formatOutputFormatButton-aac")
        const encoderBox = findChild(page, "formatEncoderBox")
        const encoderChevron = findChild(page, "formatEncoderBoxChevron")
        const outputDirectoryRow = findChild(page, "formatOutputDirectoryRow")
        verify(outputFormatGrid && firstFormatButton && fourthFormatButton
               && encoderBox && encoderChevron && outputDirectoryRow)
        tryVerify(function() {
            return outputFormatGrid.width > 0 && encoderBox.width > 0
                   && outputDirectoryRow.width > 0
        }, 1000)
        const formatGridPosition = outputFormatGrid.mapToItem(settingsPanel, 0, 0)
        const firstFormatPosition = firstFormatButton.mapToItem(settingsPanel, 0, 0)
        const fourthFormatPosition = fourthFormatButton.mapToItem(settingsPanel, 0, 0)
        const encoderPosition = encoderBox.mapToItem(settingsPanel, 0, 0)
        const outputDirectoryPosition = outputDirectoryRow.mapToItem(settingsPanel, 0, 0)
        verify(Math.round(formatGridPosition.x) === 16)
        verify(Math.round(outputFormatGrid.width) >= 383 && Math.round(outputFormatGrid.width) <= 385)
        verify(Math.round(firstFormatButton.width) >= 89 && Math.round(firstFormatButton.width) <= 91)
        verify(Math.round(fourthFormatPosition.x - firstFormatPosition.x) === 294)
        verify(Math.round(encoderPosition.x) === 149)
        verify(Math.round(outputDirectoryPosition.x) === 149)
        verify(encoderChevron.visible)
        verify(encoderChevron.source.toString().indexOf("arrow-down-s-line") >= 0)
        const cancelAll = findChild(page, "cancelAllButton")
        const cancelIcon = findChild(page, "cancelAllButtonStopIcon")
        verify(cancelAll && cancelIcon)
        verify(cancelIcon.source.toString().indexOf("checkbox-blank-fill") >= 0)
        const localProcessingHint = findChild(page, "formatLocalProcessingHint")
        verify(localProcessingHint.visible)
        verify(findChild(page, "formatSettingsAdvancedToggle"))
        verify(findChild(page, "formatTaskContextMenu"))
        verify(!findChild(bottomBar, "converterParallelJobsBox"))
        verify(!findChild(bottomBar, "formatOutputDirectoryRow"))
        const formatBox = findChild(page, "converterOutputFormatBox")
        verify(formatBox)
        compare(formatBox.count, 8)
    }

    function test_outputCatalogUsesAiffInsteadOfM4a() {
        const aiffButton = findChild(page, "formatOutputFormatButton-aiff")
        const m4aButton = findChild(page, "formatOutputFormatButton-m4a")
        verify(aiffButton, "AIFF must be one of the eight conversion outputs")
        verify(!m4aButton, "M4A remains an accepted input container, not an output format")
        compare(aiffButton.text, "AIFF")
    }

    function test_channelSelectionDefaultsToAutomatic() {
        const channelBox = findChild(page, "formatChannelBox")
        verify(channelBox)
        compare(channelBox.currentValue, "")
        compare(channelBox.displayText, qsTr("自动"))
    }

    function test_lossyFormatsExposeRecommendedDefaults() {
        const bitRateLabel = findChild(page, "formatBitRateLabel")
        const bitRateBox = findChild(page, "formatBitRateBox")
        const qualityLabel = findChild(page, "formatQualityLabel")
        const qualityBox = findChild(page, "formatQualityBox")
        const sampleRateBox = findChild(page, "formatSampleRateBox")
        const bitDepthBox = findChild(page, "formatBitDepthBox")
        const settingsPanel = findChild(page, "formatSettingsPanel")
        verify(bitRateLabel && bitRateBox && qualityLabel && qualityBox
               && sampleRateBox && bitDepthBox && settingsPanel)

        FormatConverter.selectedFormat = "mp3"
        tryCompare(bitRateBox, "currentValue", 320000, 1000)
        compare(bitRateLabel.visible, true)
        compare(qualityLabel.visible, false)
        compare(sampleRateBox.currentValue, 0)
        compare(bitDepthBox.currentValue, "")
        compare(bitDepthBox.count, 1)
        compare(bitDepthBox.enabled, false)
        compare(settingsPanel.sampleFormat, "")
        compare(settingsPanel.bitrateMode, "cbr")
        compare(settingsPanel.quality, 0)

        FormatConverter.selectedFormat = "aac"
        tryCompare(bitRateBox, "currentValue", 256000, 1000)
        compare(settingsPanel.quality, 0)

        FormatConverter.selectedFormat = "opus"
        tryCompare(bitRateBox, "currentValue", 192000, 1000)
        compare(sampleRateBox.currentValue, 48000)
        compare(settingsPanel.quality, 0)

        FormatConverter.selectedFormat = "ogg"
        tryCompare(qualityBox, "currentValue", 6, 1000)
        compare(bitRateLabel.visible, false)
        compare(qualityLabel.visible, true)
        compare(qualityLabel.text, qsTr("质量等级"))
        compare(settingsPanel.bitrateMode, "")
        compare(sampleRateBox.count, 3)
    }

    function test_losslessFormatsExposeOnlyApplicableParameters() {
        const bitRateLabel = findChild(page, "formatBitRateLabel")
        const qualityLabel = findChild(page, "formatQualityLabel")
        const qualityBox = findChild(page, "formatQualityBox")
        const bitDepthLabel = findChild(page, "formatBitDepthLabel")
        const bitDepthBox = findChild(page, "formatBitDepthBox")
        verify(bitRateLabel && qualityLabel && qualityBox && bitDepthLabel && bitDepthBox)

        FormatConverter.selectedFormat = "flac"
        tryCompare(qualityBox, "currentValue", 5, 1000)
        compare(bitRateLabel.visible, false)
        compare(qualityLabel.visible, true)
        compare(qualityLabel.text, qsTr("压缩等级"))
        compare(bitDepthLabel.visible, true)
        compare(bitDepthBox.currentValue, "")

        for (const format of ["wav", "alac", "aiff"]) {
            FormatConverter.selectedFormat = format
            tryCompare(FormatConverter, "selectedFormat", format, 1000)
            compare(bitRateLabel.visible, false)
            compare(qualityLabel.visible, false)
            compare(bitDepthLabel.visible, true)
            compare(bitDepthBox.currentValue, "")
        }
    }

    function test_realShellBodyShowsCompleteLocalProcessingHint() {
        testCase.height = 833
        wait(0)
        const settingsPanel = findChild(page, "formatSettingsPanel")
        const hint = findChild(page, "formatLocalProcessingHint")
        const advanced = findChild(page, "formatAdvancedSettings")
        verify(settingsPanel && hint && advanced)
        tryVerify(function() {
            return settingsPanel.height > 630 && hint.visible && hint.height > 0
        }, 1000)
        const hintPosition = hint.mapToItem(settingsPanel, 0, 0)
        verify(hintPosition.y >= 44)
        verify(hintPosition.y + hint.height <= settingsPanel.height)
        verify(hintPosition.y >= 566 && hintPosition.y <= 572)
        const advancedPosition = advanced.mapToItem(settingsPanel, 0, 0)
        verify(advancedPosition.y >= settingsPanel.height)
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
        let firstCell = null
        tryVerify(function() {
            firstCell = findChild(table, "formatTaskFirstFilenameCell")
            return firstCell && firstCell.visible && firstCell.width > 0 && firstCell.height > 0
                   && table.contentWidth > 0 && table.contentHeight > 0
        }, 3000)
        // Let TableView finish polishing its delegate before sending a full
        // right-button gesture to that live row, rather than to the flickable.
        wait(100)
        mousePress(firstCell, firstCell.width / 2, firstCell.height / 2, Qt.RightButton)
        wait(20)
        mouseRelease(firstCell, firstCell.width / 2, firstCell.height / 2, Qt.RightButton)
        tryVerify(function() { return menu.visible }, 2000)
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
        testCase.width = 880
        testCase.height = 560
        wait(0)
        const taskPanel = findChild(page, "formatTaskPanel")
        const settingsPanel = findChild(page, "formatSettingsPanel")
        const addFile = findChild(page, "formatAddFileButton")
        const convert = findChild(page, "convertAllButton")
        const table = findChild(page, "formatTaskTableView")
        const search = findChild(page, "formatSearchField")
        verify(taskPanel && settingsPanel && addFile && convert && table && search)
        tryCompare(settingsPanel, "width", 40, 1000)
        verify(addFile.visible && convert.visible && !search.visible)
        const addPosition = addFile.mapToItem(testCase, 0, 0)
        const convertPosition = convert.mapToItem(testCase, 0, 0)
        verify(addPosition.x >= 0 && addPosition.y >= 0)
        verify(addPosition.x + addFile.width <= testCase.width && addPosition.y + addFile.height <= testCase.height)
        verify(convertPosition.x >= 0 && convertPosition.y >= 0)
        verify(convertPosition.x + convert.width <= testCase.width && convertPosition.y + convert.height <= testCase.height)
        FormatConverter.addUrls([testAudioUrl])
        tryVerify(function() { return !FormatConverter.busy }, 5000)
        tryCompare(FormatConverter, "fileCount", 1, 3000)
        tryVerify(function() {
            return table.width > 0 && table.height > 0 && table.contentWidth > table.width
        }, 3000)
        const startContentX = table.contentX
        table.contentX = table.contentWidth - table.width
        tryVerify(function() { return table.contentX > startContentX }, 1000)
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

    function test_mp3UiDefaultsReachPreflight() {
        FormatConverter.addUrls([testAudioUrl])
        tryVerify(function() { return !FormatConverter.busy }, 5000)
        FormatConverter.selectedFormat = "mp3"
        const settingsPanel = findChild(page, "formatSettingsPanel")
        const preflightDialog = findChild(page, "formatPreflightDialog")
        verify(settingsPanel && preflightDialog)
        tryCompare(settingsPanel, "bitRate", 320000, 1000)
        compare(settingsPanel.quality, 0)

        page.requestPlan()
        tryVerify(function() { return preflightDialog.visible }, 1000)
        compare(FormatConverter.pendingPlan.taskCount, 1)
        preflightDialog.close()
        FormatConverter.rejectPendingPlan()
    }

    function test_referenceWidthShowsCompleteProgressAndFileBadge() {
        FormatConverter.addUrls([testAudioUrl])
        tryVerify(function() { return !FormatConverter.busy }, 5000)
        tryCompare(FormatConverter, "fileCount", 1, 3000)

        const table = findChild(page, "formatTaskTableView")
        verify(table)
        tryVerify(function() { return table.contentWidth > 0 }, 3000)
        verify(table.contentWidth <= table.width)
        const percent = findChild(table, "formatTaskFirstProgressPercent")
        const badge = findChild(table, "formatTaskFirstFileIconBadge")
        const icon = findChild(table, "formatTaskFirstFileIcon")
        const rowCheck = findChild(table, "formatTaskFirstCheck")
        const rowIndicator = findChild(table, "formatTaskFirstCheckIndicator")
        const rowMark = findChild(table, "formatTaskFirstCheckMark")
        verify(percent && badge && icon && badge.visible && rowCheck && rowIndicator && rowMark)
        const percentPosition = percent.mapToItem(table, 0, 0)
        verify(percentPosition.x >= 0 && percentPosition.x + percent.width <= table.width)
        compare(Math.round(badge.radius), 6)
        verify(icon.source.toString().indexOf("file-music-fill") >= 0)
        verify(rowCheck.checked && rowMark.visible)
        compare(rowIndicator.color.toString(), "#1688ff")
    }
}
