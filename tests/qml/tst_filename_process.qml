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
    height: 941

    Component {
        id: pageComponent
        FilenameProcessPage { width: 1668; height: 835 }
    }

    Component {
        id: compactPageComponent
        FilenameProcessPage { width: 880; height: 468 }
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

    function test_compactWorkbenchScrollsInternallyWithoutEscapingPage() {
        const compactPage = createTemporaryObject(compactPageComponent, testCase)
        verify(compactPage)
        wait(0)
        verify(compactPage.compactLayout)
        const scroller = findChild(compactPage, "filenameWorkspaceScroller")
        const commandBar = findChild(compactPage, "filenameCommandBar")
        const bottomBar = findChild(compactPage, "filenameBottomBar")
        verify(scroller && commandBar && bottomBar)
        verify(scroller.width <= compactPage.width)
        verify(scroller.height > 0)
        verify(scroller.contentWidth > scroller.width)
        const commandPosition = commandBar.mapToItem(compactPage, 0, 0)
        const bottomPosition = bottomBar.mapToItem(compactPage, 0, 0)
        verify(commandPosition.x >= 0 && commandPosition.y >= 0)
        verify(commandPosition.x + commandBar.width <= compactPage.width)
        verify(bottomPosition.x >= 0 && bottomPosition.y >= 0)
        verify(bottomPosition.x + bottomBar.width <= compactPage.width)
        verify(bottomPosition.y + bottomBar.height <= compactPage.height)
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
        const candidate = createTemporaryObject(compactPageComponent, testCase,
                                                { width: data.w, height: data.h })
        verify(candidate)
        wait(0)
        const command = findChild(candidate, "filenameCommandBar")
        const scroller = findChild(candidate, "filenameWorkspaceScroller")
        const bottom = findChild(candidate, "filenameBottomBar")
        verify(command && scroller && bottom)
        for (const item of [command, scroller, bottom]) {
            const position = item.mapToItem(candidate, 0, 0)
            verify(position.x >= 0 && position.y >= 0)
            verify(position.x + item.width <= candidate.width)
            verify(position.y + item.height <= candidate.height)
        }
        verify(scroller.contentWidth >= scroller.width)
        compare(candidate.compactLayout, data.w < 1500)
        if (candidate.width < candidate.desktopWorkspaceWidth)
            verify(scroller.contentWidth > scroller.width)

        for (const name of ["filenameReadySummaryCard",
                            "filenameConflictSummaryCard",
                            "filenameUndoSummaryCard",
                            "filenameStartButton", "filenameCancelButton"]) {
            const item = findChild(bottom, name)
            verify(item, "missing " + name)
            const position = item.mapToItem(bottom, 0, 0)
            verify(position.x >= -0.5, name + " starts outside the footer")
            verify(position.x + item.width <= bottom.width + 0.5,
                   name + " overflows the footer")
        }
    }

    function test_referenceLayoutAndInteractiveRules() {
        const page = createTemporaryObject(pageComponent, testCase)
        verify(page)
        const filePanel = findChild(page, "filenameFilePanel")
        const rulesPanel = findChild(page, "filenameRulesPanel")
        const previewPanel = findChild(page, "filenamePreviewPanel")
        const bottomBar = findChild(page, "filenameBottomBar")
        const commandBar = findChild(page, "filenameCommandBar")
        verify(filePanel)
        verify(rulesPanel)
        verify(previewPanel)
        verify(bottomBar)
        verify(commandBar)

        const fileTop = filePanel.mapToItem(page, 0, 0)
        const rulesTop = rulesPanel.mapToItem(page, 0, 0)
        const previewTop = previewPanel.mapToItem(page, 0, 0)
        const bottomTop = bottomBar.mapToItem(page, 0, 0)
        verify(fileTop.x < rulesTop.x)
        verify(previewTop.x >= rulesTop.x - 1)
        verify(previewTop.y > rulesTop.y + rulesPanel.height - 2)
        verify(bottomTop.y > fileTop.y + filePanel.height - 2)
        compare(Math.round(commandBar.height),
                Theme.settingsRowHeight + Theme.spacingSm)
        verify(Math.abs(filePanel.width - 619) <= 3)
        verify(Math.abs(rulesPanel.height - 259) <= 1)
        verify(Math.abs(bottomBar.height - 124) <= 1)

        const caseBox = findChild(page, "filenameCaseBox")
        const conflictBox = findChild(page, "filenameConflictBox")
        const prefixAdd = findChild(page, "filenamePrefixAddRadio")
        const prefixRemove = findChild(page, "filenamePrefixRemoveRadio")
        const suffixAdd = findChild(page, "filenameSuffixAddRadio")
        const suffixRemove = findChild(page, "filenameSuffixRemoveRadio")
        const prefixField = findChild(page, "filenamePrefixField")
        const removePrefixField = findChild(page, "filenameRemovePrefixField")
        const suffixField = findChild(page, "filenameSuffixField")
        const removeSuffixField = findChild(page, "filenameRemoveSuffixField")
        verify(caseBox)
        verify(conflictBox)
        verify(prefixAdd)
        verify(prefixRemove)
        verify(suffixAdd)
        verify(suffixRemove)
        verify(prefixField)
        verify(removePrefixField)
        verify(suffixField)
        verify(removeSuffixField)
        verify(caseBox.width >= 112)
        verify(conflictBox.width >= 180)

        if (visualFixtureOutput) {
            let captured = false
            verify(rulesPanel.grabToImage(function(result) {
                result.saveToFile(visualFixtureOutput + "-filename-rules.png")
                captured = true
            }))
            tryVerify(function() { return captured })
        }

        compare(prefixAdd.checked, true)
        let payload = page.rules()
        compare(payload.prefix, "")
        compare(payload.suffix, "")
        compare(payload.removePrefixWhenEmpty, true)
        compare(payload.removeSuffixWhenEmpty, true)
        compare(payload.removeSequenceWhenEmpty, false)

        prefixField.text = "[Live]_"
        suffixField.text = "_Remaster"
        payload = page.rules()
        compare(payload.prefix, "[Live]_")
        compare(payload.suffix, "_Remaster")
        compare(payload.removePrefix, "")
        compare(payload.removePrefixWhenEmpty, false)
        compare(payload.removeSuffixWhenEmpty, false)
        compare(payload.removeSequenceWhenEmpty, false)

        mouseClick(prefixRemove, prefixRemove.width / 2, prefixRemove.height / 2)
        removePrefixField.text = "OLD_"
        payload = page.rules()
        compare(payload.prefix, "")
        compare(payload.removePrefix, "OLD_")
        compare(payload.removePrefixWhenEmpty, false)

        mouseClick(suffixRemove, suffixRemove.width / 2, suffixRemove.height / 2)
        removeSuffixField.text = "_HQ"
        payload = page.rules()
        compare(payload.suffix, "")
        compare(payload.removeSuffix, "_HQ")
        compare(payload.removeSuffixWhenEmpty, false)
    }

    function test_removeSequenceIsMutuallyExclusiveWithAutoNumber() {
        const page = createTemporaryObject(pageComponent, testCase)
        verify(page)
        wait(0)
        const autoNumber = findChild(page, "filenameAutoNumberCheck")
        const removeSequence = findChild(page, "filenameRemoveSequenceCheck")
        verify(autoNumber && removeSequence)
        compare(autoNumber.parent, removeSequence.parent)
        verify(removeSequence.x > autoNumber.x)

        autoNumber.checked = false
        removeSequence.checked = true
        const removeRules = page.rules()
        verify(removeRules.removeSequenceAtStart)
        verify(removeRules.removeSequenceAtEnd)
        verify(!removeRules.autoNumber)

        autoNumber.checked = true
        compare(removeSequence.checked, false)
        const addRules = page.rules()
        verify(!addRules.removeSequenceAtStart)
        verify(!addRules.removeSequenceAtEnd)
        verify(addRules.autoNumber)

        removeSequence.checked = true
        compare(autoNumber.checked, false)
        const removeAgainRules = page.rules()
        verify(removeAgainRules.removeSequenceAtStart)
        verify(!removeAgainRules.autoNumber)
    }
}
