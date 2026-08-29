import QtQuick
import QtQuick.Controls
import QtTest
import AgPlayer
import "../../app/qml/AgPlayer/components/ColorScale.js" as ColorScale

TestCase {
    id: testCase
    name: "AgColorPicker"
    when: windowShown
    width: 860
    height: 900

    property int savedThemeMode: 0
    property string savedWaveformSolidBaseColor: ""
    property var savedThemeChoices: ({})

    Item {
        id: testHost
        anchors.fill: parent

        AgColorPicker {
            id: picker
            parent: testHost
            x: Math.round((testHost.width - picker.width) / 2)
            y: 28
        }

        ColorField {
            id: integratedField
            parent: testCase.Window.window
                    ? testCase.Window.window.contentItem : null
            x: Math.round(((parent ? parent.width : 0) - width) / 2)
            y: (parent ? parent.height : 0) - height - 28
            colorValue: SettingsController.waveformSolidBaseColor
            targetProperty: "waveformSolidBaseColor"
        }

        ThemeColorSelector {
            id: skinSelector
            parent: testHost
            x: 18
            y: 350
            objectNamePrefix: "skinSelector"
            title: "Theme skin color"
            selectedMode: SettingsController.skinColorMode
            selectedPreset: SettingsController.skinPreset
            customColor: SettingsController.skinCustomColor
            onDefaultRequested: SettingsController.skinColorMode = 0
            onPresetRequested: function(preset) {
                SettingsController.skinPreset = preset
                SettingsController.skinColorMode = 1
            }
            onCustomRequested: function(color) {
                SettingsController.skinCustomColor = color
                SettingsController.skinColorMode = 2
            }
        }

        Item {
            x: 18
            y: 600
            width: 680
            height: 32

            ThemedSwitch {
                id: representativeSwitch
                objectName: "representativeThemeSwitch"
                checked: true
            }
            ThemedCheckBox {
                id: representativeCheckBox
                objectName: "representativeThemeCheckBox"
                x: 60
                checked: true
            }
            ThemedRangeSlider {
                id: representativeRangeSlider
                objectName: "representativeThemeRangeSlider"
                x: 110
                width: 180
                first.value: 0.25
                second.value: 0.75
            }
            ThemedComboBox {
                id: representativeComboBox
                objectName: "representativeThemeComboBox"
                x: 310
                width: 160
                model: ["A", "B"]
            }
        }
    }

    SignalSpy {
        id: appliedSpy
        target: picker
        signalName: "applied"
    }

    SignalSpy {
        id: cancelledSpy
        target: picker
        signalName: "cancelled"
    }

    SignalSpy {
        id: integratedEditedSpy
        target: integratedField
        signalName: "colorEdited"
    }

    SignalSpy {
        id: paletteChangedSpy
        target: ThemeManager
        signalName: "paletteChanged"
    }

    function initTestCase() {
        savedThemeMode = SettingsController.themeMode
    }

    function init() {
        picker.close()
        appliedSpy.clear()
        cancelledSpy.clear()
        integratedEditedSpy.clear()
        paletteChangedSpy.clear()
        savedWaveformSolidBaseColor = SettingsController.waveformSolidBaseColor
        savedThemeChoices = {
            mode: SettingsController.skinColorMode,
            preset: SettingsController.skinPreset,
            customColor: SettingsController.skinCustomColor
        }
        SettingsController.themeMode = 0
        wait(0)
    }

    function cleanup() {
        picker.close()
        var integratedPicker = findChild(integratedField, "colorFieldPicker")
        if (integratedPicker)
            integratedPicker.close()
        SettingsController.waveformSolidBaseColor = savedWaveformSolidBaseColor
        SettingsController.skinColorMode = savedThemeChoices.mode
        SettingsController.skinPreset = savedThemeChoices.preset
        SettingsController.skinCustomColor = savedThemeChoices.customColor
        appliedSpy.clear()
        cancelledSpy.clear()
        integratedEditedSpy.clear()
        paletteChangedSpy.clear()
        SettingsController.themeMode = savedThemeMode
        wait(0)
    }

    function normalizedColor(value) {
        return ColorScale.normalizeHex(value)
    }

    function typeText(input, text) {
        testCase.Window.window.requestActivate()
        tryCompare(testCase.Window.window, "active", true)
        mouseClick(input)
        input.forceActiveFocus()
        verify(input.activeFocus)
        input.selectAll()
        keyClick(Qt.Key_Backspace)
        for (var index = 0; index < text.length; ++index)
            keyClick(text.charAt(index))
    }

    function replaceText(input, text) {
        typeText(input, text)
        keyClick(Qt.Key_Return)
        wait(0)
    }

    function openReferenceColor() {
        picker.openForColor("#63316B")
        tryCompare(picker, "visible", true)
    }

    function verifyMappedInside(item, boundary, margin) {
        var topLeft = item.mapToItem(boundary, 0, 0)
        var bottomRight = item.mapToItem(boundary, item.width, item.height)
        verify(topLeft.x >= margin)
        verify(topLeft.y >= margin)
        verify(bottomRight.x <= boundary.width - margin)
        verify(bottomRight.y <= boundary.height - margin)
    }

    function test_normalization_and_rgb_round_trip() {
        compare(ColorScale.normalizeHex("63316b"), "#63316B")
        compare(ColorScale.normalizeHex("#abc"), "#AABBCC")
        compare(ColorScale.normalizeHex("not-a-color"), "")
        compare(ColorScale.rgbToHex(99, 49, 107), "#63316B")
        var rgb = ColorScale.hexToRgb("#63316B")
        compare(rgb.r, 99)
        compare(rgb.g, 49)
        compare(rgb.b, 107)
    }

    function test_reference_palette_is_exact() {
        var expected = [
            "#F8EBFA", "#E9D2EC", "#D6B9DB", "#C09CC6", "#A76BB0",
            "#63316B", "#512C57", "#432248", "#341938", "#251028"
        ]
        var actual = ColorScale.buildPalette("#63316B")
        compare(actual.length, expected.length)
        compare(actual.join(","), expected.join(","))
    }

    function test_picker_compact_size_hex_rgb_apply_cancel_and_escape() {
        picker.openForColor("#22C55E", qsTr("Start color"))
        tryCompare(picker, "visible", true)
        verify(picker.width <= 292)
        verify(picker.height <= 248)
        verify(!findChild(picker, "colorPickerSystemDialog"))

        var hex = findChild(picker, "colorPickerHex")
        var apply = findChild(picker, "colorPickerApply")
        verify(hex && apply)
        hex.text = "#73A6FF"
        hex.forceActiveFocus()
        keyClick(Qt.Key_Enter)
        compare(appliedSpy.count, 0)
        compare(normalizedColor(picker.workingColor), "#73A6FF")
        mouseClick(apply, apply.width / 2, apply.height / 2)
        compare(appliedSpy.count, 1)
        compare(appliedSpy.signalArguments[0][0].toString().toUpperCase(), "#73A6FF")

        picker.openForColor("#A98BFF", qsTr("Middle color"))
        keyClick(Qt.Key_Escape)
        compare(cancelledSpy.count, 1)
        compare(appliedSpy.count, 1)
    }

    function test_rgb_fields_apply_without_intermediate_signal() {
        openReferenceColor()
        var red = findChild(picker, "colorPickerR")
        var green = findChild(picker, "colorPickerG")
        var blue = findChild(picker, "colorPickerB")
        var hex = findChild(picker, "colorPickerHex")
        verify(red && green && blue && hex)

        replaceText(red, "18")
        replaceText(green, "52")
        replaceText(blue, "86")

        compare(red.text, "18")
        compare(green.text, "52")
        compare(blue.text, "86")
        compare(appliedSpy.count, 0)
        compare(picker.visible, true)
        mouseClick(findChild(picker, "colorPickerApply"))
        compare(appliedSpy.count, 1)
        compare(normalizedColor(appliedSpy.signalArguments[0][0]), "#123456")
    }

    function test_apply_return_emits_applied_once() {
        openReferenceColor()
        var apply = findChild(picker, "colorPickerApply")
        verify(apply)

        apply.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Return)

        tryCompare(picker, "visible", false)
        compare(appliedSpy.count, 1)
        compare(cancelledSpy.count, 0)
    }

    function test_apply_enter_emits_applied_once() {
        openReferenceColor()
        var apply = findChild(picker, "colorPickerApply")
        verify(apply)

        apply.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Enter)

        tryCompare(picker, "visible", false)
        compare(appliedSpy.count, 1)
        compare(cancelledSpy.count, 0)
    }

    function test_invalid_hex_and_rgb_do_not_apply() {
        openReferenceColor()
        var hex = findChild(picker, "colorPickerHex")
        var apply = findChild(picker, "colorPickerApply")
        verify(hex && apply)
        hex.text = "invalid"
        mouseClick(apply, apply.width / 2, apply.height / 2)

        compare(picker.visible, true)
        compare(appliedSpy.count, 0)
        compare(hex.text, "invalid")

        picker.cancelPicker()
        openReferenceColor()
        var red = findChild(picker, "colorPickerR")
        apply = findChild(picker, "colorPickerApply")
        verify(red && apply)
        red.text = "999"
        mouseClick(apply, apply.width / 2, apply.height / 2)

        compare(picker.visible, true)
        compare(appliedSpy.count, 0)
        compare(red.text, "999")
    }

    function test_bottom_right_field_popup_stays_inside_overlay() {
        integratedField.x = integratedField.parent.width
                            - integratedField.width - 1
        integratedField.y = integratedField.parent.height
                            - integratedField.height - 1
        mouseClick(integratedField, integratedField.width / 2,
                   integratedField.height / 2)
        var integratedPicker = findChild(integratedField, "colorFieldPicker")
        verify(integratedPicker)
        tryCompare(integratedPicker, "visible", true)
        var overlay = integratedPicker.Overlay.overlay
        verify(overlay)

        verifyMappedInside(integratedPicker.background, overlay, 10)
        verifyMappedInside(integratedPicker.contentItem, overlay, 10)
        verify(integratedPicker.width <= 292)
        verify(integratedPicker.height <= 248)
        compare(integratedEditedSpy.count, 0)
    }

    function test_cancel_button_and_outside_press_each_cancel_once() {
        openReferenceColor()
        var cancel = findChild(picker, "colorPickerCancel")
        verify(cancel)
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        tryCompare(picker, "visible", false)
        compare(cancelledSpy.count, 1)
        compare(appliedSpy.count, 0)

        openReferenceColor()
        mouseClick(testHost, 4, 4)
        tryCompare(picker, "visible", false)
        compare(cancelledSpy.count, 2)
        compare(appliedSpy.count, 0)
    }

    function test_picker_controls_have_names_roles_and_apply_keyboard_activation() {
        openReferenceColor()
        var plane = findChild(picker, "colorPickerSvPlane")
        var hue = findChild(picker, "colorPickerHueRail")
        var hex = findChild(picker, "colorPickerHex")
        var red = findChild(picker, "colorPickerR")
        var green = findChild(picker, "colorPickerG")
        var blue = findChild(picker, "colorPickerB")
        var apply = findChild(picker, "colorPickerApply")
        var cancel = findChild(picker, "colorPickerCancel")
        verify(plane && hue && hex && red && green && blue && apply && cancel)
        verify(plane.Accessible.name.length > 0)
        verify(hue.Accessible.name.length > 0)
        compare(hex.Accessible.role, Accessible.EditableText)
        verify(red.Accessible.name.length > 0)
        compare(apply.Accessible.role, Accessible.Button)
        compare(cancel.Accessible.role, Accessible.Button)

        apply.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Space)
        tryCompare(picker, "visible", false)
        compare(appliedSpy.count, 1)
    }

    function test_color_field_applies_only_after_explicit_apply() {
        SettingsController.waveformSolidBaseColor = "#63316B"
        tryVerify(function() {
            return normalizedColor(integratedField.colorValue) === "#63316B"
        })
        var hexEntry = findChild(integratedField, "colorFieldHex")
        verify(hexEntry)
        compare(hexEntry.text, "#63316B")

        mouseClick(integratedField, integratedField.width / 2,
                   integratedField.height / 2)
        var integratedPicker = findChild(integratedField, "colorFieldPicker")
        verify(integratedPicker)
        tryCompare(integratedPicker, "visible", true)
        compare(normalizedColor(integratedPicker.workingColor), "#63316B")
        var hex = findChild(integratedPicker, "colorPickerHex")
        var apply = findChild(integratedPicker, "colorPickerApply")
        verify(hex && apply)
        hex.text = "#F8EBFA"
        hex.forceActiveFocus()
        keyClick(Qt.Key_Enter)
        compare(integratedEditedSpy.count, 0)
        mouseClick(apply, apply.width / 2, apply.height / 2)

        tryVerify(function() {
            return normalizedColor(SettingsController.waveformSolidBaseColor)
                    === "#F8EBFA"
        })
        compare(integratedEditedSpy.count, 1)
        compare(integratedEditedSpy.signalArguments[0][0], "#F8EBFA")
        compare(integratedPicker.visible, false)

        mouseClick(integratedField, integratedField.width / 2,
                   integratedField.height / 2)
        tryCompare(integratedPicker, "visible", true)
        mouseClick(findChild(integratedPicker, "colorPickerCancel"))

        compare(integratedPicker.visible, false)
        compare(normalizedColor(SettingsController.waveformSolidBaseColor),
                "#F8EBFA")
        compare(integratedEditedSpy.count, 1)
    }

    function test_color_field_is_focusable_accessible_and_keyboard_operable() {
        SettingsController.waveformSolidBaseColor = "#63316B"
        tryVerify(function() {
            return normalizedColor(integratedField.colorValue) === "#63316B"
        })
        compare(integratedField.objectName, "colorFieldButton")
        compare(integratedField.focusPolicy, Qt.StrongFocus)
        compare(integratedField.Accessible.role, Accessible.Button)
        compare(integratedField.Accessible.name, "Color #63316B")

        integratedField.forceActiveFocus()
        verify(integratedField.activeFocus)
        keyClick(Qt.Key_Space)
        var integratedPicker = findChild(integratedField, "colorFieldPicker")
        tryCompare(integratedPicker, "visible", true)
        integratedPicker.close()
        tryCompare(integratedField, "activeFocus", true)

        keyClick(Qt.Key_Return)
        tryCompare(integratedPicker, "visible", true)
        keyClick(Qt.Key_Escape)
        tryCompare(integratedPicker, "visible", false)
        tryCompare(integratedField, "activeFocus", true)

        compare(integratedEditedSpy.count, 0)
        compare(normalizedColor(SettingsController.waveformSolidBaseColor),
                "#63316B")
    }

    function test_theme_switch_updates_picker_chrome() {
        SettingsController.themeMode = 0
        openReferenceColor()
        var darkChrome = picker.background.color.toString()
        compare(darkChrome, Theme.elevated.toString())

        SettingsController.themeMode = 1
        wait(0)

        compare(picker.background.color.toString(), Theme.elevated.toString())
        verify(picker.background.color.toString() !== darkChrome)
        compare(normalizedColor(picker.workingColor), "#63316B")
        compare(appliedSpy.count, 0)
    }

    function test_settings_drive_runtime_theme_and_cancel_restores_tokens() {
        SettingsController.themeMode = 1
        SettingsController.skinColorMode = 0
        wait(0)

        var initialAccent = ThemeManager.accent.toString()
        var initialHighlight = ThemeManager.highlight.toString()
        var initialBackground = ThemeManager.background.toString()
        var initialCurrentTrackSurface = ThemeManager.currentTrackSurface.toString()
        var initialSemantic = ThemeManager.success.toString()

        SettingsController.beginEdit()
        SettingsController.skinColorMode = 2
        SettingsController.skinCustomColor = "#D27722"

        tryVerify(function() {
            return ThemeManager.accent.toString() !== initialAccent
                    && ThemeManager.highlight.toString() !== initialHighlight
                    && ThemeManager.background.toString() !== initialBackground
                    && ThemeManager.currentTrackSurface.toString()
                       !== initialCurrentTrackSurface
        })
        compare(Theme.accent, ThemeManager.accent)
        compare(Theme.activeSelection, ThemeManager.highlight)
        compare(Theme.currentTrackSurface, ThemeManager.currentTrackSurface)
        compare(ThemeManager.success.toString(), initialSemantic)

        SettingsController.cancelEdit()
        tryVerify(function() {
            return ThemeManager.accent.toString() === initialAccent
                    && ThemeManager.highlight.toString() === initialHighlight
                    && ThemeManager.background.toString() === initialBackground
                    && ThemeManager.currentTrackSurface.toString()
                       === initialCurrentTrackSurface
        })
    }

    function test_theme_selectors_apply_presets_and_keyboard_activation() {
        SettingsController.skinColorMode = 0
        SettingsController.skinPreset = "systemBlue"
        wait(0)
        paletteChangedSpy.clear()
        var defaultButton = findChild(skinSelector, "skinSelectorDefault")
        var purple = findChild(skinSelector, "skinSelectorPreset-purple")
        verify(defaultButton && purple)
        verify(defaultButton.checked)
        compare(purple.Accessible.role, Accessible.Button)
        verify(purple.width >= 24)
        verify(purple.height >= 24)
        verify(purple.Accessible.name.indexOf("purple") < 0)
        verify(purple.Accessible.name.indexOf("紫色") >= 0)
        verify(!purple.selectionCueVisible)
        purple.forceActiveFocus()
        tryVerify(function() { return purple.focusCueVisible })
        keyClick(Qt.Key_Space)
        tryCompare(SettingsController, "skinColorMode", 1)
        tryCompare(SettingsController, "skinPreset", "purple")
        compare(paletteChangedSpy.count, 1)
        verify(purple.checked)
        verify(purple.selectionCueVisible)
        verify(purple.focusCueVisible)
    }

    function test_representative_controls_use_accent_highlight_focus_and_disabled_tokens() {
        SettingsController.themeMode = 1
        SettingsController.skinColorMode = 1
        SettingsController.skinPreset = "purple"
        wait(0)

        compare(representativeSwitch.indicator.color.toString(),
                Theme.accent.toString())
        compare(representativeCheckBox.indicator.color.toString(),
                Theme.accent.toString())
        compare(representativeRangeSlider.background.children[0].color.toString(),
                Theme.accent.toString())
        compare(representativeComboBox.palette.highlight.toString(),
                Theme.highlight.toString())

        representativeComboBox.forceActiveFocus()
        tryCompare(representativeComboBox, "activeFocus", true)
        compare(representativeComboBox.background.border.color.toString(),
                Theme.focus.toString())

        representativeSwitch.enabled = false
        representativeCheckBox.enabled = false
        representativeRangeSlider.enabled = false
        wait(0)
        compare(representativeSwitch.indicator.color.toString(),
                Theme.disabled.toString())
        compare(representativeCheckBox.indicator.color.toString(),
                Theme.disabled.toString())
        compare(representativeRangeSlider.background.color.toString(),
                Theme.disabled.toString())

        representativeSwitch.enabled = true
        representativeCheckBox.enabled = true
        representativeRangeSlider.enabled = true
    }

    function test_theme_selector_custom_apply_and_transactions_cancel_or_commit() {
        SettingsController.beginEdit()
        SettingsController.skinColorMode = 0
        var custom = findChild(skinSelector, "skinSelectorCustomField")
        verify(custom)
        custom.clicked()
        var customPicker = findChild(custom, "colorFieldPicker")
        tryCompare(customPicker, "visible", true)
        var hex = findChild(customPicker, "colorPickerHex")
        var apply = findChild(customPicker, "colorPickerApply")
        verify(hex && apply)
        hex.text = "#FFF5EC"
        hex.forceActiveFocus()
        keyClick(Qt.Key_Enter)
        mouseClick(apply, apply.width / 2, apply.height / 2)
        tryCompare(SettingsController, "skinColorMode", 2)
        tryCompare(SettingsController, "skinCustomColor", "#FFF5EC")
        verify(custom.selectionCueVisible)
        SettingsController.cancelEdit()
        tryCompare(SettingsController, "skinColorMode", 0)

        SettingsController.beginEdit()
        SettingsController.skinColorMode = 1
        SettingsController.skinPreset = "purple"
        SettingsController.commitEdit()
        compare(SettingsController.skinColorMode, 1)
        compare(SettingsController.skinPreset, "purple")
    }
}
