import QtQuick
import QtQuick.Controls
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
                    compare(dialog.width, 320)
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
                    dialog.reject()
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
