import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "AudioToolsResponsive"
    width: 1800
    height: 1000
    visible: true
    when: windowShown

    function init() { failOnWarning(/Binding loop detected/) }

    function componentAt(file) {
        var component = Qt.createComponent("qrc:/qt/qml/AgPlayer/qml/AgPlayer/" + file)
        compare(component.status, Component.Ready, component.errorString())
        return component
    }
    function inside(item, owner) {
        var p = item ? item.mapToItem(owner, 0, 0) : null
        var visible = item && item.visible
        var clipped = []
        for (var ancestor = item ? item.parent : null; ancestor && ancestor !== owner;
             ancestor = ancestor.parent) {
            if (ancestor.clip)
                clipped.push({owner: ancestor, point: item.mapToItem(ancestor, 0, 0)})
        }
        verify(item, "missing control")
        verify(visible, item.objectName + " is hidden")
        verify(p.x >= -1 && p.y >= -1 && p.x + item.width <= owner.width + 1
               && p.y + item.height <= owner.height + 1,
               item.objectName + " outside " + owner.width + "x" + owner.height
               + ": " + p.x + "," + p.y + " " + item.width + "x" + item.height)
        for (var clippedItem of clipped) {
            var local = clippedItem.point
            var clipOwner = clippedItem.owner
            verify(local.x >= -1 && local.y >= -1
                   && local.x + item.width <= clipOwner.width + 1
                   && local.y + item.height <= clipOwner.height + 1,
                   item.objectName + " clipped by " + clipOwner.objectName
                   + ": " + local.x + "," + local.y + " " + item.width + "x" + item.height
                   + " in " + clipOwner.width + "x" + clipOwner.height)
        }
    }
    function revealVertically(item) {
        for (var pass = 0; pass < 2; ++pass) {
            for (var ancestor = item.parent; ancestor; ancestor = ancestor.parent) {
                if (ancestor.contentY !== undefined && ancestor.contentHeight !== undefined) {
                    var local = item.mapToItem(ancestor, 0, 0)
                    ancestor.contentY = Math.max(0, Math.min(ancestor.contentHeight - ancestor.height,
                        ancestor.contentY + local.y - 2))
                }
            }
            if (pass === 0) wait(0)
        }
    }
    function snapshot(page, name) {
        if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0)
            grabImage(page).save(visualFixtureOutput + "-" + name + ".png")
    }

    function test_small_pages_keep_primary_actions_reachable_data() {
        var pages = [
            {tag: "editor", file: "AudioEditorPage.qml", action: "editorRecordButton"},
            {tag: "metadata", file: "MetadataEditPage.qml", action: "metadataApplyButton"},
            {tag: "filename", file: "FilenameProcessPage.qml", action: "filenameStartButton"},
            {tag: "separation", file: "VocalSeparationPage.qml", action: "separationPrimaryAction"},
            {tag: "lossless", file: "LosslessIdentifyPage.qml", action: "losslessStartButton"}
        ]
        var cases = []
        for (var i = 0; i < pages.length; ++i) {
            for (var size of [[760, 332], [998, 442], [1065, 479], [1672, 712], [1672, 853]])
                cases.push({tag: pages[i].tag + "-" + size[0] + "x" + size[1], name: pages[i].tag,
                            file: pages[i].file, action: pages[i].action, w: size[0], h: size[1]})
        }
        return cases
    }
    function test_small_pages_keep_primary_actions_reachable(data) {
        var page = componentAt("components/tools/" + data.file).createObject(testCase,
            {width: data.w, height: data.h, visible: true})
        verify(page)
        try {
            waitForRendering(page)
            if (data.name === "metadata" && page.compactLayout
                    && !page.macStackedLayout) {
                var tab = findChild(page, "metadataCompactEditorTab")
                inside(tab, page)
                mouseClick(tab, tab.width / 2, tab.height / 2)
                waitForRendering(page)
            }
            snapshot(page, data.tag)
            var primaryAction = findChild(page, data.action)
            revealVertically(primaryAction)
            if (data.name === "metadata" && page.macStackedLayout) {
                var metadataScroller = findChild(page, "metadataWorkbenchScroller")
                metadataScroller.contentY = Math.max(0, metadataScroller.contentHeight - metadataScroller.height)
                if (data.h === 332) {
                    var metadataActionBar = findChild(page, "metadataActionBar")
                    var metadataInspector = findChild(page, "metadataInspectorPanel")
                    console.log("METADATA_LAYOUT", JSON.stringify({
                        pageHeight: page.height, inset: page.compactActionInset,
                        scrollHeight: metadataScroller.height,
                        scrollContentHeight: metadataScroller.contentHeight,
                        scrollY: metadataScroller.contentY,
                        inspectorY: metadataInspector.mapToItem(page, 0, 0).y,
                        inspectorHeight: metadataInspector.height,
                        actionBarY: metadataActionBar.mapToItem(page, 0, 0).y,
                        actionBarBottomMargin: metadataActionBar.anchors.bottomMargin,
                        actionY: primaryAction.mapToItem(page, 0, 0).y
                    }))
                }
            }
            inside(primaryAction, page)
            if (data.name === "editor") {
                revealVertically(findChild(page, "editorPlaybackTransport"))
                inside(findChild(page, "editorPlaybackTransport"), page)
                revealVertically(findChild(page, "editorRecordingTransport"))
                inside(findChild(page, "editorRecordingTransport"), page)
                var inspector = findChild(page, "editorInspector")
                revealVertically(inspector)
                if (page.macStackedLayout) {
                    compare(inspector.x, 0)
                    verify(inspector.y >= findChild(page, "editorMainColumn").height)
                } else {
                    inside(inspector, page)
                    compare(inspector.x, page.mainWidth)
                }
                verify(findChild(page, "editorTrackScroller").height >= 86)
                var exportButton = findChild(page, "editorExportButton")
                if (data.h === 712) {
                    var tracks = findChild(page, "editorTrackScroller")
                    verify(tracks.height >= tracks.contentHeight, "all six tracks fit the desktop default")
                    for (var trackIndex = 0; trackIndex < 6; ++trackIndex) {
                        var gain = findChild(page, "editorTrackGain" + trackIndex)
                        inside(gain, gain.parent)
                    }
                    inside(exportButton, page)
                }
                revealVertically(exportButton)
                inside(exportButton, page)
            }
            if (data.name === "filename") {
                var tabs = findChild(page, "filenameCompactTabs")
                if (tabs && tabs.visible) {
                    var settingsTab = findChild(page, "filenameCompactRulesTab")
                    mouseClick(settingsTab, settingsTab.width / 2, settingsTab.height / 2)
                    waitForRendering(page)
                }
                for (var name of ["filenamePrefixField", "filenameSuffixField", "filenameConflictBox", "filenameAutoNumberCheck", "filenameRemoveSequenceCheck"]) {
                    var field = findChild(page, name)
                    revealVertically(field)
                    snapshot(page, data.tag + "-" + name)
                    inside(field, page)
                    if (name === "filenamePrefixField" || name === "filenameSuffixField")
                        verify(field.width >= 120, "filename entry must remain usable: " + field.width)
                }
            }
            if (data.name === "metadata") {
                var filter = findChild(page, "metadataStatusFilter")
                inside(filter, page)
                verify(!filter.contentItem.truncated, "metadata status selection must be readable")
                var lastField = findChild(page, "metadataValueField_customTag")
                revealVertically(lastField)
                inside(lastField, page)
                inside(findChild(page, data.action), page)
            }
            if (data.name === "lossless") {
                var taskList = findChild(page, "losslessTaskList")
                verify(taskList.height >= 32, "lossless tasks need usable list space: " + taskList.height)
                var empty = findChild(page, "losslessEmptyState")
                if (empty.visible) inside(empty, taskList.parent)
            }
        } finally { page.destroy() }
    }

    function test_short_task_panel_keeps_list_space() {
        var panel = componentAt("components/tools/LosslessTaskPanel.qml").createObject(testCase,
            {width: 303, height: 162, compact: true, controller: LosslessAnalysisController})
        verify(panel)
        try {
            waitForRendering(panel)
            var list = findChild(panel, "losslessTaskList")
            verify(list.height >= 32, "lossless task list height=" + list.height)
            inside(findChild(panel, "losslessEmptyState"), list.parent)
        } finally { panel.destroy() }
    }

    function test_tool_window_can_shrink_without_filling_work_area() {
        var host = componentAt("AudioToolsWindow.qml").createObject(null, {visible: true})
        verify(host)
        try {
            waitForRendering(host.contentItem)
            var available = WindowController.availableGeometryForWindow(host)
            verify(host.height <= Math.min(800, Math.floor(available.height * 0.72)),
                   "default tools window should leave comfortable desktop space")
            verify(host.width < available.width && host.height < available.height,
                   "default tools window fills work area: " + host.width + "x" + host.height
                   + " vs " + available.width + "x" + available.height)
            host.width = 760
            host.height = 420
            waitForRendering(host.contentItem)
            compare(host.width, 760)
            compare(host.height, 420)
            var movedX = host.x + 24
            var movedY = host.y + 24
            host.x = movedX
            host.y = movedY
            compare(host.x, movedX)
            compare(host.y, movedY)
            inside(findChild(host, "audioToolsMoveArea"), host.contentItem)
            verify(findChild(host, "audioToolsResizeHandles").visible)
        } finally { host.close(); host.destroy() }
    }
}
