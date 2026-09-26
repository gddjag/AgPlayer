import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: suite
    name: "SettingsCacheFeedback"
    when: windowShown
    SignalSpy { id: clearSpy; target: SettingsController; signalName: "cacheClearStateChanged" }
    Component {
        id: settingsHost
        Window {
            width: 1040; height: 820
            visible: true
            SettingsPage {
                objectName: "feedbackSettingsPage"
                visible: true
                selectedSection: 5
            }
        }
    }

    function test_clearActionsExposeActualResult_data() {
        return [
            {tag: "waveforms", button: "clearWaveformCacheButton"},
            {tag: "covers", button: "clearCoverCacheButton"},
            {tag: "all", button: "clearAllCacheButton"}
        ]
    }

    function test_clearActionsExposeActualResult(data) {
        const host = createTemporaryObject(settingsHost, null)
        verify(host)
        host.requestActivate()
        const page = findChild(host, "feedbackSettingsPage")
        verify(page)
        verify(waitForRendering(page))
        const status = findChild(page, "cacheClearStatusText")
        verify(status, "Single-action cleanup needs visible result feedback")
        const button = findChild(page, data.button)
        verify(button && button.enabled)
        const center = button.mapToItem(host.contentItem, button.width / 2, button.height / 2)
        verify(center.x > 0 && center.x < host.width && center.y > 0 && center.y < host.height)
        clearSpy.clear()
        mouseClick(button, button.width / 2, button.height / 2, Qt.LeftButton)
        if (data.tag === "all") {
            const confirmation = findChild(page, "clearCacheConfirmDialog")
            tryCompare(confirmation, "visible", true)
            compare(clearSpy.count, 0)
            const yes = confirmation.standardButton(Dialog.Yes)
            verify(yes && yes.visible && yes.enabled)
            mouseClick(yes, yes.width / 2, yes.height / 2, Qt.LeftButton)
        }
        tryVerify(function() { return clearSpy.count > 0 })
        tryCompare(SettingsController, "cacheClearBusy", false)
        tryVerify(function() { return status.visible && status.text.length > 0 })
        compare(status.text, SettingsController.cacheClearStatus)
        verify(button.enabled)
        verify(!findChild(page, "clearTempCacheButton"), "Job-owned temporary files are not a shared cache")
        host.close()
    }
}
