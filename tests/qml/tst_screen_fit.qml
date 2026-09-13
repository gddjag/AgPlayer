import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "ScreenFit"
    when: windowShown
    property var mainWindow: null
    property int savedMode: 0

    Component { id: integratedComponent; IntegratedPlayerShell {} }
    Component { id: rollingComponent; RollingPlayerShell {} }
    Component { id: listComponent; ListWindow {} }

    function initTestCase() {
        mainWindow = testMainWindow
        savedMode = SettingsController.playerShellMode
        mainWindow.visible = true
    }

    function cleanupTestCase() { SettingsController.playerShellMode = savedMode }

    function test_main_minimum_does_not_defeat_work_area_budget() {
        var area = WindowController.availableGeometryForWindow(mainWindow)
        for (var mode of [1, 2]) {
            SettingsController.playerShellMode = mode
            verify(mainWindow.minimumWidth <= Math.floor(area.width * 0.78))
            verify(mainWindow.minimumHeight <= Math.floor(area.height * 0.78))
        }
    }

    function test_compact_shell_footer_remains_reachable_data() {
        return [
            { tag: "1280-taskbar-single", mode: 1, width: 998, height: 530 },
            { tag: "1280-taskbar-pro", mode: 2, width: 998, height: 530 },
            { tag: "1366-taskbar-single", mode: 1, width: 1065, height: 567 },
            { tag: "1366-taskbar-pro", mode: 2, width: 1065, height: 567 },
            { tag: "pro-boundary-649", mode: 2, width: 1202, height: 649 },
            { tag: "pro-boundary-650", mode: 2, width: 1202, height: 650 },
            { tag: "pro-boundary-660", mode: 2, width: 1202, height: 660 },
            { tag: "pro-boundary-680", mode: 2, width: 1202, height: 680 },
            { tag: "1440-dock-pro", mode: 2, width: 1123, height: 636 },
            { tag: "1536-scaled-pro", mode: 2, width: 1198, height: 642 }
        ]
    }

    function test_compact_shell_footer_remains_reachable(data) {
        SettingsController.playerShellMode = 0
        mainWindow.width = data.width
        mainWindow.height = data.height
        var component = data.mode === 1 ? integratedComponent : rollingComponent
        var shell = createTemporaryObject(component, mainWindow.contentItem, {
            width: data.width, height: data.height, hostWindow: mainWindow
        })
        verify(shell)
        var footer = findChild(shell, data.mode === 1 ? "integratedBottomBar"
                                                    : "rollingLibraryWorkspace")
        verify(footer)
        tryVerify(function() {
            var point = shell.mapFromItem(footer, 0, 0)
            return footer.visible && footer.height > 0
                    && point.y >= 0 && point.y + footer.height <= shell.height
        })
        var filter = findChild(shell, data.mode === 1 ? "integratedSearchFilter" : "rollingSearchFilter")
        var clearButton = findChild(filter, "clearFiltersButton")
        verify(filter && clearButton)
        tryVerify(function() {
            var end = filter.parent.mapFromItem(clearButton, clearButton.width, clearButton.height)
            return clearButton.visible && end.x <= filter.parent.width && end.y <= filter.parent.height
        }, 1000, "The clear action must fit inside its actual center-column parent")
        filter.searchText = "fixture search"
        filter.exactRating = 3
        filter.minBpm = 70
        filter.maxBpm = 140
        mouseClick(clearButton, clearButton.width / 2, clearButton.height / 2)
        compare(filter.searchText, "")
        compare(filter.exactRating, 0)
        compare(filter.minBpm, 60)
        compare(filter.maxBpm, 160)
        var table = findChild(shell, data.mode === 1 ? "integratedTrackList" : "rollingTrackList")
        var emptyResult = findChild(table, "emptyTrackResult")
        if (emptyResult && emptyResult.visible) {
            tryVerify(function() {
                var point = table.mapFromItem(emptyResult, 0, 0)
                return point.y >= table.headerHeight
            }, 1000, "An empty-list hint must stay below the table header")
        }
        if (visualFixtureOutput && data.tag === "1280-taskbar-pro") {
            var hostShellLoader = findChild(mainWindow, "playerShellLoader")
            var hostShellVisible = hostShellLoader.visible
            hostShellLoader.visible = false
            wait(0)
            grabImage(shell).save(visualFixtureOutput + "-small-screen-professional.png")
            hostShellLoader.visible = hostShellVisible
        }
    }

    function test_classic_compact_list_footer_remains_reachable_data() {
        return [{tag: "default", width: 863}, {tag: "minimum", width: 612}]
    }

    function test_classic_compact_list_footer_remains_reachable(data) {
        var list = createTemporaryObject(listComponent, null, {width: data.width, height: 266})
        verify(list)
        list.visible = true
        var footer = findChild(list, "centerTrackFooter")
        verify(footer)
        tryVerify(function() {
            var point = list.contentItem.mapFromItem(footer, 0, 0)
            return footer.visible && footer.height > 0
                    && point.y + footer.height <= list.height
        })
        var clearButton = findChild(footer, "clearFiltersButton")
        verify(clearButton)
        tryVerify(function() {
            var end = footer.mapFromItem(clearButton, clearButton.width, clearButton.height)
            return end.x <= footer.width && end.y <= footer.height
        }, 1000, "The classic clear action must fit inside its footer")
        var filter = findChild(footer, "librarySearchFilter")
        filter.searchText = "fixture search"
        mouseClick(clearButton, clearButton.width / 2, clearButton.height / 2)
        compare(filter.searchText, "")
    }
}
