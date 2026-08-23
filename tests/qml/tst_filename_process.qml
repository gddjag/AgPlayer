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
        compare(Math.round(commandBar.height), 60)
        verify(Math.abs(filePanel.width - 619) <= 3)
        verify(Math.abs(rulesPanel.height - 259) <= 1)
        verify(Math.abs(bottomBar.height - 135) <= 1)

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

    function test_removeSequenceIsIndependentAndAdjacentToAutoNumber() {
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
        removeSequence.checked = false
        const addRules = page.rules()
        verify(!addRules.removeSequenceAtStart)
        verify(!addRules.removeSequenceAtEnd)
        verify(addRules.autoNumber)
    }
}
