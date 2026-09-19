import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "PopupContentSizing"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Rectangle { anchors.fill: parent; color: Theme.contentSurface; z: -1 }
    Component { id: message; Label { text: "确认？"; wrapMode: Text.Wrap } }
    ListModel { id: playlists }

    function init() { failOnWarning(/Binding loop detected/) }

    function componentAt(file) {
        var component = Qt.createComponent("qrc:/qt/qml/AgPlayer/qml/AgPlayer/" + file)
        if (component.status === Component.Loading)
            tryCompare(component, "status", Component.Ready)
        compare(component.status, Component.Ready, component.errorString())
        return component
    }

    function test_menu_item_follows_text_and_indicator_padding() {
        var item = componentAt("components/ThemedMenuItem.qml").createObject(testCase, {text: "播放"})
        verify(item)
        try {
            compare(item.implicitWidth, Math.ceil(item.contentItem.implicitWidth + item.leftPadding + item.rightPadding))
            var shortWidth = item.implicitWidth
            item.text = "A longer translated menu command"
            verify(item.implicitWidth > shortWidth)
            item.checkable = true
            compare(item.implicitWidth, Math.ceil(item.contentItem.implicitWidth + item.leftPadding + item.rightPadding))
        } finally { item.destroy() }
    }

    function test_text_dialog_fits_content_then_wraps_at_owner_boundary() {
        failOnWarning(/Binding loop detected/)
        var dialog = componentAt("components/ThemedDialog.qml").createObject(testCase,
            { title: "确认", standardButtons: Dialog.Ok | Dialog.Cancel })
        verify(dialog)
        dialog.contentItem = createTemporaryObject(message, dialog.background.parent)
        dialog.anchors.centerIn = testCase
        try {
            dialog.open()
            tryCompare(dialog, "opened", true)
            verify(dialog.width < 320, "short confirmation must not retain a wide fixed panel")
            var shortWidth = dialog.width
            dialog.contentItem.text = "这是较长的确认信息，需要根据真实内容确定宽度，并在窗口边界内完整换行显示。".repeat(5)
            wait(0)
            verify(dialog.width > shortWidth)
            verify(dialog.width <= testCase.width - 2 * Theme.spacingLg)
            verify(dialog.contentItem.implicitHeight > Theme.fontSizeBody * 2)
            if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0)
                grabImage(testCase.Window.window.contentItem).save(visualFixtureOutput + "-message.png")
        } finally { dialog.close(); dialog.destroy() }
    }

    function test_dynamic_playlist_menu_fits_long_names() {
        playlists.clear()
        playlists.append({playlistId: "one", name: "短歌单"})
        var owner = componentAt("components/TrackList.qml").createObject(testCase,
            {width: 640, height: 400, playlistModel: playlists})
        verify(owner)
        try {
            var menu = findChild(owner, "moveTracksMenu")
            verify(menu)
            tryCompare(menu, "count", 1)
            var parentMenu = findChild(owner, "trackContextMenu")
            parentMenu.open()
            tryCompare(parentMenu, "opened", true)
            waitForRendering(parentMenu.contentItem)
            for (var index = 0; index < parentMenu.count; ++index) {
                var action = parentMenu.itemAt(index)
                if (action && action.text !== undefined)
                    verify(!action.contentItem.truncated, "truncated menu action: " + action.text)
            }
            menu.open()
            tryCompare(menu, "opened", true)
            var shortWidth = menu.width
            playlists.setProperty(0, "name", "A long playlist name that must remain readable")
            wait(0)
            verify(menu.width > shortWidth)
            verify(menu.width >= menu.itemAt(0).implicitWidth + menu.leftPadding + menu.rightPadding)
            if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0)
                grabImage(testCase.Window.window.contentItem).save(visualFixtureOutput + "-playlist-menu.png")
            menu.close()
            parentMenu.close()
        } finally { owner.destroy(); playlists.clear() }
    }

    function test_tag_menu_has_no_fixed_empty_width() {
        var owner = componentAt("components/TagManagementPanel.qml").createObject(testCase,
            {width: 320, height: 300})
        verify(owner)
        try {
            var menu = findChild(owner, "tagContextMenu")
            verify(menu)
            menu.open()
            tryCompare(menu, "opened", true)
            var required = 0
            for (var index = 0; index < menu.count; ++index) {
                var item = menu.itemAt(index)
                if (item && item.text !== undefined)
                    required = Math.max(required, item.implicitWidth)
            }
            verify(Math.abs(menu.width - required - menu.leftPadding - menu.rightPadding) <= 1)
            verify(menu.width < 190)
            menu.close()
        } finally { owner.destroy() }
    }

    function test_classic_search_footer_keeps_bottom_breathing_room() {
        var host = componentAt("ListWindow.qml").createObject(null, {visible: true})
        verify(host)
        try {
            var search = findChild(host, "librarySearchFilter")
            verify(search)
            waitForRendering(search)
            var bottom = search.mapToItem(host.contentItem, 0, search.height).y
            var gap = host.height - bottom
            verify(gap >= Theme.spacingSm, "classic search bottom gap=" + gap)
            verify(gap <= Theme.spacingLg + 4, "only a small footer gutter is needed")
            if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0)
                grabImage(host.contentItem).save(visualFixtureOutput + "-classic-footer.png")
        } finally { host.close(); host.destroy() }
    }

    function test_text_popup_callers_open_without_layout_feedback_data() {
        return [
            {tag: "track-list", file: "components/TrackList.qml", window: false,
             titles: ["彻底删除至回收站", "部分文件未删除"]},
            {tag: "playlist", file: "ListWindow.qml", window: true, titles: ["删除歌单"]},
            {tag: "metadata", file: "components/tools/MetadataEditPage.qml", window: false,
             titles: ["三态编辑说明", "元数据处理提示", "预检发现不支持项"]},
            {tag: "filename", file: "components/tools/FilenameProcessPage.qml", window: false,
             titles: ["确认覆盖现有文件"]},
            {tag: "editor", file: "components/tools/AudioEditorPage.qml", window: false,
             titles: ["舍弃未保存更改？"]},
            {tag: "tools-window", file: "AudioToolsWindow.qml", window: true,
             titles: ["舍弃未保存更改？"]}
        ]
    }

    function test_text_popup_callers_open_without_layout_feedback(data) {
        var owner = componentAt(data.file).createObject(data.window ? null : testCase,
            {visible: true, width: 640, height: 480})
        verify(owner)
        try {
            var children = data.window ? owner.contentItem.data : owner.data
            for (var titleIndex = 0; titleIndex < data.titles.length; ++titleIndex) {
                var dialog = null
                for (var childIndex = 0; childIndex < children.length; ++childIndex) {
                    var child = children[childIndex]
                    if (child && child.title === data.titles[titleIndex]
                            && typeof child.open === "function") {
                        dialog = child
                        break
                    }
                }
                verify(dialog, data.file + ": " + data.titles[titleIndex])
                dialog.open()
                tryCompare(dialog, "opened", true)
                waitForRendering(dialog.contentItem)
                verify(dialog.width > 0)
                verify(dialog.width <= dialog.parent.width - 2 * Theme.spacingLg,
                       data.file + " dialog=" + dialog.width + " parent=" + dialog.parent.width)
                verify(dialog.contentItem.width >= 0)
                if (data.tag === "metadata" && titleIndex === 1) {
                    dialog.contentItem.text = "C:/Music/" + "a".repeat(240) + ".flac"
                    waitForRendering(dialog.contentItem)
                    verify(dialog.contentItem.implicitHeight > Theme.fontSizeBody * 2,
                           "long metadata error paths must wrap within the dialog")
                    verify(dialog.contentItem.contentWidth <= dialog.contentItem.width + 1,
                           "metadata error text exceeds the available width: "
                           + dialog.contentItem.contentWidth + "/" + dialog.contentItem.width)
                }
                if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0
                        && data.tag === "metadata")
                    grabImage(testCase.Window.window.contentItem).save(
                        visualFixtureOutput + "-" + data.tag + "-" + titleIndex + ".png")
                dialog.close()
            }
        } finally { if (data.window) owner.close(); owner.destroy() }
    }
}
