import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import AgPlayer

TestCase {
    id: suite
    name: "SettingsPrivacy"
    when: windowShown

    Component {
        id: settingsHost
        Window {
            width: 856
            height: 938
            visible: true
            color: Theme.background
            SettingsPage { objectName: "privacySettingsPage"; visible: true; selectedSection: 6 }
        }
    }

    function test_aboutAndScrollablePolicy_data() {
        return [{tag: "reference", width: 856, height: 938},
                {tag: "compact", width: 700, height: 560}]
    }

    function test_aboutAndScrollablePolicy(data) {
        const host = createTemporaryObject(settingsHost, null, {width: data.width, height: data.height})
        verify(host)
        host.requestActivate()
        const page = findChild(host, "privacySettingsPage")
        verify(waitForRendering(page))
        const product = findChild(page, "aboutProductInfo")
        const update = findChild(page, "aboutUpdateInfo")
        const button = findChild(page, "aboutPrivacyPolicyButton")
        verify(product && update && button)
        if (data.tag === "reference") {
            verify(update.mapToItem(page, 0, 0).x > product.mapToItem(page, 0, 0).x)
            verify(button.mapToItem(page, 0, 0).y > update.mapToItem(page, 0, update.height).y)
            if (visualFixtureOutput.length > 0)
                grabImage(host.contentItem).save(visualFixtureOutput + "-about.png")
        }
        mouseClick(button, button.width / 2, button.height / 2)
        const dialog = findChild(page, "privacyPolicyDialog")
        tryCompare(dialog, "opened", true)
        compare(dialog.title, "AgPlayer 隐私政策")
        verify(dialog.width < host.width && dialog.height < host.height)
        const scroll = findChild(dialog, "privacyPolicyScroll")
        const body = findChild(dialog, "privacyPolicyBody")
        verify(scroll && body && body.readOnly)
        verify(body.text.indexOf("一、本地数据与权限") >= 0)
        verify(body.text.indexOf("五、安全、未成年人及政策更新") >= 0)
        verify(body.text.indexOf("mailto:agplayer@foxmail.com") >= 0)
        verify(body.text.indexOf("https://github.com/gddjag/AgPlayer/releases") >= 0)
        const plain = body.getText(0, body.length)
        const linkPosition = plain.indexOf("UVR")
        verify(linkPosition > 0)
        const rect = body.positionToRectangle(linkPosition + 1)
        compare(body.linkAt(rect.x + 1, rect.y + rect.height / 2),
                "https://github.com/Anjok07/ultimatevocalremovergui")
        verify(scroll.contentItem.contentHeight > scroll.availableHeight)
        if (data.tag === "reference" && visualFixtureOutput.length > 0)
            grabImage(host.contentItem).save(visualFixtureOutput + "-dialog.png")
        mouseWheel(scroll, scroll.width / 2, scroll.height / 2, 0, -480)
        tryVerify(function() { return scroll.contentItem.contentY > 0 })
        const closeButton = dialog.standardButton(Dialog.Close)
        verify(closeButton && closeButton.visible)
        mouseClick(closeButton, closeButton.width / 2, closeButton.height / 2)
        tryCompare(dialog, "visible", false)
        verify(page.visible)
        mouseClick(button, button.width / 2, button.height / 2)
        tryCompare(dialog, "opened", true)
        compare(scroll.contentItem.contentY, 0)
        keyClick(Qt.Key_Escape)
        tryCompare(dialog, "visible", false)
        verify(page.visible)
        host.close()
    }
}
