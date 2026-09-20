import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "ResourceNavigation"
    when: windowShown

    Window { id: host; width: 420; height: 320; visible: true }
    property var navigation: null
    property var paths: []
    property var filterModel: null

    function initTestCase() {
        testMainWindow.hide()
        filterModel = findChild(testMainWindow, "filterModel")
        verify(filterModel)
        filterModel.searchText = ""
        filterModel.tagKey = ""
        filterModel.minBpm = 60
        filterModel.maxBpm = 160
        var component = Qt.createComponent(Qt.resolvedUrl(
            "../../app/qml/AgPlayer/components/SideNavigation.qml"))
        compare(component.status, Component.Ready, component.errorString())
        navigation = component.createObject(host.contentItem, {width: 420, height: 320})
        verify(navigation)
        navigation.navigationSelected.connect(function(type, id, path) {
            filterModel.category = "all"
            filterModel.resourceFolder = path
        })
        for (var i = 0; i < 12; ++i) {
            var url = nativeDropHelper.createAudioDropDirectory(testAudioUrl, 1)
            var path = ResourceFolderController.classifyDropUrl(url).path
            verify(path.length > 0)
            paths.push(path)
            verify(ResourceFolderController.addMonitoredFolder(path))
        }
        LibraryNavigationModel.setExpanded("library:all", false)
        ResourceFolderController.rescan()
        tryVerify(function() { return !ResourceFolderController.scanning && !ImportController.busy }, 5000)
        host.requestActivate()
        wait(100)
    }

    function cleanupTestCase() {
        filterModel.resourceFolder = ""
        for (var i = 0; i < paths.length; ++i)
            ResourceFolderController.removeMonitoredFolder(paths[i])
        navigation.destroy()
        host.close()
    }

    function test_click_scrolled_and_readded_folders() {
        verify(ResourceFolderController.removeMonitoredFolder(paths[4]))
        verify(ResourceFolderController.addMonitoredFolder(paths[4]))
        wait(450)
        var list = findChild(navigation, "libraryNavigationList")
        verify(list)
        for (var i = 0; i < paths.length; ++i) {
            var nodeId = "root:" + paths[i]
            verify(navigation.revealNode(nodeId))
            wait(50)
            var row = findChild(navigation, "navigationNode-" + nodeId)
            verify(row)
            compare(row.count, 1)
            var position = row.mapToItem(list, row.width * 0.75, row.height / 2)
            filterModel.resourceFolder = ""
            mouseClick(list, position.x, position.y, Qt.LeftButton)
            compare(filterModel.resourceFolder, paths[i], "Scrolled resource row " + i)
            tryCompare(filterModel, "count", 1)
        }
        verify(list.contentY > 0, "Exercise actual scrolling, not only the first visible rows")
    }

    function test_click_survives_model_reset() {
        // Keep the pressed row stationary while reset replaces its delegate.
        host.height = 900
        navigation.height = 900
        verify(navigation.revealNode("root:" + paths[0]))
        wait(50)
        var list = findChild(navigation, "libraryNavigationList")
        var row = findChild(navigation, "navigationNode-root:" + paths[0])
        var position = row.mapToItem(list, row.width * 0.75, row.height / 2)
        filterModel.resourceFolder = ""
        mousePress(list, position.x, position.y, Qt.LeftButton)
        var path = ResourceFolderController.classifyDropUrl(
            nativeDropHelper.createDropDirectory()).path
        paths.push(path)
        verify(ResourceFolderController.addMonitoredFolder(path))
        wait(0)
        mouseRelease(list, position.x, position.y, Qt.LeftButton)
        compare(filterModel.resourceFolder, paths[0])
        host.height = 320
        navigation.height = 320
    }

    function test_scrolled_expand_arrow_and_child_selection() {
        var path = ResourceFolderController.classifyDropUrl(
            nativeDropHelper.createNestedDropDirectory()).path
        paths.push(path)
        verify(ResourceFolderController.addMonitoredFolder(path))
        ResourceFolderController.rescan()
        tryVerify(function() { return !ResourceFolderController.scanning }, 3000)
        verify(navigation.revealNode("root:" + path))
        wait(50)
        var row = findChild(navigation, "navigationNode-root:" + path)
        verify(row.hasChildren)
        var button = findChild(row, "navigationExpandButton")
        filterModel.resourceFolder = paths[0]
        mouseClick(button, button.width / 2, button.height / 2, Qt.LeftButton)
        compare(filterModel.resourceFolder, paths[0], "Expanding is not selecting")
        var child = path + "/child"
        verify(navigation.revealNode("folder:" + child))
        wait(50)
        row = findChild(navigation, "navigationNode-folder:" + child)
        var list = findChild(navigation, "libraryNavigationList")
        var position = row.mapToItem(list, row.width * 0.75, row.height / 2)
        mouseClick(list, position.x, position.y, Qt.LeftButton)
        compare(filterModel.resourceFolder, child)
    }

    function test_scan_dialog_is_bounded_and_wraps() {
        var dialog = findChild(navigation, "resourceScanDialog")
        verify(dialog)
        dialog.open()
        tryCompare(dialog, "opened", true)
        var label = findChild(dialog, "resourceScanStatus")
        verify(label)
        label.text = "扫描完成：发现 642 个音频文件，559 个文件已提交导入，歌曲数量将在导入后更新。已有歌曲不会重复加入；手动重新扫描会恢复目录中曾移除的歌曲。"
        wait(100)
        verify(dialog.width <= 480)
        verify(dialog.width < host.width)
        verify(label.implicitHeight > label.font.pixelSize * 2, "Long scan messages wrap")
        verify(dialog.height < host.height)
        if (visualFixtureOutput) {
            var saved = false
            host.contentItem.grabToImage(function(result) {
                saved = result.saveToFile(visualFixtureOutput)
            })
            tryVerify(function() { return saved })
        }
        dialog.close()
    }
}
