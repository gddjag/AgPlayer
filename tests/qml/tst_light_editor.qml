import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "LightEditor"
    when: windowShown

    width: 1350
    height: 900

    Component {
        id: pageComponent
        LightEditPage {
            width: 1350
            height: 900
        }
    }

    Component {
        id: toolsWindowComponent
        AudioToolsWindow {
            visible: false
        }
    }

    property var page

    function init() {
        page = createTemporaryObject(pageComponent, testCase)
        verify(page)
        wait(50)
    }

    function collectByObjectName(item, name, result) {
        if (!item) return
        if (item.objectName === name) result.push(item)
        const childItems = item.children || []
        for (let i = 0; i < childItems.length; ++i)
            collectByObjectName(childItems[i], name, result)
    }

    function test_newEditorControlsAndSixLanesExist() {
        verify(findChild(page, "targetBpmField"))
        verify(findChild(page, "snapGridBox"))
        verify(findChild(page, "unifyBpmButton"))
        verify(findChild(page, "alignBpmButton"))

        const lanes = []
        collectByObjectName(page, "trackLane", lanes)
        compare(lanes.length, 6)
    }

    function test_wheelZoomEntryPointChangesScale() {
        compare(page.zoomScale, 1.0)
        verify(typeof page.applyWheelZoom === "function")
        page.applyWheelZoom(120, page.width / 2)
        verify(page.zoomScale > 1.0)
    }

    function test_audioToolsWindowUsesReferenceSize() {
        const toolsWindow = createTemporaryObject(toolsWindowComponent, testCase)
        verify(toolsWindow)
        compare(toolsWindow.width, 1536)
        compare(toolsWindow.height, 1024)
    }
}
