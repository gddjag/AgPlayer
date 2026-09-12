import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MetadataEditorWorkflow"
    when: windowShown
    visible: true
    width: 1672
    height: 836

    MetadataEditPage {
        id: page
        anchors.fill: parent
    }

    function test_generic_metadata_notice_is_compact_and_not_a_write_failure() {
        var dialog = findChild(page, "metadataErrorDialog")
        verify(dialog)
        page.errorMessage = "未发现新的受支持音频文件。"
        dialog.open()
        try {
            tryCompare(dialog, "opened", true)
            compare(dialog.title, "元数据处理提示")
            verify(dialog.width <= 360)
            verify(dialog.height < 240)
            compare(dialog.contentItem.color.toString(), Theme.primaryText.toString())
        } finally { dialog.close(); page.errorMessage = "" }
    }

    Component {
        id: navigationWindowComponent
        Window {
            width: testCase.width
            height: 52
            visible: true
            ToolSidebar {
                anchors.fill: parent
                referenceWorkbench: true
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

    Component {
        id: compactPageComponent
        MetadataEditPage { width: 880; height: 560 }
    }

    Component {
        id: responsivePageComponent
        MetadataEditPage { height: 700 }
    }

    function init() {
        testCase.width = 1672
        testCase.height = 836
        if (!MetadataEditor.busy)
            MetadataEditor.clear()
    }

    function test_referenceDesktopGeometry() {
        wait(0)
        const files = findChild(page, "metadataFilePanel")
        const inspector = findChild(page, "metadataInspectorPanel")
        const actionBar = findChild(page, "metadataActionBar")
        const apply = findChild(page, "metadataApplyButton")
        const cancel = findChild(page, "metadataCancelButton")
        verify(files && inspector && actionBar && apply && cancel)
        verify(Math.abs(files.width - 902) <= 20, "files width=" + files.width)
        verify(Math.abs(inspector.width - 723) <= 20, "inspector width=" + inspector.width)
        verify(Math.abs(inspector.x - files.width - 8) <= 20,
               "panel gap=" + (inspector.x - files.width))
        verify(apply.width >= 190)
        verify(cancel.width >= 180)
        compare(Math.round(apply.height), Theme.controlHeightProminent)
        compare(Math.round(cancel.height), Theme.controlHeightProminent)
        verify(apply.x + apply.width <= actionBar.width)
        verify(cancel.x + cancel.width <= actionBar.width)
    }

    function test_referenceInspectorUsesDirectFieldLayout() {
        wait(0)
        const inspector = findChild(page, "metadataInspectorPanel")
        const form = findChild(page, "metadataReferenceFieldForm")
        const cover = findChild(page, "metadataCoverPreview")
        verify(inspector && form && cover)
        const formTop = form.mapToItem(inspector, 0, 0).y
        verify(formTop >= 72 && formTop <= 82,
               "form starts at reference field baseline: " + formTop)
        verify(cover.width === 212)
        verify(cover.height === 210)
    }

    function test_changePreviewClipsAndScrollsInternally() {
        const preview = findChild(page, "metadataChangePreview")
        const scroll = findChild(page, "metadataChangePreviewScroll")
        verify(preview && scroll)
        verify(preview.clip)
        verify(scroll.clip)
        verify(scroll.mapToItem(preview, 0, 0).y >= 0)
        verify(scroll.mapToItem(preview, scroll.width, scroll.height).x
               <= preview.width)
        verify(scroll.mapToItem(preview, scroll.width, scroll.height).y
               <= preview.height)
        for (let index = 0; index < page.fieldDefinitions.length; ++index) {
            const key = page.fieldDefinitions[index].key
            page.setFieldMode(key, "set")
            page.setFieldValue(key, "long-preview-value-" + index)
        }
        const overflow = Math.max(0, scroll.contentHeight
                                     - scroll.availableHeight)
        if (overflow > 0) {
            scroll.contentItem.contentY = overflow
            tryVerify(function() { return scroll.contentItem.contentY > 0 }, 1000)
        } else {
            compare(scroll.contentItem.contentY, 0)
        }
        page.resetEdits()
    }

    function test_runtimeLayoutMatrix_data() {
        return [
            { tag: "minimum", w: 880, h: 560 },
            { tag: "compact", w: 1000, h: 720 },
            { tag: "desktop", w: 1280, h: 720 },
            { tag: "reference", w: 1672, h: 942 }
        ]
    }

    function test_runtimeLayoutMatrix(data) {
        const candidate = createTemporaryObject(responsivePageComponent,
                                                 testCase,
                                                 { width: data.w, height: data.h })
        verify(candidate)
        wait(0)
        const toolbar = findChild(candidate, "metadataToolbar")
        const files = findChild(candidate, "metadataFilePanel")
        const inspector = findChild(candidate, "metadataInspectorPanel")
        const tabs = findChild(candidate, "metadataCompactTabs")
        verify(toolbar && files && inspector && tabs)
        const toolbarPosition = toolbar.mapToItem(candidate, 0, 0)
        verify(toolbarPosition.x >= 0 && toolbarPosition.y >= 0)
        verify(toolbarPosition.x + toolbar.width <= candidate.width)
        verify(toolbarPosition.y + toolbar.height <= candidate.height)
        if (candidate.compactLayout) {
            verify(tabs.visible && files.visible)
            tabs.currentIndex = 1
            tryVerify(function() { return inspector.visible })
        }
        const visiblePanel = candidate.compactLayout ? inspector : files
        tryVerify(function() {
            const panelPosition = visiblePanel.mapToItem(candidate, 0, 0)
            return panelPosition.x >= 0 && panelPosition.y >= 0
                    && panelPosition.x + visiblePanel.width <= candidate.width
                    && panelPosition.y + visiblePanel.height <= candidate.height
        })
    }

    function test_resultsSummaryIncludesUnsupportedCount() {
        const summary = findChild(page, "metadataResultsSummaryLabel")
        verify(summary)
        if (!summary)
            return
        verify(summary.text.indexOf("不支持") >= 0,
               "result summary must expose unsupported rows: " + summary.text)
    }

    function test_failureResultExplainsCauseStageCodeAndRecovery() {
        const details = page.resultDetailText({
            fileName: "locked.mp3",
            success: false,
            stage: "write",
            errorCode: 5,
            message: ""
        })
        verify(details.indexOf("文件正在被占用") >= 0, details)
        verify(details.indexOf("写入") >= 0, details)
        verify(details.indexOf("FileInUse") >= 0, details)
        verify(details.indexOf("关闭正在播放") >= 0, details)
    }

    function test_failureResultPreservesBackendSpecificReason() {
        const details = page.resultDetailText({
            fileName: "changed.mp3",
            success: false,
            stage: "write",
            errorCode: 19,
            message: "源文件在预检后发生变化，请重新预检"
        })
        verify(details.indexOf("源文件在预检后发生变化") >= 0, details)
        verify(details.indexOf("SourceChanged") >= 0, details)
        verify(details.indexOf("重新加载文件后再试") >= 0, details)
    }

    function test_compactLayoutKeepsBothWorkspacesReachable() {
        const compactPage = createTemporaryObject(compactPageComponent, testCase)
        verify(compactPage)
        wait(0)
        const files = findChild(compactPage, "metadataFilePanel")
        const inspector = findChild(compactPage, "metadataInspectorPanel")
        const tabs = findChild(compactPage, "metadataCompactTabs")
        const filesTab = findChild(compactPage, "metadataCompactFilesTab")
        const editorTab = findChild(compactPage, "metadataCompactEditorTab")
        verify(files && inspector && tabs && filesTab && editorTab)
        compare(tabs.currentIndex, 0)
        compare(tabs.height, 36)
        tryVerify(function() {
            return files.visible && files.width > 0 && files.height > 0
        })
        verify(!inspector.visible)

        mouseClick(editorTab, editorTab.width / 2, editorTab.height / 2)
        compare(tabs.currentIndex, 1)
        tryVerify(function() { return inspector.visible && inspector.width > 0 })
        tryVerify(function() {
            return inspector.mapToItem(compactPage, inspector.width, 0).x
                    <= compactPage.width
        })

        mouseClick(filesTab, filesTab.width / 2, filesTab.height / 2)
        compare(tabs.currentIndex, 0)
        tryVerify(function() { return files.visible && !inspector.visible })
    }

    function test_responsiveThresholdUsesBothPanelMinimumWidths() {
        const below = createTemporaryObject(responsivePageComponent, testCase,
                                            { width: 1205 })
        const boundary = createTemporaryObject(responsivePageComponent, testCase,
                                               { width: 1206 })
        verify(below && boundary)
        wait(0)
        verify(below.compactLayout)
        verify(!boundary.compactLayout)
        const files = findChild(boundary, "metadataFilePanel")
        const inspector = findChild(boundary, "metadataInspectorPanel")
        verify(files && inspector)
        verify(files.width >= 560)
        verify(inspector.width >= 620)
        verify(inspector.mapToItem(boundary, inspector.width, 0).x <= boundary.width)
    }

    function test_highDpiLogicalWidthsNeverClipTheInspector() {
        const logicalWidths = [1672, 1338, 1115, 836]
        for (let index = 0; index < logicalWidths.length; ++index) {
            const candidate = createTemporaryObject(responsivePageComponent,
                                                    testCase,
                                                    { width: logicalWidths[index] })
            verify(candidate)
            wait(0)
            const files = findChild(candidate, "metadataFilePanel")
            const inspector = findChild(candidate, "metadataInspectorPanel")
            const tabs = findChild(candidate, "metadataCompactTabs")
            verify(files && inspector && tabs)
            if (candidate.compactLayout) {
                verify(tabs.visible)
                tabs.currentIndex = 1
                tryVerify(function() { return inspector.visible })
            }
            verify(inspector.mapToItem(candidate, inspector.width, 0).x
                   <= candidate.width)
        }
    }

    function test_threeStateFieldsAreMutuallyExclusive() {
        MetadataEditor.loadFiles([testAudioUrl])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        compare(MetadataEditor.fileCount, 1)
        page.selectedIndices = [0]
        page.selectionAnchor = 0
        page.refreshFields()

        compare(page.fieldMode("title"), "keep")
        verify(findChild(page, "metadataModeButton_title_keep"))
        verify(findChild(page, "metadataModeButton_title_set"))
        verify(findChild(page, "metadataModeButton_title_clear"))
        // The reference editor exposes its value inputs directly: an empty
        // value means "keep original" until the user types a replacement.
        verify(findChild(page, "metadataValueField_title").enabled)

        page.setFieldMode("title", "set")
        page.setFieldValue("title", "New title")
        compare(page.fieldMode("title"), "set")
        compare(page.fieldPayload().title.value, "New title")
        verify(page.configuredEditCount() > 0)
        verify(page.targetCount() > 0)
        verify(findChild(page, "metadataValueField_title").enabled)

        page.setFieldMode("title", "clear")
        compare(page.fieldMode("title"), "clear")
        verify(!findChild(page, "metadataValueField_title").enabled)
    }

    function test_mixedValuesAndCoversAreVisibleWithoutChoosingFirstFile() {
        const titleRow = page.rowForField("title")
        const titleField = findChild(page, "metadataValueField_title")
        const clearButton = findChild(page, "metadataClearButton_title")
        const coverSummary = findChild(page, "metadataCoverSummaryLabel")
        verify(titleRow && titleField && clearButton && coverSummary)
        titleRow.reset({ value: "", multiple: true })
        compare(titleField.text, "")
        compare(titleField.placeholderText, "多种值")
        verify(clearButton.visible)
        mouseClick(clearButton)
        compare(titleRow.selectedMode, "clear")
        compare(titleField.placeholderText, "将清除")
        page.scopeAggregate = { cover: { state: "multiple" } }
        tryCompare(coverSummary, "text", "当前封面：多种封面")
    }

    function test_loadedFieldsStayUntouchedAndDeletingExistingValueClears() {
        const titleRow = page.rowForField("title")
        const titleField = findChild(page, "metadataValueField_title")
        verify(titleRow && titleField)
        titleRow.reset({ value: "Existing title", multiple: false })
        compare(titleRow.selectedMode, "keep")
        titleField.text = ""
        titleField.textEdited()
        compare(titleRow.selectedMode, "clear")
        compare(titleRow.descriptor().mode, "clear")
    }

    function test_customTagReplacesYearInEditorSchema() {
        const expectedKeys = ["title", "artist", "album", "albumArtist", "genre",
                              "composer", "date", "customTag", "bpm"]
        const expectedLabels = ["标题", "艺术家", "专辑", "专辑艺术家", "流派",
                                "作曲", "日期", "自定义标签", "BPM"]
        compare(page.fieldDefinitions.length, expectedKeys.length)
        for (let index = 0; index < expectedKeys.length; ++index) {
            compare(page.fieldDefinitions[index].key, expectedKeys[index])
            compare(page.fieldDefinitions[index].label, expectedLabels[index])
        }
        verify(page.rowForField("customTag"))
        compare(page.rowForField("year"), null)

        const tagHeader = findChild(page, "metadataTableTagHeader")
        verify(tagHeader)
        compare(tagHeader.text, "标签")
    }

    function test_processingScopeTracksCurrentSelectedAndAll() {
        MetadataEditor.loadFiles([testAudioUrl])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        page.selectedIndices = [0]
        page.selectionAnchor = 0
        const scope = findChild(page, "metadataScopeBox")
        verify(scope)
        scope.currentIndex = 0
        compare(page.targetCount(), 1)
        scope.currentIndex = 1
        compare(page.targetCount(), 1)
        scope.currentIndex = 2
        compare(page.targetCount(), MetadataEditor.fileCount)
    }

    function test_realCurrentSelectedAndAllScopesWriteAndReadBackSnapshots() {
        const scope = findChild(page, "metadataScopeBox")
        verify(scope)

        function loadCopies(count) {
            const urls = []
            for (let index = 0; index < count; ++index) {
                const copy = nativeDropHelper.copyForNativeDrop(testAudioUrl)
                verify(copy.toString().length > 0)
                urls.push(copy)
            }
            MetadataEditor.loadFiles(urls)
            tryVerify(function() { return !MetadataEditor.busy }, 5000)
            compare(MetadataEditor.fileCount, count)
            return urls
        }

        function applyTitle(title, mutateSelection) {
            page.refreshFields()
            page.setFieldMode("title", "set")
            page.setFieldValue("title", title)
            const apply = findChild(page, "metadataApplyButton")
            verify(apply && apply.enabled)
            mouseClick(apply, apply.width / 2, apply.height / 2,
                       Qt.LeftButton)
            mutateSelection()
            tryVerify(function() { return !MetadataEditor.busy }, 30000)
        }

        let files = loadCopies(2)
        page.selectedIndices = [0, 1]
        page.selectionAnchor = 1
        scope.currentIndex = 0
        applyTitle("qml-current-snapshot", function() {
            page.selectedIndices = [0]
            page.selectionAnchor = 0
        })
        compare(nativeDropHelper.probeMetadataTitle(files[0]), "")
        compare(nativeDropHelper.probeMetadataTitle(files[1]),
                "qml-current-snapshot")
        compare(MetadataEditor.results.length, 1)

        MetadataEditor.clear()
        files = loadCopies(3)
        page.selectedIndices = [0, 2]
        page.selectionAnchor = 0
        scope.currentIndex = 1
        applyTitle("qml-selected-snapshot", function() {
            page.selectedIndices = [1]
            page.selectionAnchor = 1
        })
        compare(nativeDropHelper.probeMetadataTitle(files[0]),
                "qml-selected-snapshot")
        compare(nativeDropHelper.probeMetadataTitle(files[1]), "")
        compare(nativeDropHelper.probeMetadataTitle(files[2]),
                "qml-selected-snapshot")
        compare(MetadataEditor.results.length, 2)

        MetadataEditor.clear()
        files = loadCopies(2)
        page.selectedIndices = [0]
        page.selectionAnchor = 0
        scope.currentIndex = 2
        applyTitle("qml-all-snapshot", function() {
            page.selectedIndices = []
            page.selectionAnchor = -1
        })
        compare(nativeDropHelper.probeMetadataTitle(files[0]),
                "qml-all-snapshot")
        compare(nativeDropHelper.probeMetadataTitle(files[1]),
                "qml-all-snapshot")
        compare(MetadataEditor.results.length, 2)
    }

    function test_realChinesePathsRetainOverwriteAndClearFieldsInBatch() {
        const sourceText = testAudioUrl.toString()
        const suffix = sourceText.substring(sourceText.lastIndexOf(".") + 1)
        const first = nativeDropHelper.copyForNativeDropWithFileName(
                          testAudioUrl, "歌曲一号." + suffix)
        const second = nativeDropHelper.copyForNativeDropWithFileName(
                           testAudioUrl, "歌曲二号." + suffix)
        verify(first.toString().length > 0)
        verify(second.toString().length > 0)

        MetadataEditor.clear()
        MetadataEditor.loadFiles([first, second])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        compare(MetadataEditor.fileCount, 2)
        page.selectedIndices = [0, 1]
        page.selectionAnchor = 0
        const scope = findChild(page, "metadataScopeBox")
        const apply = findChild(page, "metadataApplyButton")
        verify(scope && apply)
        scope.currentIndex = 2
        page.refreshFields()
        page.setFieldMode("title", "set")
        page.setFieldValue("title", "中文标题")
        page.setFieldMode("artist", "set")
        page.setFieldValue("artist", "原艺术家")
        page.setFieldMode("album", "set")
        page.setFieldValue("album", "保留专辑")
        mouseClick(apply)
        tryVerify(function() { return !MetadataEditor.busy }, 30000)
        compare(MetadataEditor.successCount, 2)

        MetadataEditor.clear()
        MetadataEditor.loadFiles([first, second])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        page.selectedIndices = [0]
        page.selectionAnchor = 0
        scope.currentIndex = 0
        page.refreshFields()
        page.setFieldMode("title", "clear")
        page.setFieldMode("artist", "set")
        page.setFieldValue("artist", "新艺术家")
        mouseClick(apply)
        tryVerify(function() { return !MetadataEditor.busy }, 30000)
        compare(MetadataEditor.successCount, 1)
        compare(nativeDropHelper.probeMetadataText(first, "title"), "")
        compare(nativeDropHelper.probeMetadataText(first, "artist"), "新艺术家")
        compare(nativeDropHelper.probeMetadataText(first, "album"), "保留专辑")
        compare(nativeDropHelper.probeMetadataText(second, "title"), "中文标题")
        compare(nativeDropHelper.probeMetadataText(second, "artist"), "原艺术家")
        compare(nativeDropHelper.probeMetadataText(second, "album"), "保留专辑")
    }


    function test_allScopeAndAggregateIgnoreSearchFilter() {
        const copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio.toString().length > 0)
        MetadataEditor.loadFiles([testAudioUrl, copiedAudio])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        compare(MetadataEditor.fileCount, 2)
        const scope = findChild(page, "metadataScopeBox")
        scope.currentIndex = 2
        page.searchText = "name-that-matches-nothing"
        compare(page.displayedIndices.length, 0)
        compare(page.targetCount(), 2)
        compare(page.aggregateTargetIndices().length, 2)
    }

    function test_shiftSelectionUsesDisplayedOrder() {
        const copiedAudio = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(copiedAudio.toString().length > 0)
        MetadataEditor.loadFiles([testAudioUrl, copiedAudio])
        tryVerify(function() { return !MetadataEditor.busy }, 5000)
        compare(MetadataEditor.fileCount, 2)

        page.searchText = ""
        page.sortKey = "fileName"
        page.sortAscending = true
        if (page.displayedIndices[0] < page.displayedIndices[1])
            page.sortAscending = false
        const visibleOrder = page.displayedIndices.slice()
        verify(visibleOrder[0] > visibleOrder[1])

        page.selectIndex(visibleOrder[0], Qt.NoModifier)
        page.selectIndex(visibleOrder[1], Qt.ShiftModifier)
        compare(page.selectedIndices, visibleOrder)

        page.sortAscending = true
        page.selectedIndices = []
        page.selectionAnchor = -1
    }
}
