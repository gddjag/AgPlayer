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

        Column {
            id: skinFixture
            parent: testCase.Window.window
                    ? testCase.Window.window.contentItem : null
            x: 18
            y: 350
            width: 429
            spacing: 6

            ThemeColorSelector {
                id: skinSelector
                width: parent.width
                height: implicitHeight
                objectNamePrefix: "skinSelector"
                title: "Theme skin color"
                selectedMode: SettingsController.skinColorMode
                selectedPreset: SettingsController.skinPreset
                customKind: SettingsController.skinCustomKind
                customColor: SettingsController.skinCustomColor
                customColorMiddle: SettingsController.skinCustomColorMiddle
                customColorEnd: SettingsController.skinCustomColorEnd
                onDefaultRequested: SettingsController.selectDefaultSkin()
                onPresetRequested: function(id) {
                    SettingsController.selectSkinPreset(id)
                }
                onCustomConfigurationRequested: function(kind, start, middle, end) {
                    SettingsController.setSkinCustomConfiguration(
                                kind, start, middle, end)
                }
            }

            Item {
                id: nextSettingFixture
                objectName: "skinSelectorNextSetting"
                width: parent.width
                height: 36
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

    Component {
        id: localizedSelectorComponent

        ThemeColorSelector {
            width: 429
            selectedMode: 0
        }
    }

    Component {
        id: settingsPageComponent

        SettingsPage {
            width: 860
            height: 720
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

    SignalSpy {
        id: skinConfigurationChangedSpy
        target: SettingsController
        signalName: "skinConfigurationChanged"
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
        skinConfigurationChangedSpy.clear()
        savedWaveformSolidBaseColor = SettingsController.waveformSolidBaseColor
        savedThemeChoices = {
            mode: SettingsController.skinColorMode,
            preset: SettingsController.skinPreset,
            customKind: SettingsController.skinCustomKind,
            customColor: SettingsController.skinCustomColor,
            customMiddle: SettingsController.skinCustomColorMiddle,
            customEnd: SettingsController.skinCustomColorEnd
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
        SettingsController.setSkinCustomConfiguration(
                    savedThemeChoices.customKind,
                    savedThemeChoices.customColor,
                    savedThemeChoices.customMiddle,
                    savedThemeChoices.customEnd)
        if (savedThemeChoices.mode === 0)
            SettingsController.selectDefaultSkin()
        else if (savedThemeChoices.mode === 1)
            SettingsController.selectSkinPreset(savedThemeChoices.preset)
        appliedSpy.clear()
        cancelledSpy.clear()
        integratedEditedSpy.clear()
        paletteChangedSpy.clear()
        skinConfigurationChangedSpy.clear()
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

    function test_theme_selector_renders_exact_recommended_gradient_cards() {
        SettingsController.selectDefaultSkin()
        wait(0)

        var ids = ["aurora", "seaGlass", "sunset", "lavenderMist",
                   "morningGlow"]
        var labels = ["Aurora", "Sea Glass", "Sunset", "Lavender Mist",
                      "Morning Glow"]
        var colors = [
            ["#73A6FF", "#A98BFF", "#F0A8D8"],
            ["#71D9D0", "#82C9F4", "#A7B7FF"],
            ["#F49BC2", "#FF9B86", "#FFC97A"],
            ["#8295F2", "#B89BE8", "#E8B7D5"],
            ["#8EDFCB", "#D4E9C2", "#FFD995"]
        ]
        var previousX = -1
        for (var i = 0; i < ids.length; ++i) {
            var card = findChild(skinSelector,
                                 "skinSelectorPreset-" + ids[i])
            verify(card)
            compare(card.width, 46)
            compare(card.height, 30)
            verify(card.x > previousX)
            previousX = card.x
            compare(card.Accessible.role, Accessible.Button)
            verify(card.Accessible.name.indexOf(labels[i]) >= 0)
            for (var stop = 0; stop < colors[i].length; ++stop)
                verify(card.Accessible.name.indexOf(colors[i][stop]) >= 0)
            compare(card.background.gradient.stops.length, 3)
            compare(normalizedColor(card.background.gradient.stops[0].color),
                    colors[i][0])
            compare(normalizedColor(card.background.gradient.stops[1].color),
                    colors[i][1])
            compare(normalizedColor(card.background.gradient.stops[2].color),
                    colors[i][2])
        }
        verify(!findChild(skinSelector, "skinSelectorPreset-purple"))
    }

    function test_theme_selector_keyboard_selection_is_atomic() {
        SettingsController.selectDefaultSkin()
        wait(0)
        paletteChangedSpy.clear()
        skinConfigurationChangedSpy.clear()

        var defaultButton = findChild(skinSelector, "skinSelectorDefault")
        var defaultCue = findChild(
                    skinSelector, "skinSelectorDefaultSelectionCue")
        var sunset = findChild(skinSelector, "skinSelectorPreset-sunset")
        var sunsetCue = findChild(
                    skinSelector, "skinSelectorPreset-sunsetSelectionCue")
        var morningGlow = findChild(
                    skinSelector, "skinSelectorPreset-morningGlow")
        verify(defaultButton && defaultCue && sunset && sunsetCue
               && morningGlow)
        verify(defaultButton.checked)
        verify(defaultCue.visible)
        compare(defaultCue.color.toString(), Theme.background.toString())
        compare(defaultCue.border.color.toString(),
                Theme.primaryText.toString())
        verify(!sunset.selectionCueVisible)
        verify(!sunsetCue.visible)
        sunset.forceActiveFocus()
        tryVerify(function() { return sunset.focusCueVisible })
        keyClick(Qt.Key_Space)
        tryCompare(SettingsController, "skinColorMode", 1)
        tryCompare(SettingsController, "skinPreset", "sunset")
        compare(skinConfigurationChangedSpy.count, 1)
        compare(paletteChangedSpy.count, 1)
        verify(sunset.checked)
        verify(sunset.selectionCueVisible)
        verify(sunsetCue.visible)
        compare(sunsetCue.color.toString(), Theme.background.toString())
        compare(sunsetCue.border.color.toString(),
                Theme.primaryText.toString())
        verify(sunset.focusCueVisible)

        skinConfigurationChangedSpy.clear()
        paletteChangedSpy.clear()
        morningGlow.forceActiveFocus()
        keyClick(Qt.Key_Return)
        tryCompare(SettingsController, "skinPreset", "morningGlow")
        compare(skinConfigurationChangedSpy.count, 1)
        compare(paletteChangedSpy.count, 1)
    }

    function test_representative_controls_use_accent_highlight_focus_and_disabled_tokens() {
        SettingsController.themeMode = 1
        SettingsController.selectSkinPreset("aurora")
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

    function test_theme_selector_custom_expands_solid_and_gradient_editor() {
        SettingsController.setSkinCustomConfiguration(
                    1, "#73A6FF", "#A98BFF", "#F0A8D8")
        SettingsController.selectDefaultSkin()
        wait(0)
        paletteChangedSpy.clear()
        skinConfigurationChangedSpy.clear()

        var custom = findChild(skinSelector, "skinSelectorCustom")
        var editor = findChild(skinSelector, "skinSelectorCustomEditor")
        verify(custom && editor)
        verify(!editor.visible)
        var collapsedHeight = skinSelector.implicitHeight

        custom.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryCompare(SettingsController, "skinColorMode", 2)
        tryCompare(skinSelector, "selectedMode", 2)
        compare(editor.expanded, true)
        verify(skinSelector.visible)
        tryCompare(editor, "visible", true)
        verify(skinSelector.implicitHeight > collapsedHeight)
        compare(skinSelector.width, 429)
        tryVerify(function() {
            return nextSettingFixture.y
                    >= skinSelector.y + skinSelector.height
        })
        compare(skinConfigurationChangedSpy.count, 1)
        compare(paletteChangedSpy.count, 1)
        verify(custom.checked)
        verify(custom.selectionCueVisible)

        var solid = findChild(skinSelector, "skinSelectorCustomSolid")
        var gradient = findChild(skinSelector, "skinSelectorCustomGradient")
        var start = findChild(skinSelector, "skinCustomStart")
        var middle = findChild(skinSelector, "skinCustomMiddle")
        var end = findChild(skinSelector, "skinCustomEnd")
        var preview = findChild(skinSelector, "skinCustomPreview")
        verify(solid && gradient && start && middle && end && preview)
        verify(solid.checked)
        verify(start.visible)
        verify(!middle.visible && !end.visible)
        compare(start.editingLabel, "Start")
        compare(middle.editingLabel, "Middle")
        compare(end.editingLabel, "End")
        compare(preview.gradient.stops.length, 3)
        compare(normalizedColor(preview.gradient.stops[0].color),
                normalizedColor(ThemeManager.backdropStart))
        compare(normalizedColor(preview.gradient.stops[1].color),
                normalizedColor(ThemeManager.backdropMiddle))
        compare(normalizedColor(preview.gradient.stops[2].color),
                normalizedColor(ThemeManager.backdropEnd))

        skinConfigurationChangedSpy.clear()
        gradient.clicked()
        tryCompare(SettingsController, "skinCustomKind", 1)
        verify(gradient.checked)
        verify(start.visible && middle.visible && end.visible)
        var controls = [solid, gradient, start, middle, end, preview]
        var minimumWidths = [58, 72, start.implicitWidth,
                             middle.implicitWidth, end.implicitWidth, 90]
        tryVerify(function() {
            var readySolid = solid.mapToItem(skinSelector, 0, 0)
            var readyGradient = gradient.mapToItem(skinSelector, 0, 0)
            var readyStart = start.mapToItem(skinSelector, 0, 0)
            var readyMiddle = middle.mapToItem(skinSelector, 0, 0)
            var readyEnd = end.mapToItem(skinSelector, 0, 0)
            var readyPreview = preview.mapToItem(skinSelector, 0, 0)
            return start.width >= start.implicitWidth
                    && middle.width >= middle.implicitWidth
                    && end.width >= end.implicitWidth
                    && readyGradient.x >= readySolid.x + solid.width
                    && readyPreview.x >= readyGradient.x + gradient.width
                    && readyStart.y >= readySolid.y + solid.height
                    && readyMiddle.x >= readyStart.x + start.width
                    && readyEnd.x >= readyMiddle.x + middle.width
        })
        for (var controlIndex = 0;
             controlIndex < controls.length; ++controlIndex) {
            verify(controls[controlIndex].width
                   >= minimumWidths[controlIndex])
            verifyMappedInside(controls[controlIndex], skinSelector, 0)
        }
        var solidPosition = solid.mapToItem(skinSelector, 0, 0)
        var gradientPosition = gradient.mapToItem(skinSelector, 0, 0)
        var startPosition = start.mapToItem(skinSelector, 0, 0)
        var middlePosition = middle.mapToItem(skinSelector, 0, 0)
        var endPosition = end.mapToItem(skinSelector, 0, 0)
        var previewPosition = preview.mapToItem(skinSelector, 0, 0)
        verify(gradientPosition.x >= solidPosition.x + solid.width)
        verify(previewPosition.x >= gradientPosition.x + gradient.width)
        verify(startPosition.y >= solidPosition.y + solid.height)
        verify(middlePosition.x >= startPosition.x + start.width,
               "start=" + startPosition.x + "+" + start.width
               + " middle=" + middlePosition.x)
        verify(endPosition.x >= middlePosition.x + middle.width,
               "middle=" + middlePosition.x + "+" + middle.width
               + " end=" + endPosition.x)
        verify(editor.y + editor.height <= skinSelector.height)
        tryVerify(function() {
            return nextSettingFixture.y >= skinSelector.height
        })
        compare(SettingsController.skinCustomColorMiddle, "#A98BFF")
        compare(SettingsController.skinCustomColorEnd, "#F0A8D8")
        compare(skinConfigurationChangedSpy.count, 1)
        compare(normalizedColor(preview.gradient.stops[0].color), "#73A6FF")
        compare(normalizedColor(preview.gradient.stops[1].color), "#A98BFF")
        compare(normalizedColor(preview.gradient.stops[2].color), "#F0A8D8")

        skinConfigurationChangedSpy.clear()
        solid.forceActiveFocus()
        keyClick(Qt.Key_Return)
        tryCompare(SettingsController, "skinCustomKind", 0)
        compare(SettingsController.skinCustomColorMiddle, "#A98BFF")
        compare(SettingsController.skinCustomColorEnd, "#F0A8D8")
        compare(skinConfigurationChangedSpy.count, 1)

        SettingsController.setSkinCustomConfiguration(
                    0, "#73A6FF", "#112233", "#445566")
        compare(SettingsController.skinCustomColorMiddle, "#112233")
        compare(SettingsController.skinCustomColorEnd, "#445566")
        skinConfigurationChangedSpy.clear()
        gradient.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryCompare(SettingsController, "skinCustomKind", 1)
        compare(SettingsController.skinCustomColorMiddle, "#112233")
        compare(SettingsController.skinCustomColorEnd, "#445566")
        compare(skinConfigurationChangedSpy.count, 1)
    }

    function test_theme_selector_custom_edit_is_one_complete_configuration() {
        SettingsController.setSkinCustomConfiguration(
                    1, "#73A6FF", "#A98BFF", "#F0A8D8")
        SettingsController.beginEdit()
        wait(0)
        skinConfigurationChangedSpy.clear()
        paletteChangedSpy.clear()

        var middle = findChild(skinSelector, "skinCustomMiddle")
        verify(middle)
        middle.clicked()
        var customPicker = findChild(middle, "colorFieldPicker")
        tryCompare(customPicker, "visible", true)
        compare(customPicker.editingLabel, "Middle")
        var hex = findChild(customPicker, "colorPickerHex")
        var apply = findChild(customPicker, "colorPickerApply")
        verify(hex && apply)
        hex.text = "#B4A2ED"
        hex.forceActiveFocus()
        keyClick(Qt.Key_Enter)
        mouseClick(apply, apply.width / 2, apply.height / 2)
        tryCompare(SettingsController, "skinColorMode", 2)
        compare(SettingsController.skinCustomKind, 1)
        compare(SettingsController.skinCustomColor, "#73A6FF")
        compare(SettingsController.skinCustomColorMiddle, "#B4A2ED")
        compare(SettingsController.skinCustomColorEnd, "#F0A8D8")
        compare(skinConfigurationChangedSpy.count, 1)
        compare(paletteChangedSpy.count, 1)
        SettingsController.cancelEdit()
        tryCompare(SettingsController, "skinCustomColorMiddle", "#A98BFF")

        SettingsController.beginEdit()
        SettingsController.selectSkinPreset("aurora")
        SettingsController.commitEdit()
        compare(SettingsController.skinColorMode, 1)
        compare(SettingsController.skinPreset, "aurora")
    }

    function test_qa_picker_routes_open_each_named_custom_stop() {
        SettingsController.setSkinCustomConfiguration(
                    1, "#73A6FF", "#A98BFF", "#F0A8D8")

        var routes = [
            { objectName: "skinCustomStart", label: "Start" },
            { objectName: "skinCustomMiddle", label: "Middle" },
            { objectName: "skinCustomEnd", label: "End" }
        ]
        for (var routeIndex = 0; routeIndex < routes.length; ++routeIndex) {
            var route = routes[routeIndex]
            var field = findChild(skinSelector, route.objectName)
            verify(field)
            field.clicked()
            var picker = findChild(field, "colorFieldPicker")
            verify(picker)
            tryCompare(picker, "visible", true)
            compare(picker.editingLabel, route.label)
            picker.cancelPicker()
            tryCompare(picker, "visible", false)
        }
    }

    function test_localized_default_label_reserves_selection_cue_and_row_fits() {
        var selector = localizedSelectorComponent.createObject(testHost)
        verify(selector)
        wait(0)

        var defaultButton = findChild(selector, "themeColorDefault")
        var selectionCue = findChild(
                    selector, "themeColorDefaultSelectionCue")
        var customButton = findChild(selector, "themeColorCustom")
        verify(defaultButton && selectionCue && customButton)

        var labels = ["默认", "Default", "ค่าเริ่มต้น", "Mặc định"]
        for (var labelIndex = 0; labelIndex < labels.length; ++labelIndex) {
            defaultButton.text = labels[labelIndex]
            wait(0)

            var textItem = defaultButton.contentItem
            var textPosition = textItem.mapToItem(defaultButton, 0, 0)
            var cuePosition = selectionCue.mapToItem(defaultButton, 0, 0)
            var paintedRight = textPosition.x
                    + (textItem.width + textItem.contentWidth) / 2
            verify(paintedRight <= cuePosition.x - 2,
                   labels[labelIndex] + ": text=" + paintedRight
                   + " cue=" + cuePosition.x)

            var customPosition = customButton.mapToItem(selector, 0, 0)
            verify(customPosition.x + customButton.width <= selector.width)
            verify(defaultButton.parent.implicitWidth <= selector.width)
        }

        selector.destroy()
    }

    function test_settings_sidebar_elides_and_only_tooltips_truncated_labels() {
        var page = settingsPageComponent.createObject(
                    testCase.Window.window.contentItem, {
            visible: true,
            z: 100
        })
        verify(page)
        page.visible = true
        wait(0)
        verify(page.visible)

        var sidebar = findChild(page, "settingsSidebar")
        var label = findChild(page, "settingsSectionLabel-3")
        var hoverArea = findChild(page, "settingsSectionHoverArea-3")
        verify(sidebar && label && hoverArea)
        compare(sidebar.width, 184)
        compare(label.elide, Text.ElideRight)
        verify(label.clip)

        label.text = "Audio tool presets with an intentionally long label"
        tryVerify(function() { return label.truncated })
        compare(label.ToolTip.text, label.text)
        verify(!label.ToolTip.visible)
        testCase.Window.window.requestActivate()
        tryCompare(testCase.Window.window, "active", true)
        mouseMove(page, page.width - 2, page.height - 2)
        wait(20)
        verify(hoverArea.visible && hoverArea.width > 0 && hoverArea.height > 0)
        var hoverPosition = hoverArea.mapToItem(
                    testCase.Window.window.contentItem,
                    hoverArea.width / 2, hoverArea.height / 2)
        mouseMove(testCase.Window.window.contentItem,
                  hoverPosition.x, hoverPosition.y)
        tryVerify(function() { return hoverArea.containsMouse }, 1000)
        tryVerify(function() { return label.ToolTip.visible })

        label.text = "About"
        tryVerify(function() { return !label.truncated })
        verify(!label.ToolTip.visible)

        page.destroy()
    }
}
