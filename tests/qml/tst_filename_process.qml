import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "FilenameProcess"
    when: windowShown
    visible: true
    width: 1672
    height: 942

    Component {
        id: pageComponent
        FilenameProcessPage { width: 1672; height: 942 }
    }

    function test_referenceLayoutAndInteractiveRules() {
        const page = createTemporaryObject(pageComponent, testCase)
        verify(page)
        const filePanel = findChild(page, "filenameFilePanel")
        const rulesPanel = findChild(page, "filenameRulesPanel")
        const previewPanel = findChild(page, "filenamePreviewPanel")
        const bottomBar = findChild(page, "filenameBottomBar")
        verify(filePanel)
        verify(rulesPanel)
        verify(previewPanel)
        verify(bottomBar)

        const fileTop = filePanel.mapToItem(page, 0, 0)
        const rulesTop = rulesPanel.mapToItem(page, 0, 0)
        const previewTop = previewPanel.mapToItem(page, 0, 0)
        const bottomTop = bottomBar.mapToItem(page, 0, 0)
        verify(fileTop.x < rulesTop.x)
        verify(previewTop.x >= rulesTop.x - 1)
        verify(previewTop.y > rulesTop.y + rulesPanel.height - 2)
        verify(bottomTop.y > fileTop.y + filePanel.height - 2)

        const caseBox = findChild(page, "filenameCaseBox")
        const conflictBox = findChild(page, "filenameConflictBox")
        const issueFilter = findChild(page, "filenameIssueFilterButton")
        const removePrefix = findChild(page, "filenameRemovePrefixField")
        const removeSuffix = findChild(page, "filenameRemoveSuffixField")
        const removeLeading = findChild(page, "filenameRemoveLeadingSequence")
        const removeTrailing = findChild(page, "filenameRemoveTrailingSequence")
        verify(caseBox)
        verify(conflictBox)
        verify(issueFilter)
        verify(removePrefix && removeSuffix && removeLeading && removeTrailing)
        verify(caseBox.width >= 112)
        verify(conflictBox.width >= 180)
        compare(page.issueFilterEnabled, false)
        mouseClick(issueFilter, issueFilter.width / 2, issueFilter.height / 2)
        compare(page.issueFilterEnabled, true)
    }

    function test_compact_layout_keeps_rules_preview_and_actions_inside() {
        const page = createTemporaryObject(pageComponent, testCase,
                                           {width: 880, height: 457})
        verify(page)
        const filePanel = findChild(page, "filenameFilePanel")
        const rulesPanel = findChild(page, "filenameRulesPanel")
        const previewPanel = findChild(page, "filenamePreviewPanel")
        const bottom = findChild(page, "filenameBottomBar")
        const primary = findChild(page, "filenamePrimaryRules")
        const numbering = findChild(page, "filenameNumberingRules")
        const cancelButton = findChild(page, "filenameCancelButton")
        const startButton = findChild(page, "filenameStartButton")
        verify(filePanel && rulesPanel && previewPanel && bottom)
        verify(primary && numbering && cancelButton && startButton)
        verify(filePanel.width >= 250)
        const fileTop = filePanel.mapToItem(page, 0, 0)
        const rulesTop = rulesPanel.mapToItem(page, 0, 0)
        verify(rulesTop.x >= fileTop.x + filePanel.width)
        verify(primary.x + primary.width <= rulesPanel.width)
        verify(numbering.x + numbering.width <= rulesPanel.width)
        verify(bottom.width <= page.width)
        const cancelRight = cancelButton.mapToItem(page, cancelButton.width, 0).x
        const startRight = startButton.mapToItem(page, startButton.width, 0).x
        verify(cancelRight <= page.width)
        verify(startRight <= page.width)
    }

    function test_dense_layout_keeps_rule_groups_and_actions_inside() {
        const page = createTemporaryObject(pageComponent, testCase,
                                           {width: 1280, height: 720})
        verify(page)
        const rulesPanel = findChild(page, "filenameRulesPanel")
        const primary = findChild(page, "filenamePrimaryRules")
        const options = findChild(page, "filenameOptionRules")
        const numbering = findChild(page, "filenameNumberingRules")
        const cancelButton = findChild(page, "filenameCancelButton")
        const startButton = findChild(page, "filenameStartButton")
        verify(rulesPanel && primary && options && numbering)
        verify(cancelButton && startButton)
        for (const group of [primary, options, numbering]) {
            const bottomRight = group.mapToItem(rulesPanel, group.width, group.height)
            verify(bottomRight.x <= rulesPanel.width + 1)
            verify(bottomRight.y <= rulesPanel.height + 1)
        }
        verify(cancelButton.mapToItem(page, cancelButton.width, 0).x <= page.width)
        verify(startButton.mapToItem(page, startButton.width, 0).x <= page.width)
    }
}
