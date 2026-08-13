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
        verify(caseBox)
        verify(conflictBox)
        verify(issueFilter)
        verify(caseBox.width >= 112)
        verify(conflictBox.width >= 180)
        compare(page.issueFilterEnabled, false)
        mouseClick(issueFilter, issueFilter.width / 2, issueFilter.height / 2)
        compare(page.issueFilterEnabled, true)
    }
}
