import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "ThemedDialogConfirmation"
    when: windowShown
    width: 640
    height: 480
    visible: true

    Rectangle {
        anchors.fill: parent
        color: Theme.contentSurface
        z: -1
    }

    SignalSpy { id: acceptedSpy; signalName: "accepted" }
    SignalSpy { id: rejectedSpy; signalName: "rejected" }
    Component {
        id: offsetWindowComponent
        ApplicationWindow { width: 860; height: 540; visible: true; color: Theme.contentSurface }
    }
    function test_tag_dialogs_center_on_owner_window_data() {
        return [{tag: "dark-dual", mode: 0, hostWidth: 860, hostHeight: 540},
                {tag: "light-dual", mode: 1, hostWidth: 860, hostHeight: 540},
                {tag: "dark-wide", mode: 0, hostWidth: 1280, hostHeight: 800},
                {tag: "light-small", mode: 1, hostWidth: 320, hostHeight: 260},
                {tag: "narrow-wrap", mode: 0, hostWidth: 248, hostHeight: 230}]
    }
    function test_tag_dialogs_center_on_owner_window(data) {
        failOnWarning(/Binding loop detected/)
        var previousMode = SettingsController.themeMode
        var host = createTemporaryObject(offsetWindowComponent, testCase)
        verify(host)
        host.width = data.hostWidth
        host.height = data.hostHeight
        host.requestActivate()
        tryCompare(host, "active", true)
        SettingsController.themeMode = data.mode
        var files = ["TagManagementPanel.qml", "TrackList.qml"]
        var names = [["addTagDialog", "renameTagDialog", "removeTagDialog"], ["trackTagDialog"]]
        try {
            for (var f = 0; f < files.length; ++f) {
                var component = Qt.createComponent(Qt.resolvedUrl(
                            "../../app/qml/AgPlayer/components/" + files[f]))
                compare(component.status, Component.Ready, component.errorString())
                var owner = component.createObject(host.contentItem,
                            {"x": host.width - 240, "y": 40,
                             "width": 240, "height": host.height - 80})
                verify(owner)
                try {
                    for (var n = 0; n < names[f].length; ++n) {
                        var dialog = findChild(owner, names[f][n])
                        verify(dialog)
                        dialog.open()
                        tryCompare(dialog, "opened", true)
                        waitForRendering(dialog.contentItem)
                        var frame = dialog.background.parent
                        var point = frame.mapToItem(host.contentItem, 0, 0)
                        verify(Math.abs(point.x + dialog.width / 2 - host.width / 2) <= 1,
                               names[f][n] + " must center in owner window; actual left=" + point.x
                               + " width=" + dialog.width + " overlayWidth=" + dialog.parent.width + " hostWidth=" + host.width)
                        verify(Math.abs(point.y + dialog.height / 2 - host.height / 2) <= 1,
                               names[f][n] + " must center vertically in owner window")
                        verify(point.x >= 0 && point.x + dialog.width <= host.width)
                        verify(point.y >= 0 && point.y + dialog.height <= host.height)
                        verify(dialog.height <= 220)
                        var cancel = dialog.standardButton(Dialog.Cancel)
                        verify(cancel.width <= 96, names[f][n] + " actions must retain compact width; actual=" + cancel.width)
                        if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0) {
                            waitForRendering(dialog.contentItem)
                            grabImage(host.contentItem).save(visualFixtureOutput + "-" + data.tag + "-" + names[f][n] + ".png")
                        }
                        dialog.reject()
                        tryCompare(dialog, "visible", false)
                    }
                } finally { owner.destroy() }
            }
        } finally { SettingsController.themeMode = previousMode }
    }
    function test_cache_confirmation_uses_themed_buttons_without_clearing_cache() {
        var component = Qt.createComponent(Qt.resolvedUrl(
                    "../../app/qml/AgPlayer/SettingsPage.qml"))
        compare(component.status, Component.Ready, component.errorString())
        var owner = component.createObject(testCase, {"width": 640, "height": 480})
        verify(owner)
        var previousTheme = SettingsController.themeMode
        var previousLanguage = SettingsController.language
        try {
            SettingsController.language = "zh"
            var dialog = findChild(owner, "clearCacheConfirmDialog")
            verify(dialog)
            for (var mode = 0; mode <= 1; ++mode) {
                SettingsController.themeMode = mode
                dialog.open()
                tryCompare(dialog, "opened", true)
                compare(dialog.width, 360)
                compare(dialog.contentItem.color.toString(), Theme.textPrimary.toString())
                compare(dialog.standardButton(Dialog.Yes).text, "是")
                var no = dialog.standardButton(Dialog.No)
                compare(no.text, "否")
                mouseClick(no, no.width / 2, no.height / 2)
                tryCompare(dialog, "visible", false)
            }
        } finally {
            SettingsController.themeMode = previousTheme
            SettingsController.language = previousLanguage
            owner.destroy()
        }
    }
    function test_all_tag_dialogs_share_compact_localized_controls() {
        testCase.Window.window.requestActivate()
        tryCompare(testCase.Window.window, "active", true)
        var files = ["TagManagementPanel.qml", "TrackList.qml"]
        var names = [["addTagDialog", "renameTagDialog", "removeTagDialog"],
                     ["trackTagDialog"]]
        for (var f = 0; f < files.length; ++f) {
            var component = Qt.createComponent(Qt.resolvedUrl(
                        "../../app/qml/AgPlayer/components/" + files[f]))
            compare(component.status, Component.Ready, component.errorString())
            var owner = component.createObject(testCase, {"width": 600, "height": 400})
            verify(owner)
            try {
                for (var n = 0; n < names[f].length; ++n) {
                    var dialog = findChild(owner, names[f][n])
                    verify(dialog, names[f][n])
                    dialog.open()
                    tryCompare(dialog, "opened", true)
                    verify(dialog.width >= 280 && dialog.width <= 300)
                    verify(dialog.height <= 220, "compact tag dialog")
                    compare(dialog.standardButton(Dialog.Cancel).text, "取消")
                    compare(dialog.standardButton(Dialog.Ok).text, "确定")
                    compare(dialog.header.color.toString(), Theme.textPrimary.toString())
                    if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0) {
                        waitForRendering(dialog.contentItem)
                        var saved = false
                        verify(dialog.background.parent.grabToImage(function(result) {
                            saved = result.saveToFile(visualFixtureOutput + "-" + names[f][n] + ".png")
                        }))
                        tryVerify(function() { return saved })
                    }
                    dialog.contentItem.forceActiveFocus()
                    keyClick(Qt.Key_Escape)
                    tryCompare(dialog, "visible", false)
                }
            } finally { owner.destroy() }
        }
    }
    Component {
        id: messageComponent
        Label {
            text: "确定删除这个歌单？音乐文件不会被删除。"
            color: Theme.textPrimary
            wrapMode: Text.Wrap
        }
    }

    function test_standard_buttons_are_localized_compact_and_keep_roles() {
        var component = Qt.createComponent(Qt.resolvedUrl(
                    "../../app/qml/AgPlayer/components/ThemedDialog.qml"))
        compare(component.status, Component.Ready, component.errorString())
        var dialog = component.createObject(testCase, {
            "title": "确认操作", "width": 360,
            "standardButtons": Dialog.Ok | Dialog.Cancel
        })
        verify(dialog)
        dialog.contentItem = createTemporaryObject(messageComponent, dialog.background.parent)
        dialog.x = (width - dialog.width) / 2
        dialog.y = (height - dialog.height) / 2
        acceptedSpy.target = dialog
        rejectedSpy.target = dialog
        acceptedSpy.clear()
        rejectedSpy.clear()
        try {
            dialog.open()
            tryCompare(dialog, "opened", true)
            compare(dialog.contentItem.parent, dialog.background.parent)
            compare(dialog.header.color.toString(), Theme.textPrimary.toString())
            var ok = dialog.standardButton(Dialog.Ok)
            var cancel = dialog.standardButton(Dialog.Cancel)
            tryCompare(ok, "text", "确定")
            compare(cancel.text, "取消")
            verify(ok.height <= Theme.controlHeight)
            verify(cancel.height <= Theme.controlHeight)
            verify(ok.width <= 96)
            verify(cancel.width <= 96)
            if (typeof testTranslationsEnabled !== "undefined" && testTranslationsEnabled) {
                SettingsController.language = "en"
                tryCompare(ok, "text", "OK")
                compare(cancel.text, "Cancel")
                SettingsController.language = "zh"
                tryCompare(ok, "text", "确定")
                compare(cancel.text, "取消")
            }
            if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length > 0) {
                waitForRendering(ok)
                var saved = false
                verify(dialog.background.parent.grabToImage(function(result) {
                    saved = result.saveToFile(visualFixtureOutput)
                }))
                tryVerify(function() { return saved })
            }
            mouseClick(cancel, cancel.width / 2, cancel.height / 2)
            compare(rejectedSpy.count, 1)
            compare(acceptedSpy.count, 0)
            tryCompare(dialog, "visible", false)
            dialog.open()
            tryCompare(dialog, "opened", true)
            waitForRendering(ok)
            mouseClick(ok, ok.width / 2, ok.height / 2)
            compare(acceptedSpy.count, 1)
            dialog.standardButtons = Dialog.Yes | Dialog.No | Dialog.Close
            dialog.open()
            tryCompare(dialog, "opened", true)
            tryCompare(dialog.standardButton(Dialog.Yes), "text", "是")
            compare(dialog.standardButton(Dialog.No).text, "否")
            compare(dialog.standardButton(Dialog.Close).text, "关闭")
        } finally {
            SettingsController.language = "zh"
            dialog.close()
            acceptedSpy.target = null
            rejectedSpy.target = null
            dialog.destroy()
        }
    }
}
