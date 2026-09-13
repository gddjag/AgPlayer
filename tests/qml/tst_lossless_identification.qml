import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "LosslessIdentificationWorkbench"
    when: windowShown
    visible: true
    width: 1672
    height: 776

    ListModel {
        id: taskModel
        ListElement {
            taskId: "fixture-1"
            fileName: "真实测试音频.flac"
            filePath: "D:/fixture/真实测试音频.flac"
            formatName: "FLAC"
            audioFormat: "96 kHz · 24-bit"
            verdictCode: "suspected_upsample"
            verdictText: "疑似升频"
            confidence: 87
            state: "Completed"
            stateText: "已完成"
            checked: true
            progress: 1.0
        }
    }

    QtObject {
        id: mockController
        property var tasks: taskModel
        property var selectedResult: ({})
        property bool running: false
        property bool stopping: false
        property real progress: 0.0
        property int completedCount: 0
        property int totalCount: taskModel.count
        property int selectedCount: 1
        property int concurrency: 2
        property string filter: "all"
        property string searchText: ""
        property string statusText: ""
        property string error: ""
        property var counts: [1, 0, 0, 1, 0]
        property int startCalls: 0
        property int cancelCalls: 0
        property int checkedCalls: 0
        property int reportCalls: 0
        property int spectrogramCalls: 0

        property var receivedFiles: []
        function loadFiles(urls) { receivedFiles = urls }
        function addFolder(url) {}
        function start() { startCalls += 1 }
        function cancel() { cancelCalls += 1 }
        function retrySelected() {}
        function removeSelected() {}
        function clear() {}
        function selectTask(id) {
            selectedResult = {
                taskId: id,
                fileName: "真实测试音频.flac",
                path: "D:/fixture/真实测试音频.flac",
                formatName: "FLAC",
                codec: "FLAC",
                sampleRate: 96000,
                bitsPerSample: 24,
                channels: 2,
                durationMs: 367000,
                fileSize: 61551411,
                modified: "2026-09-05 10:20:30",
                verdictCode: "suspected_upsample",
                verdictText: "疑似升频",
                confidence: 87,
                coverage: 1.0,
                cutoffHz: 22050,
                effectiveBits: 16.2,
                resamplingText: "44.1 kHz → 96 kHz",
                holesText: "未发现稳定编码空洞",
                evidence: [
                    { text: "高频能量在 22.05 kHz 附近稳定衰减", value: 22050, unit: "Hz", reference: "stable edge > 0.8", coverageStartSeconds: 0.5, coverageEndSeconds: 12.0 }
                ],
                candidates: [
                    { format: "44.1 kHz PCM", confidence: 78, limitation: "截止与镜像共同支持" }
                ],
                chain: ["44.1 kHz PCM（推测）", "96 kHz FLAC"],
                spectrum: [-14, -20, -26, -35, -48, -63, -78, -96],
                spectrogram: [[-18, -28, -46], [-20, -31, -52]],
                error: "",
                warnings: []
            }
        }
        function setChecked(id, checked) { checkedCalls += 1 }
        function selectAll(checked) {}
        function exportReport(url, format) { reportCalls += 1 }
        function requestSpectrogram() { spectrogramCalls += 1 }
    }

    LosslessIdentifyPage {
        id: page
        anchors.fill: parent
        controller: mockController
    }

    Component {
        id: toolsWindowComponent
        AudioToolsWindow {
            width: 1672
            height: 941
            visible: true
        }
    }

    SignalSpy {
        id: playlistSpy
        target: page
        signalName: "addPlaylistRequested"
    }

    SignalSpy {
        id: locateSpy
        target: page
        signalName: "locateRequested"
    }

    function init() {
        if (taskModel.count === 0) {
            taskModel.append({
                taskId: "fixture-1", fileName: "真实测试音频.flac",
                filePath: "D:/fixture/真实测试音频.flac", formatName: "FLAC",
                audioFormat: "96 kHz · 24-bit", verdictCode: "suspected_upsample",
                verdictText: "疑似升频", confidence: 87, state: "Completed",
                stateText: "已完成", checked: true, progress: 1.0
            })
        }
        testCase.width = 1672
        testCase.height = 776
        mockController.selectedResult = ({})
        mockController.running = false
        mockController.stopping = false
        mockController.progress = 0
        mockController.completedCount = 0
        mockController.totalCount = taskModel.count
        mockController.selectedCount = 1
        mockController.startCalls = 0
        mockController.cancelCalls = 0
        mockController.spectrogramCalls = 0
        mockController.filter = "all"
        mockController.searchText = ""
        playlistSpy.clear()
        locateSpy.clear()
    }

    function test_selectedFileSnapshotSurvivesSourceMutation() {
        const selected = [testAudioUrl]
        page.importSelectedFiles(selected)
        selected.length = 0
        wait(0)
        compare(mockController.receivedFiles.length, 1)
        verify(Array.isArray(mockController.receivedFiles))
        compare(mockController.receivedFiles[0], String(testAudioUrl))
    }

    function test_emptyStateAndRequiredRegions() {
        mockController.totalCount = 0
        mockController.selectedCount = 0
        taskModel.clear()
        wait(0)
        verify(findChild(page, "losslessToolbar"))
        const notice = findChild(page, "losslessExperimentalNotice")
        verify(notice && notice.visible)
        verify(notice.text.indexOf("实验性") >= 0
               || notice.text.indexOf("Experimental") >= 0)
        verify(notice.width <= page.width)
        verify(findChild(page, "losslessTaskPanel"))
        verify(findChild(page, "losslessEvidencePanel"))
        verify(findChild(page, "losslessConclusionPanel"))
        verify(findChild(page, "losslessBottomBar"))
        compare(page.color, Theme.background)
        verify(!findChild(page, "losslessSearchToggle"))
        verify(!findChild(page, "losslessRetryButton"))
        verify(findChild(page, "losslessRemoveButton"))
        verify(findChild(page, "losslessClearButton"))
        // ListView applies model removals during its next polish pass.
        tryCompare(findChild(page, "losslessEmptyState"), "visible", true)
        verify(!findChild(page, "losslessStartButton").enabled)
        taskModel.append({
            taskId: "fixture-1", fileName: "真实测试音频.flac",
            filePath: "D:/fixture/真实测试音频.flac", formatName: "FLAC",
            audioFormat: "96 kHz · 24-bit", verdictCode: "suspected_upsample",
            verdictText: "疑似升频", confidence: 87, state: "Completed",
            stateText: "已完成", checked: true, progress: 1.0
        })
        mockController.totalCount = 1
    }

    function test_taskActionsAndFiltersStayInsideAtDefaultWidth() {
        const oldWidth = testCase.width
        testCase.width = 1386
        wait(50)
        const panel = findChild(page, "losslessTaskPanel")
        for (const name of ["losslessRemoveButton", "losslessClearButton",
                            "losslessFilter_all", "losslessFilter_inconclusive"]) {
            const item = findChild(page, name)
            verify(item)
            const point = item.mapToItem(panel, 0, 0)
            verify(point.x >= 0 && point.x + item.width <= panel.width + 1,
                   name + " must stay inside the task panel")
        }
        testCase.width = oldWidth
    }

    function test_referenceWidthUsesMeasuredThreeColumnRatio() {
        const left = findChild(page, "losslessTaskPanel")
        const middle = findChild(page, "losslessEvidencePanel")
        const right = findChild(page, "losslessConclusionPanel")
        verify(left && middle && right)
        fuzzyCompare(left.width, 622, 1.5)
        fuzzyCompare(middle.width, 600, 1.5)
        fuzzyCompare(right.width, 416, 1.5)
        const total = left.width + middle.width + right.width
        fuzzyCompare(left.width / total, 622 / 1638, 0.012)
        fuzzyCompare(middle.width / total, 600 / 1638, 0.012)
        fuzzyCompare(right.width / total, 416 / 1638, 0.012)
        verify(right.x + right.width <= right.parent.width + 0.5)
    }

    function test_toolNavigationKeepsGeometryWhenSwitchingTools() {
        AudioToolsController.selectTool(5)
        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        wait(0)
        const titleBar = findChild(toolsWindow.contentItem, "audioToolsTitleBar")
        const titleText = findChild(toolsWindow.contentItem,
                                    "audioToolsWindowTitle")
        const logo = findChild(toolsWindow.contentItem, "audioToolsLogo")
        const minimize = findChild(toolsWindow.contentItem,
                                   "audioToolsMinimizeButton")
        const maximize = findChild(toolsWindow.contentItem,
                                   "audioToolsMaximizeButton")
        const close = findChild(toolsWindow.contentItem,
                                "audioToolsCloseButton")
        const navigation = findChild(toolsWindow.contentItem, "audioToolsTopNav")
        const firstTab = findChild(toolsWindow.contentItem, "audioToolNav_0")
        verify(titleBar && titleText && logo && minimize && maximize && close
               && navigation && firstTab)
        compare(titleBar.height, Theme.titleBarHeight)
        compare(titleBar.color.toString(), Theme.titleBarSurface.toString())
        compare(titleText.font.pixelSize, Theme.fontSizeSection)
        fuzzyCompare(logo.mapToItem(titleBar, 0, 0).x,
                     Theme.spacingXl, 0.5)
        for (const control of [minimize, maximize, close]) {
            compare(control.width, Theme.navigationActionExtent)
            compare(control.height, Theme.navigationActionExtent)
            fuzzyCompare(control.y,
                         (titleBar.height - Theme.navigationActionExtent) / 2,
                         0.5)
        }
        for (const child of titleBar.children) {
            if (child !== titleBar && child.visible
                    && child.color !== undefined
                    && Math.abs(child.width - titleBar.width) <= 0.5
                    && Math.abs(child.height - titleBar.height) <= 0.5) {
                compare(child.color.toString(),
                        Theme.titleBarSurface.toString(),
                        "lossless mode must not cover the shared title surface")
            }
        }
        compare(navigation.height, Theme.settingsRowHeight)
        const initialWidth = firstTab.width
        const initialFont = firstTab.labelPixelSize
        const losslessTab = findChild(navigation, "audioToolNav_5")
        verify(losslessTab)
        verify(losslessTab.iconSource.toString() !== firstTab.iconSource.toString())
        for (let tool of [0, 4, 1, 2, 3, 5]) {
            AudioToolsController.selectTool(tool)
            wait(0)
            compare(navigation.height, Theme.settingsRowHeight)
            fuzzyCompare(firstTab.width, initialWidth, 0.5)
            compare(firstTab.labelPixelSize, initialFont)
            compare(firstTab.underlineSelection, false)
            compare(findChild(navigation, "audioToolNav_" + tool).selected, true)
        }

        AudioToolsController.selectTool(0)
        wait(0)
        compare(titleBar.height, Theme.titleBarHeight)
        compare(navigation.height, Theme.settingsRowHeight)
    }

    function test_selectionPublishesRealDetailsAndEvidence() {
        const firstRow = findChild(page, "losslessTaskRow_fixture-1")
        verify(firstRow)
        mouseClick(firstRow, firstRow.width / 2, firstRow.height / 2)
        tryVerify(function() {
            return mockController.selectedResult.taskId === "fixture-1"
        })
        compare(findChild(page, "losslessVerdictText").text, "疑似升频")
        compare(findChild(page, "losslessConfidenceValue").text, "87分")
        verify(findChild(page, "losslessEvidenceList").count > 0)
        verify(findChild(page, "losslessSpectrumChart").spectrum.length > 0)
        const disclaimer = findChild(page, "losslessDisclaimer")
        const actions = findChild(page, "losslessConclusionActions")
        verify(disclaimer.visible)
        verify(disclaimer.y + disclaimer.height <= actions.y + 0.5)
    }

    function test_conversionChainKeepsFullLabelsAndEvidenceIsKeyboardInspectable() {
        mockController.selectTask("fixture-1")
        wait(0)
        const chain = findChild(page, "losslessConversionChain")
        const first = findChild(page, "losslessChainNode_0")
        const second = findChild(page, "losslessChainNode_1")
        verify(chain && first && second)
        compare(first.width, second.width)
        verify(first.height >= 72)
        verify(second.mapToItem(chain, second.width, 0).x <= chain.width + 0.5)
        const evidence = findChild(page, "losslessEvidenceRow_0")
        verify(evidence, "The evidence delegate must be created")
        const tip = evidence.detailPopup
        verify(tip, "The delegate must own its inspectable detail popup")
        evidence.forceActiveFocus(Qt.TabFocusReason)
        tryVerify(function() { return tip.visible })
        verify(tip.text.indexOf("stable edge > 0.8") >= 0)
        verify(tip.text.indexOf("12.00") >= 0)
        keyClick(Qt.Key_Tab)
        tryVerify(function() { return !evidence.activeFocus })
    }

    function test_standardResultShowsAllSixFileDetailsWithoutScrolling() {
        // The design's 941px window reserves 50px title + 56px navigation
        // outside this page. The standalone default fixture is shorter.
        testCase.height = 941 - 50 - 56
        // Match the two measured evidence items shown by the real broadband
        // result, including the longer explanation which wraps onto two lines.
        mockController.selectTask("fixture-1")
        const result = Object.assign({}, mockController.selectedResult)
        result.verdictCode = "credible_lossless"
        result.verdictText = "可信无损"
        result.chain = ["wav"]
        result.candidates = []
        result.evidence = [
            { text: "截止仅描述跨时间频谱，不单独证明有损来源。", value: 48000, unit: "Hz" },
            { text: "统计来自解码器原始整数表示，未先降低为float32。", value: 0.996, unit: "ratio" }
        ]
        mockController.selectedResult = result
        wait(0)
        const info = findChild(page, "losslessFileInformation")
        const rows = findChild(page, "losslessFileInformationRows")
        const viewport = findChild(page, "losslessDetailsScroll")
        verify(info && rows && viewport)
        compare(rows.count, 6)
        compare(viewport.contentY, 0)
        const infoBottom = info.mapToItem(viewport, 0, info.height).y
        verify(infoBottom <= viewport.height + 0.5,
               "File information bottom " + infoBottom + " exceeds viewport " + viewport.height)
        verify(findChild(page, "losslessChainNode_0").width <= 112)
        const detail = findChild(page, "losslessConclusionPanel")
        const tinyEvidence = detail.evidenceText({text: "energy", value: 0.000004779, unit: "score"})
                                   .replace(/\u2060/g, "")
        verify(tinyEvidence.indexOf("4.78e-6") >= 0,
               "Measured nonzero pre-onset energy must not be displayed as zero")
        verify(detail.sizeText(4096).indexOf("KiB") >= 0)
        compare(detail.evidenceText({text: "insufficient evidence", value: 0, unit: ""}),
                "insufficient evidence")
        testCase.height = 776
    }

    function test_filterSearchAndBatchActionsUseControllerContract() {
        const upsampleFilter = findChild(page, "losslessFilter_upsample")
        verify(upsampleFilter)
        mouseClick(upsampleFilter)
        compare(mockController.filter, "upsample")

        const start = findChild(page, "losslessStartButton")
        verify(start.enabled)
        mouseClick(start)
        compare(mockController.startCalls, 1)

        mockController.running = true
        wait(0)
        verify(!start.enabled)
        const cancel = findChild(page, "losslessCancelButton")
        verify(cancel.enabled)
        mouseClick(cancel)
        compare(mockController.cancelCalls, 1)
    }

    function test_playlistAndPlayerLocationAreForwardedForIntegration() {
        mouseClick(findChild(page, "losslessAddPlaylistButton"))
        compare(playlistSpy.count, 1)

        mockController.selectTask("fixture-1")
        wait(0)
        const locate = findChild(page, "losslessLocateButton")
        verify(locate.enabled)
        mouseClick(locate)
        compare(locateSpy.count, 1)
        compare(locateSpy.signalArguments[0][0],
                "D:/fixture/真实测试音频.flac")
    }

    function test_minimumViewportRemainsUsableWithoutPanelOverlap() {
        testCase.width = 880
        testCase.height = 560
        wait(0)
        verify(page.compactLayout)
        const toolbar = findChild(page, "losslessToolbar")
        const content = findChild(page, "losslessContent")
        const bottom = findChild(page, "losslessBottomBar")
        verify(toolbar.width <= page.width)
        verify(content.width <= page.width)
        verify(bottom.width <= page.width)
        verify(findChild(page, "losslessCompactViewSwitch").visible)
        const conclusion = findChild(page, "losslessCompactConclusionButton")
        const evidence = findChild(page, "losslessCompactEvidenceButton")
        verify(conclusion && evidence)
        const conclusionRight = conclusion.mapToItem(page, conclusion.width, 0).x
        verify(conclusionRight <= page.width, "Conclusion tab must fit without scrolling")
        mouseClick(conclusion)
        compare(page.compactView, 1)
        verify(findChild(page, "losslessConclusionPanel").visible)
        mouseClick(evidence)
        compare(page.compactView, 0)
        testCase.width = 1672
        testCase.height = 776
    }

    function test_chartModeIsEventDrivenAndSpectrogramIsLazy() {
        mockController.selectTask("fixture-1")
        const chart = findChild(page, "losslessSpectrumChart")
        const spectrogram = findChild(page, "losslessSpectrogramChart")
        verify(chart && spectrogram)
        verify(chart.visible)
        verify(!spectrogram.visible)
        mouseClick(findChild(page, "losslessSpectrogramTab"))
        compare(mockController.spectrogramCalls, 1)
        verify(!chart.visible)
        verify(spectrogram.visible)
        compare(spectrogram.spectrogram.length, 2)
        mouseClick(findChild(page, "losslessSpectrumTab"))
    }
}
