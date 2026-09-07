import QtQuick
import QtQuick.Controls
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "TagCapsule"
    when: windowShown
    visible: true
    width: 64
    height: 64

    property string firstKey: ""
    property string secondKey: ""
    property string firstLabel: ""
    property string secondLabel: ""

    ApplicationWindow {
        id: hostWindow
        visible: true
        width: 320
        height: 260

        QtObject {
            id: filterModel
            property string tagKey: ""
        }

        TagManagementPanel {
            id: panel
            anchors.fill: parent
            filterModel: filterModel
            showHeader: true
        }
    }

    function childObjects(parent) {
        var values = []
        function append(items) {
            if (!items)
                return
            for (var index = 0; index < items.length; ++index) {
                if (items[index] && values.indexOf(items[index]) < 0)
                    values.push(items[index])
            }
        }
        append(parent.children)
        append(parent.data)
        if (parent.contentItem)
            append([parent.contentItem])
        return values
    }

    function findChild(parent, objectName, visited) {
        if (!parent)
            return null
        visited = visited || []
        if (visited.indexOf(parent) >= 0)
            return null
        visited.push(parent)
        if (parent.objectName === objectName)
            return parent
        var children = childObjects(parent)
        for (var index = 0; index < children.length; ++index) {
            var match = findChild(children[index], objectName, visited)
            if (match)
                return match
        }
        return null
    }

    function makeTag(label) {
        verify(TagModel.createTag(label))
        var key = panel.tagKeyForDisplayName(label)
        verify(key.length > 0)
        return key
    }

    function init() {
        failOnWarning(/.?/)
        TagModel.selectedKey = ""
        filterModel.tagKey = ""
        panel.searchText = ""
        hostWindow.requestActivate()
        tryVerify(function() { return hostWindow.active }, 1000)
        var suffix = String(Date.now()) + "-" + String(Math.random())
        firstLabel = "Capsule A " + suffix
        secondLabel = "A deliberately long capsule label that wraps the Flow "
                + suffix
        firstKey = makeTag(firstLabel)
        secondKey = makeTag(secondLabel)
        tryVerify(function() {
            return findChild(panel, "tagPill-" + firstKey)
                    && findChild(panel, "tagPill-" + secondKey)
        }, 1000)
    }

    function cleanup() {
        TagModel.selectedKey = ""
        filterModel.tagKey = ""
        panel.searchText = ""
        if (firstKey)
            TagModel.removeTag(firstKey)
        if (secondKey)
            TagModel.removeTag(secondKey)
        firstKey = ""
        secondKey = ""
        firstLabel = ""
        secondLabel = ""
        wait(0)
    }

    function test_pointer_toggle_never_draws_focus_ring() {
        panel.searchText = firstLabel
        tryVerify(function() {
            return findChild(panel, "tagPill-" + firstKey) !== null
        }, 500)
        var pill = findChild(panel, "tagPill-" + firstKey)
        var pointer = findChild(panel, "tagPillPointerArea-" + firstKey)
        var focusRing = findChild(panel, "tagCapsuleFocus-" + firstKey)
        verify(pill && pointer && focusRing)

        mouseClick(pointer, pointer.width / 2, pointer.height / 2)
        tryVerify(function() { return pill.selectedVisual }, 500)
        verify(pointer.activeFocus)
        verify(!focusRing.visible)

        mouseClick(pointer, pointer.width / 2, pointer.height / 2)
        tryVerify(function() { return !pill.selectedVisual }, 500)
        verify(pointer.activeFocus)
        verify(!focusRing.visible,
               "pointer focus must not leave a white ring after deselection")
    }

    function test_keyboard_tab_focus_keeps_focus_ring() {
        var search = findChild(panel, "tagSearchField")
        verify(search)
        panel.searchText = firstLabel
        tryVerify(function() {
            return findChild(panel, "tagPill-" + firstKey) !== null
        }, 500)
        var pointer = findChild(panel, "tagPillPointerArea-" + firstKey)
        var focusRing = findChild(panel, "tagCapsuleFocus-" + firstKey)
        verify(pointer && focusRing)
        search.forceActiveFocus()
        for (var step = 0; step < 4 && !pointer.activeFocus; ++step) {
            verify(nativeDropHelper.sendKey(search, Qt.Key_Tab))
            wait(0)
        }
        verify(pointer.activeFocus)
        verify(focusRing.visible,
               "keyboard navigation must retain the focus affordance")
    }

    function test_capsules_share_compact_height() {
        var first = findChild(panel, "tagPill-" + firstKey)
        var second = findChild(panel, "tagPill-" + secondKey)
        var flow = findChild(panel, "tagFlow")
        verify(first && second && flow)
        compare(first.height, 24)
        compare(second.height, 24)
        compare(first.parent.height, first.height)
        compare(second.parent.height, second.height)
        compare(second.parent.width, flow.width)
        panel.selectTag(firstKey)
        tryVerify(function() { return first.selectedVisual }, 500)
        compare(first.height, second.height)
        verify(findChild(first, "tagCapsuleName-" + firstKey).implicitHeight
               <= first.height)
        verify(findChild(second, "tagCapsuleName-" + secondKey).implicitHeight
               <= second.height)
        verify(findChild(first, "tagCapsuleCount-" + firstKey).implicitHeight
               <= first.height)
        verify(findChild(second, "tagCapsuleCount-" + secondKey).implicitHeight
               <= second.height)
    }
}
