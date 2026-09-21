import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    name: "RecentPlaybackNavigation"
    when: windowShown
    Window { id: host; width: 1386; height: 850; visible: true; color: Theme.surface }
    property var filterModel: null
    property var trackIds: []
    property real savedVolume: 0

    function initTestCase() {
        testMainWindow.hide()
        filterModel = findChild(testMainWindow, "filterModel")
        verify(filterModel)
        filterModel.category = "all"
        filterModel.searchText = ""
        filterModel.tagKey = ""
        filterModel.resourceFolder = ""
        filterModel.minBpm = 60
        filterModel.maxBpm = 160
        PlaybackController.stop()
        savedVolume = PlaybackController.volume
        PlaybackController.setVolume(0)
        nativeDropHelper.clearLibrary()
        trackIds = nativeDropHelper.ensureSortableTracks()
        compare(trackIds.length, 3)
        PlaybackController.playRow(0)
        PlaybackController.pause()
        tryCompare(LibraryModel, "historyCount", 1)
        wait(10)
        PlaybackController.playRow(1)
        PlaybackController.pause()
        tryCompare(LibraryModel, "historyCount", 2)
    }

    function cleanupTestCase() {
        PlaybackController.stop()
        PlaybackController.setVolume(savedVolume)
        filterModel.category = "all"
        host.close()
    }

    function test_recent_playback_data() {
        return [
            {tag: "classic", file: "ListWindow.qml", navigation: "referenceSideNavigation"},
            {tag: "integrated", file: "components/IntegratedPlayerShell.qml", navigation: "integratedLibraryNavigation"},
            {tag: "rolling", file: "components/RollingPlayerShell.qml", navigation: "rollingLibraryNavigation"}
        ]
    }

    function test_compact_sidebar_preview() {
        if (!visualFixtureOutput)
            skip("Set AGPLAYER_VISUAL_FIXTURE_OUTPUT to capture the navigation")
        var component = Qt.createComponent(Qt.resolvedUrl(
            "../../app/qml/AgPlayer/components/SideNavigation.qml"))
        compare(component.status, Component.Ready, component.errorString())
        var navigation = component.createObject(host.contentItem,
            {width: 216, height: 300, selectedCategory: "history"})
        verify(navigation)
        try {
            wait(50)
            grabImage(navigation).save(visualFixtureOutput.replace(/\.png$/i, "") + "-compact.png")
        } finally {
            navigation.destroy()
        }
    }

    function test_recent_playback(data) {
        filterModel.category = "all"
        var component = Qt.createComponent(Qt.resolvedUrl(
            "../../app/qml/AgPlayer/" + data.file))
        compare(component.status, Component.Ready, component.errorString())
        var properties = {width: host.width, height: host.height,
                          visible: true, filterModel: filterModel}
        if (data.tag !== "classic")
            properties.hostWindow = host
        var shell = component.createObject(data.tag === "classic" ? null : host.contentItem, properties)
        verify(shell)
        try {
            var navigation = findChild(shell, data.navigation)
            verify(navigation)
            for (var windowWidth of [1100, 1386]) {
                shell.width = windowWidth
                wait(50)
                tryCompare(navigation, "width", Theme.navigationWidth)
                var resourceLabel = findChild(navigation, "resourceFolderRefreshLabel")
                verify(resourceLabel)
                tryCompare(resourceLabel, "truncated", false)
            }
            verify(navigation.revealNode("history:history"))
            wait(50)
            var recent = null
            tryVerify(function() {
                recent = findChild(navigation, "historyCategoryButton")
                return recent && recent.visible
            }, 2000)
            compare(recent.displayName, "最近播放")
            compare(navigation.navigationModel.data(
                        navigation.navigationModel.index(recent.index - 1, 0),
                        LibraryNavigationModel.NodeIdRole),
                    "favorites:favorites")
            compare(recent.count, 2)
            mouseClick(recent, recent.width * 0.6, recent.height / 2, Qt.LeftButton)
            tryCompare(filterModel, "category", "history")
            compare(filterModel.count, 2)
            compare(filterModel.data(filterModel.index(0, 0), LibraryModel.TrackIdRole), trackIds[1])
            compare(filterModel.data(filterModel.index(1, 0), LibraryModel.TrackIdRole), trackIds[0])
            verify(recent.selected)
            if (visualFixtureOutput && data.tag !== "classic")
                grabImage(navigation).save(visualFixtureOutput.replace(/\.png$/i, "") + "-" + data.tag + ".png")
        } finally {
            shell.destroy()
            filterModel.category = "all"
        }
    }
}
