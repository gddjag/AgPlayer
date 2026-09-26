import QtQuick
import QtQuick.Controls
import QtTest
import AgPlayer

TestCase {
    id: suite
    name: "AuxiliaryResponsive"
    when: windowShown
    Component { id: settingsComponent; SettingsWindow {} }
    Component { id: equalizerComponent; EqualizerWindow {} }
    Component { id: immersiveComponent; ImmersiveWindow { renderingEnabled: false } }
    Component { id: miniComponent; MiniPlayerWindow {} }

    function inside(item, host) {
        verify(item && item.visible, "Control must be visible")
        var p = item.mapToItem(host.contentItem, 0, 0)
        var q = item.mapToItem(host.contentItem, item.width, item.height)
        verify(p.x >= -1 && p.y >= -1 && q.x <= host.width + 1
               && q.y <= host.height + 1, item.objectName + " outside "
               + host.width + "x" + host.height + ": " + p + " .. " + q)
    }

    function test_settings_resize_data() {
        return [{tag: "laptop", width: 760, height: 530},
                {tag: "compact", width: 720, height: 420}]
    }
    function test_settings_resize(data) {
        var host = createTemporaryObject(settingsComponent, null)
        verify(host)
        verify(host.minimumWidth <= data.width)
        verify(host.minimumHeight <= data.height)
        host.width = data.width
        host.height = data.height
        host.openSettings()
        var page = findChild(host, "settingsPage")
        verify(waitForRendering(page))
        for (var section = 0; section < 7; ++section) {
            page.selectedSection = section
            wait(0)
            inside(findChild(page, "settingsSaveButton"), host)
            inside(findChild(page, "settingsCancelButton"), host)
            var scroll = findChild(page, "settingsScroll")
            compare(scroll.contentWidth, scroll.availableWidth)
            var column = findChild(page, "settingsContentColumn")
            var right = column.mapToItem(host.contentItem, column.width, 0)
            verify(right.x <= host.width, "Settings must not need horizontal scrolling")
        }
        page.selectedSection = 2
        wait(0)
        var light = findChild(page, "themeModeLight")
        inside(light, host)
        mouseClick(light)
        compare(SettingsController.themeMode, 1)
        wait(180) // Allow existing theme color transitions to settle for capture.
        if (visualFixtureOutput)
            grabImage(host.contentItem).save(visualFixtureOutput + "-settings-" + data.tag + ".png")
        page.cancelAndClose()
    }

    function test_equalizer_resize_data() {
        return [{tag: "laptop", width: 998, height: 530},
                {tag: "compact", width: 760, height: 420},
                {tag: "reference", width: 1080, height: 620}]
    }
    function test_equalizer_resize(data) {
        var host = createTemporaryObject(equalizerComponent, null)
        verify(host)
        // Isolate content from the native minimum in the red reproduction.
        host.minimumWidth = 0
        host.minimumHeight = 0
        host.width = data.width
        host.height = data.height
        host.visible = true
        verify(waitForRendering(host.contentItem))
        for (var name of ["equalizerSaveButton", "equalizerManageButton",
                          "equalizerResetButton", "equalizerPreampSlider",
                          "equalizerOutputLevelText", "equalizerPrecisionLowButton"])
            inside(findChild(host, name), host)
        var preamp = findChild(host, "equalizerPreampSlider-control")
        inside(preamp, host)
        var precision = EqualizerController.precisionMode
        mouseClick(findChild(host, "equalizerPrecisionLowButton"))
        compare(EqualizerController.precisionMode, "low")
        EqualizerController.setPrecisionMode(precision)
        mouseClick(findChild(host, "equalizerManageButton"))
        var popup = findChild(host, "equalizerManagePopup")
        tryCompare(popup, "visible", true)
        verify(popup.y >= 0 && popup.y + popup.height <= host.height)
        popup.close()
        if (visualFixtureOutput)
            grabImage(host.contentItem).save(visualFixtureOutput + "-eq-" + data.tag + ".png")
        host.hide()
    }

    function test_immersive_resize_data() {
        return [{tag: "laptop", width: 998, height: 530},
                {tag: "compact", width: 760, height: 420}]
    }
    function test_immersive_resize(data) {
        var host = createTemporaryObject(immersiveComponent, null)
        verify(host)
        verify(host.minimumWidth <= data.width)
        verify(host.minimumHeight <= data.height)
        host.width = data.width
        host.height = data.height
        host.visible = true
        var panel = findChild(host, "immersiveControlPanelHost")
        var savedPanelVisible = PlayerExperienceController.panelVisible
        PlayerExperienceController.panelVisible = true
        // This fixture exercises layout with rendering disabled, not the
        // activation/hot-corner path (covered by immersive integration tests).
        host.surfaceItem.panelAutoHidden = false
        tryCompare(panel, "opacity", 1)
        for (var tab = 0; tab < 3; ++tab) {
            panel.currentTab = tab
            wait(220)
            inside(panel, host)
            var scroll = findChild(panel, "immersivePanelScroll")
            inside(scroll, host)
            compare(scroll.contentWidth, scroll.availableWidth)
        }
        PlayerExperienceController.panelVisible = savedPanelVisible
        host.hide()
    }

    function test_mini_resize() {
        var host = createTemporaryObject(miniComponent, null)
        verify(host)
        verify(host.minimumWidth <= 480)
        verify(host.minimumHeight <= 160)
        host.width = 480
        host.height = 160
        host.visible = true
        verify(waitForRendering(host.contentItem))
        var controls = findChild(host, "miniPlayerControls")
        for (var control of [host.closeButton, host.restoreButton,
                             controls.playPauseButton, controls.nextButton,
                             controls.muteButton])
            inside(control, host)
        if (visualFixtureOutput)
            grabImage(host.contentItem).save(visualFixtureOutput + "-mini.png")
        host.hide()
    }
}
