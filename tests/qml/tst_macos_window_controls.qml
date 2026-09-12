import QtQuick
import QtQuick.Controls
import QtTest
import AgPlayer

Item {
Window {
    id: target
    visible: true
    width: 640
    height: 120
TestCase {
    id: testCase
    name: "MacWindowControls"
    when: windowShown
    visible: true
    width: 640
    height: 120
    property bool captured: false

    Rectangle {
        id: preview
        width: 640
        height: Theme.titleBarHeight
        color: Theme.titleBarSurface
        ThemedMacWindowControls {
            id: controls
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            visible: true // Exercise Mac controls on both build hosts.
            targetWindow: target
        }
        Text {
            anchors.centerIn: parent
            text: "AgPlayer · macOS"
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
            color: Theme.primaryText
        }
    }
    SignalSpy { id: closeSpy; target: controls; signalName: "closeRequested" }

    function init() {
        closeSpy.clear()
        controls.allowFullScreen = true
        target.showNormal()
        target.requestActivate()
        wait(80)
    }
    function cleanup() { target.showNormal() }

    function test_close_preserves_owner_policy_and_keyboard_access() {
        const button = findChild(controls, "macCloseButton")
        verify(button)
        compare(button.width, Theme.minimumInteractionExtent)
        verify(button.Accessible.name.length > 0)
        mouseClick(button)
        compare(closeSpy.count, 1)
        verify(target.visible) // The control does not bypass the owner's confirmation.
        button.focus = false
        target.contentItem.forceActiveFocus()
        target.requestActivate()
        verify(nativeDropHelper.sendKey(target, Qt.Key_Tab))
        tryVerify(function() { return button.visualFocus })
        verify(nativeDropHelper.sendKey(target, Qt.Key_Space))
        compare(closeSpy.count, 2)
    }
    function test_window_actions_and_disabled_full_screen() {
        const minimize = findChild(controls, "macMinimizeButton")
        const fullScreen = findChild(controls, "macFullScreenButton")
        mouseClick(minimize)
        compare(target.visibility, Window.Minimized)
        target.showNormal()
        controls.allowFullScreen = false
        verify(!fullScreen.enabled)
        mouseClick(fullScreen)
        compare(target.visibility, Window.Windowed)
        controls.allowFullScreen = true
        mouseClick(fullScreen)
        compare(target.visibility, Window.FullScreen)
        mouseClick(fullScreen)
        compare(target.visibility, Window.Windowed)
        compare(closeSpy.count, 0)
    }

    function test_render_light_and_dark_headers() {
        const original = SettingsController.themeMode
        try {
            for (let mode = 0; mode < 2; ++mode) {
                SettingsController.themeMode = mode
                mouseMove(controls, 14, 14)
                wait(80)
                captured = false
                const path = decodeURIComponent(Qt.resolvedUrl("../../build/macos-ui-titlebar-" + mode + ".png")
                    .toString().replace(/^file:\/\//, "").replace(/^\/([A-Za-z]:)/, "$1"))
                preview.grabToImage(function(result) { captured = result.saveToFile(path) }, Qt.size(1280, 80))
                tryVerify(function() { return captured }, 5000)
            }
        } finally { SettingsController.themeMode = original }
    }
}
}
}
