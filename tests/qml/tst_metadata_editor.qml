import QtQuick
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

    Component {
        id: compactPageComponent
        MetadataEditPage { width: 880; height: 620 }
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
        verify(apply.height >= 40)
        verify(cancel.height >= 40)
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
        tryVerify(function() {
            return scroll.contentHeight > scroll.availableHeight
        }, 1000)
        scroll.contentItem.contentY = scroll.contentHeight
                                      - scroll.availableHeight
        tryVerify(function() { return scroll.contentItem.contentY > 0 }, 1000)
        page.resetEdits()
    }

    function test_compactLayoutKeepsBothWorkspacesReachable() {
        const compactPage = createTemporaryObject(compactPageComponent, testCase)
        verify(compactPage)
        wait(0)
        const files = findChild(compactPage, "metadataFilePanel")
        const inspector = findChild(compactPage, "metadataInspectorPanel")
        const tabs = findChild(compactPage, "metadataCompactTabs")
        verify(files && inspector && tabs)
        verify(files.visible)
        tabs.currentIndex = 1
        tryVerify(function() { return inspector.visible && inspector.width > 0 })
        verify(inspector.mapToItem(compactPage, inspector.width, 0).x <= compactPage.width)
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
        const coverSummary = findChild(page, "metadataCoverSummaryLabel")
        verify(titleRow && titleField && coverSummary)
        titleRow.reset({ value: "", multiple: true })
        compare(titleField.text, "")
        compare(titleField.placeholderText, "多种值")
        page.scopeAggregate = { cover: { state: "multiple" } }
        tryCompare(coverSummary, "text", "当前封面：多种封面")
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
