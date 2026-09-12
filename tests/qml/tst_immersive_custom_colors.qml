import QtQuick
import QtQuick.Controls
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "ImmersiveCustomColors"
    when: windowShown
    visible: true
    width: 960
    height: 760

    ImmersiveSurface {
        id: surface
        anchors.fill: parent
        attached: true
        hostExposed: true
        qaSyntheticFeatures: true
    }

    function init() {
        PlayerExperienceController.applyTheme("nocturnal")
        PlayerExperienceController.immersiveMode = PlayerExperienceController.TerrainReactor
        surface.revealPanelFromHotCorner()
        PlayerExperienceController.autoRotate = 0
        PlayerExperienceController.topographyDensity = 10
        PlayerExperienceController.floatingCubesEnabled = false
        PlayerExperienceController.meteorsEnabled = false
        PlayerExperienceController.idleBreathingEnabled = false
    }

    function cleanup() {
        PlayerExperienceController.immersiveMode = PlayerExperienceController.Off
    }

    function test_picker_stays_interactive_past_panel_idle_timeout() {
        var field = findChild(surface, "immersiveColorField0")
        verify(field)
        mouseClick(field, field.width / 2, field.height / 2)
        var picker = findChild(field, "colorFieldPicker")
        tryCompare(picker, "opened", true)
        wait(5300)
        compare(surface.panelAutoHidden, false, "Opening a modal picker must suspend panel auto-hide")
        var cancel = findChild(field, "colorPickerCancelButton")
        verify(cancel.enabled, "A hidden/disabled owner must not strand the modal popup")
        mouseClick(cancel)
        tryCompare(picker, "visible", false)
        var idle = findChild(surface, "immersivePanelAutoHideTimer")
        verify(idle.running, "Auto-hide must resume after closing the picker")
    }

    function accelerated() {
        return testCase.GraphicsInfo.api !== GraphicsInfo.Software
    }

    function test_all_five_colors_preview_cancel_confirm_and_keep_rendering() {
        var keys = ["coolColor", "warmColor", "accentColor", "peakColor", "baseColor"]
        var colors = ["#21ED43", "#F12243", "#31DAFF", "#EDDD24", "#151023"]
        var terrain = surface.terrainItem
        verify(terrain)
        if (accelerated()) {
            tryCompare(terrain, "renderStatus", TerrainReactorItem.Ready, 5000)
            terrain.setSyntheticFeatures([0.9,0.85,0.7,0.5,0.5,0.6,0.5,0.4], 0.7,0.6,false,false)
        }
        for (var i = 0; i < keys.length; ++i) {
            var field = findChild(surface, "immersiveColorField" + i)
            var original = PlayerExperienceController.customColors[keys[i]]
            mouseClick(field, field.width / 2, field.height / 2)
            var picker = findChild(field, "colorFieldPicker")
            tryCompare(picker, "opened", true)
            var frames = terrain.frameCount
            if (i === 0) {
                var plane = findChild(field, "colorPickerSaturationValue")
                mousePress(plane, plane.width * .2, plane.height * .6)
                mouseMove(plane, plane.width * .8, plane.height * .2, 30)
                mouseRelease(plane, plane.width * .8, plane.height * .2)
                compare(PlayerExperienceController.coolColor,
                        picker.workingColor.toString().toUpperCase())
                verify(PlayerExperienceController.coolColor !== original)
            }
            verify(picker.applyHexText(colors[i]))
            compare(PlayerExperienceController.themeId, "custom")
            compare(PlayerExperienceController[keys[i]], colors[i])
            compare(PlayerExperienceController.customColors[keys[i]], original)
            if (accelerated())
                tryVerify(function() { return terrain.frameCount > frames + 3 }, 3000,
                          "GPU must continue rendering while editing " + keys[i])
            mouseClick(findChild(field, "colorPickerCancelButton"))
            tryCompare(picker, "visible", false)
            compare(PlayerExperienceController[keys[i]], original)
            mouseClick(field, field.width / 2, field.height / 2)
            tryCompare(picker, "opened", true)
            picker.applyHexText(colors[i])
            mouseClick(findChild(field, "colorPickerConfirmButton"))
            tryCompare(picker, "visible", false)
            compare(PlayerExperienceController.customColors[keys[i]], colors[i])
        }
        PlayerExperienceController.applyTheme("nocturnal")
        for (var j = 0; j < keys.length; ++j)
            compare(findChild(surface, "immersiveColorField" + j).colorValue.toString().toUpperCase(), colors[j])
        mouseClick(findChild(surface, "immersiveCustomColorsButton"))
        compare(PlayerExperienceController.themeId, "custom")
        for (var k = 0; k < keys.length; ++k)
            compare(PlayerExperienceController[keys[k]], colors[k])
    }

    function test_violet_heart_native_capture() {
        if (!accelerated()) skip("Native GPU capture requires hardware rendering")
        PlayerExperienceController.applyTheme("violet-heart")
        PlayerExperienceController.restoreDynamicDefaults()
        PlayerExperienceController.topographyDensity = 46
        PlayerExperienceController.autoRotate = 0
        surface.panelAutoHidden = true
        var terrain = surface.terrainItem
        tryCompare(terrain, "renderStatus", TerrainReactorItem.Ready, 5000)
        terrain.setSyntheticFeatures([0.8,0.7,0.5,0.3,0.4,0.5,0.2,0.2], 0.65,0.5,true,false)
        var frames = terrain.frameCount
        tryVerify(function() { return terrain.frameCount > frames + 12 }, 3000)
        wait(1200) // Let the authored palette settle and the panel fade out.
        var path = decodeURIComponent(Qt.resolvedUrl("../../build/qa/custom-violet-heart.png").toString().replace("file:///", ""))
        if (Qt.platform.os !== "windows") path = "/" + path
        var image = grabImage(surface)
        compare(image.width, surface.width)
        image.save(path)
    }
}
