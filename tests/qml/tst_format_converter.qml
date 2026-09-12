import QtQuick
import QtQuick.Window
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

    Component {
        id: navigationWindowComponent
        Window {
            width: testCase.width
            height: 59
            visible: true
            ToolSidebar { anchors.fill: parent }
        }
    }

    function test_start_button_disabled_text_follows_theme() {
        var previousTheme = SettingsController.themeMode
        var button = findChild(page, "convertAllButton")
        var label = findChild(page, "convertAllButtonLabel")
        var icon = findChild(page, "convertAllButtonIcon")
        verify(button && label && icon)
        try {
            for (var mode = 0; mode <= 1; ++mode) {
                SettingsController.themeMode = mode
                var expected = button.enabled ? Theme.accentText : Theme.textDisabled
                compare(label.color.toString(), expected.toString())
                compare(icon.tint.toString(), expected.toString())
            }
        } finally {
            SettingsController.themeMode = previousTheme
        }
    }

    Component {
        id: dialogBehaviorWindowComponent

        Window {
            id: dialogHost
            width: 880
            height: 560
            visible: true

            property alias preflightDialog: preflightDialog
            property alias errorDialog: errorDialog
            property alias converterStub: converterStub
            property int preflightAcceptedCount: 0
            property int preflightRejectedCount: 0
            property int errorRejectedCount: 0

            QtObject {
                id: converterStub
                property int confirmCount: 0
                property int rejectCount: 0
                property int copyCount: 0
                property var pendingPlan: ({
                    taskCount: 12,
                    requiresConfirmation: true
                })
                function confirmPendingPlan() { ++confirmCount }
                function rejectPendingPlan() { ++rejectCount }
                function copyText(text) { ++copyCount }
            }

            FormatPreflightDialog {
                id: preflightDialog
                parent: dialogHost.contentItem
                converter: converterStub
            }

            FormatErrorDialog {
                id: errorDialog
                parent: dialogHost.contentItem
                converter: converterStub
                summary: qsTr("2 个任务转换失败，请检查输出目录与编码参数。")
                detail: (qsTr("长错误详情") + "\n").repeat(40)
            }

            Connections {
                target: preflightDialog
                function onAccepted() { ++dialogHost.preflightAcceptedCount }
                function onRejected() { ++dialogHost.preflightRejectedCount }
            }

            Connections {
                target: errorDialog
                function onRejected() { ++dialogHost.errorRejectedCount }
            }
        }
    }

    function verifyAscendingX(parent, names) {
        let previousX = -1
        for (let index = 0; index < names.length; ++index) {
            const item = findChild(parent, names[index])
            verify(item, "missing " + names[index])
            const x = item.mapToItem(parent, 0, 0).x
            verify(x > previousX, names[index] + " must follow its predecessor")
            previousX = x
        }
    }

    function test_sharedTopNavigationHasFixedLeftInsetAndOrder() {
        var window = createTemporaryObject(navigationWindowComponent, testCase)
        verify(window)
        wait(0)
        var nav = findChild(window, "audioToolsTopNav")
        var first = findChild(nav, "audioToolNav_0")
        compare(first.mapToItem(nav, 0, 0).x, 12)
        verifyAscendingX(nav, ["audioToolNav_0", "audioToolNav_4", "audioToolNav_1",
                               "audioToolNav_2", "audioToolNav_3"])
    }

    function test_dialogButtonRolesAndScrollableDetail() {
        const host = createTemporaryObject(dialogBehaviorWindowComponent, testCase)
        verify(host)
        tryVerify(function() { return host.visible }, 1000)

        const confirmButton = findChild(host, "formatPreflightConfirmButton")
        const cancelButton = findChild(host, "formatPreflightCancelButton")
        const copyButton = findChild(host, "formatErrorCopyButton")
        const closeButton = findChild(host, "formatErrorCloseButton")
        const detail = findChild(host, "formatErrorDetailTextArea")
        verify(confirmButton && cancelButton && copyButton && closeButton && detail)
        verify(detail.contentHeight > 180,
               "long error details must overflow the fixed viewport and remain scrollable")

        host.preflightDialog.open()
        tryVerify(function() { return host.preflightDialog.visible }, 1000)
        mouseClick(confirmButton, confirmButton.width / 2, confirmButton.height / 2)
        tryVerify(function() { return !host.preflightDialog.visible }, 1000)
        compare(host.preflightAcceptedCount, 1)
        compare(host.converterStub.confirmCount, 1)
        compare(host.preflightRejectedCount, 0)
        compare(host.converterStub.rejectCount, 0)

        host.preflightDialog.open()
        tryVerify(function() { return host.preflightDialog.visible }, 1000)
        mouseClick(cancelButton, cancelButton.width / 2, cancelButton.height / 2)
        tryVerify(function() { return !host.preflightDialog.visible }, 1000)
        compare(host.preflightAcceptedCount, 1)
        compare(host.converterStub.confirmCount, 1)
        compare(host.preflightRejectedCount, 1)
        compare(host.converterStub.rejectCount, 1)

        host.errorDialog.open()
        tryVerify(function() { return host.errorDialog.visible }, 1000)
        mouseClick(copyButton, copyButton.width / 2, copyButton.height / 2)
        compare(host.converterStub.copyCount, 1)
        compare(host.errorRejectedCount, 0)
        verify(host.errorDialog.visible, "copying details must not close the dialog")
        mouseClick(closeButton, closeButton.width / 2, closeButton.height / 2)
        tryVerify(function() { return !host.errorDialog.visible }, 1000)
        compare(host.errorRejectedCount, 1)

        host.destroy()
    }

    function init() {
        testCase.width = 1672
        testCase.height = 941
        FormatConverter.rejectPendingPlan()
        FormatConverter.selectedFormat = "mp3"
        SettingsController.preserveMetadata = true
        SettingsController.defaultOutputDirectory = ""
        const settings = findChild(page, "formatSettingsPanel")
        if (settings)
            settings.expanded = true
        wait(0)
        if (!FormatConverter.busy)
            FormatConverter.clear()
        tryCompare(FormatConverter, "fileCount", 0, 3000)
    }

    function cleanup() {
        nativeDropHelper.unlockFiles()
    }

    function test_runtimeLayoutMatrix_data() {
        return [
            { tag: "minimum", w: 880, h: 560 },
            { tag: "compact-boundary", w: 1000, h: 720 },
            { tag: "desktop", w: 1280, h: 720 },
            { tag: "reference", w: 1672, h: 942 }
        ]
    }

    function test_runtimeLayoutMatrix(data) {
        testCase.width = data.w
        testCase.height = data.h
        wait(0)
        const toolbar = findChild(page, "formatToolbar")
        const tasks = findChild(page, "formatTaskPanel")
        const settings = findChild(page, "formatSettingsPanel")
        const bottom = findChild(page, "formatBottomBar")
        verify(toolbar && tasks && settings && bottom)
        for (const item of [toolbar, tasks, settings, bottom]) {
            const position = item.mapToItem(page, 0, 0)
            verify(position.x >= 0 && position.y >= 0)
            verify(position.x + item.width <= page.width + 0.5)
            verify(position.y + item.height <= page.height + 0.5)
        }
        verify(tasks.width > 0 && tasks.height > 0)
        compare(page.compactLayout, data.w < 1500)
        if (page.compactLayout)
            verify(settings.width <= 40.5)

        const bottomItems = ["converterParallelJobsGroup", "formatSummaryCard",
                             "convertAllButton", "cancelAllButton"]
        for (const name of bottomItems) {
            const item = findChild(bottom, name)
            verify(item, "missing " + name)
            const position = item.mapToItem(bottom, 0, 0)
            verify(position.x >= -0.5, name + " starts outside the footer")
            verify(position.x + item.width <= bottom.width + 0.5,
                   name + " overflows the footer")
        }

        for (const name of ["formatAddFileButton", "formatToolbarButton-folder",
                            "formatToolbarButton-playlist", "formatToolbarButton-remove",
                            "formatToolbarButton-clear"]) {
            const action = findChild(toolbar, name)
            verify(action && action.visible, "missing toolbar action " + name)
            const position = action.mapToItem(toolbar, 0, 0)
            verify(position.x >= -0.5 && position.x + action.width <= toolbar.width + 0.5,
                   name + " must remain reachable")
        }
    }

    function test_referenceGeometryAndControls() {
        const toolbar = findChild(page, "formatToolbar")
        const taskPanel = findChild(page, "formatTaskPanel")
        const settingsPanel = findChild(page, "formatSettingsPanel")
        const bottomBar = findChild(page, "formatBottomBar")
        verify(toolbar && taskPanel && settingsPanel && bottomBar, "reference panels")
        compare(testCase.height, 941)
        compare(Math.round(toolbar.height),
                Theme.settingsRowHeight + Theme.spacingSm)
        compare(Math.round(bottomBar.height),
                Theme.settingsRowHeight + Theme.spacingLg)
        tryVerify(function() {
            return Math.round(taskPanel.mapToItem(page, 0, 0).y) === 60
                   && Math.round(settingsPanel.mapToItem(page, 0, 0).y) === 60
                   && Math.round(bottomBar.mapToItem(page, 0, 0).y) === 877
        }, 1000, "reference vertical geometry")
        verify(settingsPanel.width >= 443 && settingsPanel.width <= 447,
               "settings width=" + settingsPanel.width)
        compare(Math.round(settingsPanel.x - (taskPanel.x + taskPanel.width)), 8)
        let filenameHeaderCell = null
        tryVerify(function() {
            filenameHeaderCell = findChild(page, "formatHeaderCell-1")
            return filenameHeaderCell && filenameHeaderCell.width > 0
        }, 1000)
        const filenameHeaderPosition = filenameHeaderCell.mapToItem(page, 30, 0)
        verify(Math.round(filenameHeaderPosition.x) >= 92
               && Math.round(filenameHeaderPosition.x) <= 96,
               "filename header x=" + filenameHeaderPosition.x)
        verify(!findChild(page, "formatSearchField"))
        verify(!findChild(page, "formatFilterButton"))
        verify(findChild(page, "formatSelectAllCheck"))
        const metadataCheck = findChild(page, "keepMetadataCheck")
        const extractAudioCheck = findChild(page, "extractAudioCheck")
        const metadataIndicator = findChild(page, "keepMetadataCheckIndicator")
        const metadataMark = findChild(page, "keepMetadataCheckMark")
        const extractIndicator = findChild(page, "extractAudioCheckIndicator")
        verify(metadataCheck && extractAudioCheck && metadataIndicator && metadataMark && extractIndicator,
               "reference check controls")
        compare(Math.round(metadataIndicator.width), 20)
        compare(Math.round(metadataIndicator.radius), 3)
        compare(metadataIndicator.color.toString(), Theme.accent.toString())
        verify(metadataMark.visible, "metadata mark visible")
        verify(metadataMark.source.toString().indexOf("check-line") >= 0, "metadata mark source")
        compare(extractIndicator.color.toString(), Theme.accent.toString())
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
               && encoderBox && encoderChevron && outputDirectoryRow,
               "reference settings controls")
        tryVerify(function() {
            return outputFormatGrid.width > 0 && encoderBox.width > 0
                   && outputDirectoryRow.width > 0
        }, 1000)
        const formatGridPosition = outputFormatGrid.mapToItem(settingsPanel, 0, 0)
        const firstFormatPosition = firstFormatButton.mapToItem(settingsPanel, 0, 0)
        const fourthFormatPosition = fourthFormatButton.mapToItem(settingsPanel, 0, 0)
        const encoderPosition = encoderBox.mapToItem(settingsPanel, 0, 0)
        const outputDirectoryPosition = outputDirectoryRow.mapToItem(settingsPanel, 0, 0)
        verify(Math.round(formatGridPosition.x) === 16, "format grid x=" + formatGridPosition.x)
        verify(Math.round(outputFormatGrid.width) >= 383 && Math.round(outputFormatGrid.width) <= 385,
               "format grid width=" + outputFormatGrid.width)
        verify(Math.round(firstFormatButton.width) >= 89 && Math.round(firstFormatButton.width) <= 91,
               "format button width=" + firstFormatButton.width)
        verify(Math.round(fourthFormatPosition.x - firstFormatPosition.x) === 294,
               "format fourth delta=" + (fourthFormatPosition.x - firstFormatPosition.x))
        verify(Math.round(encoderPosition.x) === 149, "encoder x=" + encoderPosition.x)
        verify(Math.abs(outputDirectoryPosition.x - encoderPosition.x) <= 2.5,
               "output directory x=" + outputDirectoryPosition.x
               + ", encoder x=" + encoderPosition.x)
        verify(encoderChevron.visible, "encoder chevron visible")
        verify(encoderChevron.source.toString().indexOf("arrow-down-s-line") >= 0,
               "encoder chevron source")
        const cancelAll = findChild(page, "cancelAllButton")
        const cancelIcon = findChild(page, "cancelAllButtonStopIcon")
        verify(cancelAll && cancelIcon, "cancel button and icon")
        verify(cancelIcon.source.toString().indexOf("checkbox-blank-fill") >= 0,
               "cancel icon source")
        const localProcessingHint = findChild(page, "formatLocalProcessingHint")
        verify(localProcessingHint.visible, "local processing hint visible")
        verify(findChild(page, "formatSettingsAdvancedToggle"))
        verify(findChild(page, "formatTaskContextMenu"))
        verify(findChild(bottomBar, "converterParallelJobsBox"))
        verify(!findChild(bottomBar, "formatOutputDirectoryRow"))
        const formatBox = findChild(page, "converterOutputFormatBox")
        const toolbarIcon = findChild(page, "formatToolbarIcon-file")
        verify(formatBox, "hidden format box")
        verify(toolbarIcon, "toolbar icon")
        compare(formatBox.count, 8)
        compare(toolbarIcon.tint.toString(), Theme.iconPrimary.toString())
    }

    function test_outputCatalogUsesAiffInsteadOfM4a() {
        const aiffButton = findChild(page, "formatOutputFormatButton-aiff")
        const m4aButton = findChild(page, "formatOutputFormatButton-m4a")
        verify(aiffButton, "AIFF must be one of the eight conversion outputs")
        verify(!m4aButton, "M4A remains an input container, not an output format")
        compare(aiffButton.text, "AIFF")
    }

    function test_recommendedCapabilityMatrixIsExposed() {
        const capabilities = FormatConverter.supportedOutputFormats
        verify(capabilities.length === 8)
        const expectedDefaults = {mp3: 320000, aac: 256000, opus: 320000}
        for (let index = 0; index < capabilities.length; ++index) {
            const capability = capabilities[index]
            verify(String(capability.outputExtension || "").length > 0)
            verify(capability.sampleRateChoices !== undefined)
            verify(capability.bitRateChoices !== undefined)
            if (expectedDefaults[capability.key] !== undefined)
                compare(capability.defaultBitRate,
                        expectedDefaults[capability.key])
        }
    }

    function test_realShellBodyShowsCompleteLocalProcessingHint() {
        testCase.height = 833
        wait(0)
        const settingsPanel = findChild(page, "formatSettingsPanel")
        const hint = findChild(page, "formatLocalProcessingHint")
        const advanced = findChild(page, "formatAdvancedSettings")
        verify(settingsPanel && hint && advanced, "local processing controls")
        tryVerify(function() {
            return settingsPanel.height > 0 && hint.visible && hint.height > 0
        }, 1000, "hint visible within settings panel")
        const hintPosition = hint.mapToItem(settingsPanel, 0, 0)
        verify(hintPosition.y >= 44, "hint top=" + hintPosition.y)
        verify(hintPosition.y + hint.height <= settingsPanel.height,
               "hint bottom=" + (hintPosition.y + hint.height) + ", panel=" + settingsPanel.height)
        verify(hintPosition.y >= 520 && hintPosition.y <= 552,
               "compact hint y=" + hintPosition.y)
        const advancedPosition = advanced.mapToItem(settingsPanel, 0, 0)
        verify(advancedPosition.y >= hintPosition.y + hint.height,
               "advanced y=" + advancedPosition.y + ", hint bottom="
               + (hintPosition.y + hint.height))
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
        SettingsController.resetToDefaults()
        tryCompare(FormatConverter, "parallelJobs", 5, 1000)
        compare(parallelBox.currentValue, 5)
        SettingsController.parallelJobs = 10
        tryCompare(FormatConverter, "parallelJobs", 10, 1000)
        compare(parallelBox.currentValue, 10)
    }

    function test_parallelJobsControlMatchesSchedulerContract() {
        const bottomBar = findChild(page, "formatBottomBar")
        const parallelBox = findChild(page, "converterParallelJobsBox")
        const parallelFrame = findChild(page, "converterParallelJobsBoxFrame")
        const totalProgress = findChild(page, "formatTotalProgress")
        const summary = findChild(page, "formatSummaryCard")
        verify(bottomBar && parallelBox && totalProgress && summary)
        compare(parallelBox.count, 10)
        SettingsController.resetToDefaults()
        compare(parallelBox.currentValue, 5)
        verify(parallelFrame)
        verify(parallelFrame.border.width > 0)
        const boxPosition = parallelBox.mapToItem(bottomBar, 0, 0)
        const progressPosition = totalProgress.mapToItem(bottomBar, 0, 0)
        const summaryPosition = summary.mapToItem(bottomBar, 0, 0)
        verify(boxPosition.x > progressPosition.x + totalProgress.width,
               "concurrency belongs in the right-side action group")
        verify(boxPosition.x < summaryPosition.x)

        FormatConverter.addUrls([testAudioUrl])
        compare(FormatConverter.busy, true)
        compare(parallelBox.enabled, false)
        tryVerify(function() { return !FormatConverter.busy }, 5000)
    }

    function test_rowContextMenuRemovesExactlyOneTask() {
        const secondUrl = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(secondUrl.toString().length > 0)
        FormatConverter.addUrls([testAudioUrl, secondUrl])
        tryVerify(function() { return !FormatConverter.busy }, 5000,
                  "context-menu fixtures did not finish importing")
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
        }, 3000, "first table-row delegate did not become interactive")
        // Let TableView finish polishing its delegate before sending a full
        // right-button gesture to that live row, rather than to the flickable.
        wait(100)
        mousePress(firstCell, firstCell.width / 2, firstCell.height / 2, Qt.RightButton)
        wait(20)
        mouseRelease(firstCell, firstCell.width / 2, firstCell.height / 2, Qt.RightButton)
        tryVerify(function() { return menu.visible }, 2000,
                  "right click did not open the row context menu")
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
        verify(taskPanel && settingsPanel && addFile && convert && table)
        tryCompare(settingsPanel, "width", 40, 1000)
        verify(addFile.visible && convert.visible)
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
        wait(0)
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

    function test_summaryCardsFilterCompletedAndFailedTasks() {
        const completeFilter = findChild(page, "formatCompletedSummaryFilter")
        const failedFilter = findChild(page, "formatFailedSummaryFilter")
        verify(completeFilter && failedFilter)
        mouseClick(completeFilter, completeFilter.width / 2,
                   completeFilter.height / 2, Qt.LeftButton)
        compare(FormatConverter.filteredTaskModel.statusFilter, "Done")
        mouseClick(completeFilter, completeFilter.width / 2,
                   completeFilter.height / 2, Qt.LeftButton)
        compare(FormatConverter.filteredTaskModel.statusFilter, "All")
        mouseClick(failedFilter, failedFilter.width / 2,
                   failedFilter.height / 2, Qt.LeftButton)
        compare(FormatConverter.filteredTaskModel.statusFilter, "Error")
    }

    function test_formatButtonsRebuildAndResetCapabilityParameters() {
        const settings = findChild(page, "formatSettingsPanel")
        const modeRow = findChild(page, "formatBitrateModeRow")
        const bitrateRow = findChild(page, "formatBitrateRow")
        const bitrateBox = findChild(page, "formatBitrateBox")
        const sampleRateBox = findChild(page, "formatSampleRateBox")
        const sampleFormatBox = findChild(page, "formatSampleFormatBox")
        const channelBox = findChild(page, "formatChannelBox")
        verify(settings && modeRow && bitrateRow && bitrateBox
               && sampleRateBox && sampleFormatBox && channelBox)

        const expectedKeys = ["mp3", "flac", "wav", "aac",
                              "opus", "ogg", "alac", "aiff"]
        const capabilities = FormatConverter.outputCapabilities
        compare(capabilities.length, expectedKeys.length)
        for (let index = 0; index < capabilities.length; ++index) {
            const capability = capabilities[index]
            verify(expectedKeys.indexOf(capability.key) >= 0)
            const button = findChild(page,
                                     "formatOutputFormatButton-" + capability.key)
            verify(button)
            compare(button.enabled, capability.available)
            if (!capability.available) {
                verify(String(capability.reason || "").length > 0)
                continue
            }

            mouseClick(button, button.width / 2, button.height / 2,
                       Qt.LeftButton)
            tryCompare(FormatConverter, "selectedFormat", capability.key, 1000)
            wait(0)

            compare(modeRow.visible, capability.bitrateModes.length > 0)
            compare(bitrateRow.visible, capability.parameterKind === "bitrate")
            if (capability.bitrateModes.length > 0) {
                const supportedModes = capability.bitrateModes.map(
                            function(mode) { return mode.key })
                verify(supportedModes.indexOf(settings.bitrateMode) >= 0)
            } else {
                compare(settings.bitrateMode, "")
            }
            if (capability.parameterKind === "bitrate") {
                const supportedBitRates = capability.bitRateChoices !== undefined
                        ? capability.bitRateChoices : capability.bitRates
                verify(supportedBitRates.indexOf(settings.bitRate) >= 0)
                compare(bitrateBox.count, supportedBitRates.length)
            } else {
                compare(settings.bitRate, 0)
            }
            if (capability.key === "opus")
                compare(settings.sampleRate, 48000)
            else
                verify(settings.sampleRate === 0
                       || capability.sampleRateChoices.indexOf(
                           settings.sampleRate) >= 0)
            verify(settings.sampleFormat === ""
                   || capability.sampleFormats.indexOf(settings.sampleFormat) >= 0)
            verify(settings.channelLayout === ""
                   || capability.channelLayouts.indexOf(settings.channelLayout) >= 0)
        }
    }

    function test_losslessDepthAndQualityControlsUseCapabilityDefaults() {
        const bitDepthBox = findChild(page, "formatBitDepthBox")
        const qualityBox = findChild(page, "formatQualityBox")
        const settings = findChild(page, "formatSettingsPanel")
        verify(bitDepthBox && qualityBox && settings)
        const originalBitrate = SettingsController.transcodeBitrateKbps
        SettingsController.transcodeBitrateKbps = 320

        FormatConverter.selectedFormat = "flac"
        wait(0)
        const flac = FormatConverter.currentCapability
        compare(flac.parameterKind, "compression")
        compare(flac.defaultQuality, 5)
        compare((flac.bitDepths || []).length, 3)
        tryCompare(qualityBox, "currentValue", 5, 1000)
        compare(bitDepthBox.currentValue, "")
        compare(settings.bitDepth, "")
        compare(settings.sampleFormat, "")

        FormatConverter.selectedFormat = "ogg"
        wait(0)
        const ogg = FormatConverter.currentCapability
        compare(ogg.parameterKind, "quality")
        compare(ogg.defaultQuality, 8)
        tryCompare(qualityBox, "currentValue", 8, 1000)
        compare(settings.bitRate, 0)

        FormatConverter.selectedFormat = "opus"
        wait(0)
        tryCompare(findChild(page, "formatBitrateBox"), "currentValue", 320000, 1000)
        SettingsController.transcodeBitrateKbps = originalBitrate
    }

    function test_realRuntimeFailureShowsUnderlyingErrorInStatusRow() {
        const input = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(input.toString().length > 0)
        FormatConverter.addUrls([input])
        tryVerify(function() { return !FormatConverter.busy }, 5000,
                  "locked-file fixture did not finish importing")
        tryCompare(FormatConverter, "fileCount", 1, 3000)

        const formatButton = findChild(page, "formatOutputFormatButton-mp3")
        const convert = findChild(page, "convertAllButton")
        const preflight = findChild(page, "formatPreflightDialog")
        verify(formatButton && convert && preflight)
        mouseClick(formatButton, formatButton.width / 2,
                   formatButton.height / 2, Qt.LeftButton)
        mouseClick(convert, convert.width / 2, convert.height / 2,
                   Qt.LeftButton)
        const errorDialog = findChild(page, "formatErrorDialog")
        tryVerify(function() { return preflight.visible || errorDialog.visible }, 3000,
                  "runtime-failure conversion opened no result dialog")
        verify(preflight.visible,
               "runtime-failure preflight rejected settings: " + errorDialog.summary)
        verify(nativeDropHelper.lockFileExclusive(input))
        preflight.accept()
        tryVerify(function() {
            return !FormatConverter.busy && FormatConverter.failedCount === 1
        }, 30000, "locked output did not finish as one failed conversion")
        nativeDropHelper.unlockFiles()

        const row = FormatConverter.files[0]
        compare(row.status, "Error")
        const rawError = String(row.errorMessage || "")
        verify(rawError.startsWith("转换失败："))
        verify(rawError.length > "转换失败：".length)
        let statusText = null
        tryVerify(function() {
            statusText = findChild(page, "formatTaskFirstStatusText")
            return statusText !== null && statusText.visible
                   && statusText.text.indexOf(rawError) >= 0
        }, 3000, "failed row did not display the underlying error")
        verify(statusText.text.indexOf(rawError) >= 0)
    }

    function test_mp3UiDefaultsReachPreflight() {
        const originalBitrate = SettingsController.transcodeBitrateKbps
        SettingsController.transcodeBitrateKbps = 320
        FormatConverter.addUrls([testAudioUrl])
        tryVerify(function() { return !FormatConverter.busy }, 5000,
                  "MP3 fixture import did not finish")
        FormatConverter.selectedFormat = "mp3"
        const settingsPanel = findChild(page, "formatSettingsPanel")
        const preflightDialog = findChild(page, "formatPreflightDialog")
        verify(settingsPanel && preflightDialog)
        tryCompare(settingsPanel, "bitRate", 320000, 1000)
        compare(settingsPanel.quality, 75)

        page.requestPlan()
        const errorDialog = findChild(page, "formatErrorDialog")
        tryVerify(function() { return preflightDialog.visible || errorDialog.visible }, 1000,
                  "MP3 defaults produced no preflight result dialog")
        verify(preflightDialog.visible,
               "MP3 defaults were rejected: " + errorDialog.summary)
        compare(FormatConverter.pendingPlan.taskCount, 1)
        preflightDialog.close()
        FormatConverter.rejectPendingPlan()
        SettingsController.transcodeBitrateKbps = originalBitrate
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
        compare(rowIndicator.color.toString(), Theme.accent.toString())
    }
}
