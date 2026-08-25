import QtQuick
import QtQuick.Controls
import QtTest
import AgPlayer
import "../../app/qml/AgPlayer/components/ColorScale.js" as ColorScale

TestCase {
    id: testCase
    name: "AgColorPicker"
    when: windowShown
    width: 720
    height: 640

    property int savedThemeMode: 0
    property string savedWaveformUnplayedColor: ""
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
            colorValue: SettingsController.waveformUnplayedColor
            targetProperty: "waveformUnplayedColor"
        }

        ThemeColorSelector {
            id: accentSelector
            parent: testHost
            x: 18
            y: 350
            objectNamePrefix: "accentSelector"
            title: "Accent"
            selectedMode: SettingsController.accentMode
            selectedPreset: SettingsController.accentPreset
            customColor: SettingsController.accentCustomColor
            onDefaultRequested: SettingsController.accentMode = 0
            onPresetRequested: function(preset) {
                SettingsController.accentMode = 1
                SettingsController.accentPreset = preset
            }
            onCustomRequested: function(color) {
                SettingsController.accentMode = 2
                SettingsController.accentCustomColor = color
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

        ThemeColorSelector {
            id: highlightSelector
            parent: testHost
            x: 365
            y: 350
            objectNamePrefix: "highlightSelector"
            title: "Highlight"
            enabled: !SettingsController.highlightFollowAccent
            selectedMode: SettingsController.highlightMode
            selectedPreset: SettingsController.highlightPreset
            customColor: SettingsController.highlightCustomColor
            onDefaultRequested: SettingsController.highlightMode = 0
            onPresetRequested: function(preset) {
                SettingsController.highlightMode = 1
                SettingsController.highlightPreset = preset
            }
            onCustomRequested: function(color) {
                SettingsController.highlightMode = 2
                SettingsController.highlightCustomColor = color
            }
        }
    }

    SignalSpy {
        id: acceptedSpy
        target: picker
        signalName: "colorAccepted"
    }

    SignalSpy {
        id: integratedEditedSpy
        target: integratedField
        signalName: "colorEdited"
    }

    function initTestCase() {
        savedThemeMode = SettingsController.themeMode
    }

    function init() {
        picker.close()
        acceptedSpy.clear()
        integratedEditedSpy.clear()
        savedWaveformUnplayedColor = SettingsController.waveformUnplayedColor
        savedThemeChoices = {
            accentMode: SettingsController.accentMode,
            accentPreset: SettingsController.accentPreset,
            accentCustomColor: SettingsController.accentCustomColor,
            follow: SettingsController.highlightFollowAccent,
            highlightMode: SettingsController.highlightMode,
            highlightPreset: SettingsController.highlightPreset,
            highlightCustomColor: SettingsController.highlightCustomColor
        }
        SettingsController.themeMode = 0
        wait(0)
    }

    function cleanup() {
        picker.close()
        var integratedPicker = findChild(integratedField, "colorFieldPicker")
        if (integratedPicker)
            integratedPicker.close()
        SettingsController.waveformUnplayedColor = savedWaveformUnplayedColor
        SettingsController.accentMode = savedThemeChoices.accentMode
        SettingsController.accentPreset = savedThemeChoices.accentPreset
        SettingsController.accentCustomColor = savedThemeChoices.accentCustomColor
        SettingsController.highlightFollowAccent = savedThemeChoices.follow
        SettingsController.highlightMode = savedThemeChoices.highlightMode
        SettingsController.highlightPreset = savedThemeChoices.highlightPreset
        SettingsController.highlightCustomColor = savedThemeChoices.highlightCustomColor
        acceptedSpy.clear()
        integratedEditedSpy.clear()
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

    function test_open_initializes_both_states_and_candidates() {
        var expected = [
            "#F8EBFA", "#E9D2EC", "#D6B9DB", "#C09CC6", "#A76BB0",
            "#63316B", "#512C57", "#432248", "#341938", "#251028"
        ]

        picker.openForColor("#123456")
        tryCompare(picker, "visible", true)
        picker.close()
        openReferenceColor()

        compare(normalizedColor(picker.baseColor), "#63316B")
        compare(normalizedColor(picker.selectedColor), "#63316B")
        compare(picker.candidateColors.join(","), expected.join(","))
        compare(acceptedSpy.count, 0)
    }

    function test_hex_edit_changes_base_and_candidates_only() {
        openReferenceColor()
        var originalCandidates = picker.candidateColors.join(",")
        var hexInput = findChild(picker, "colorPickerHex")
        verify(hexInput)

        replaceText(hexInput, "#123456")

        compare(normalizedColor(picker.baseColor), "#123456")
        compare(hexInput.text, "#123456")
        verify(picker.candidateColors.join(",") !== originalCandidates)
        compare(normalizedColor(picker.selectedColor), "#63316B")
        compare(acceptedSpy.count, 0)
    }

    function test_rgb_fields_synchronize_hex_and_sliders() {
        openReferenceColor()
        var redInput = findChild(picker, "colorPickerR")
        var greenInput = findChild(picker, "colorPickerG")
        var blueInput = findChild(picker, "colorPickerB")
        var redSlider = findChild(picker, "colorPickerRSlider")
        var greenSlider = findChild(picker, "colorPickerGSlider")
        var blueSlider = findChild(picker, "colorPickerBSlider")
        var hexInput = findChild(picker, "colorPickerHex")
        verify(redInput && greenInput && blueInput)
        verify(redSlider && greenSlider && blueSlider && hexInput)

        replaceText(redInput, "18")
        replaceText(greenInput, "52")
        replaceText(blueInput, "86")

        compare(normalizedColor(picker.baseColor), "#123456")
        compare(hexInput.text, "#123456")
        compare(redInput.text, "18")
        compare(greenInput.text, "52")
        compare(blueInput.text, "86")
        compare(Math.round(redSlider.value), 18)
        compare(Math.round(greenSlider.value), 52)
        compare(Math.round(blueSlider.value), 86)
        compare(normalizedColor(picker.selectedColor), "#63316B")
        compare(acceptedSpy.count, 0)
    }

    function test_each_slider_synchronizes_rgb_fields_and_hex() {
        picker.openForColor("#123456")
        tryCompare(picker, "visible", true)
        var redSlider = findChild(picker, "colorPickerRSlider")
        var greenSlider = findChild(picker, "colorPickerGSlider")
        var blueSlider = findChild(picker, "colorPickerBSlider")
        var redInput = findChild(picker, "colorPickerR")
        var greenInput = findChild(picker, "colorPickerG")
        var blueInput = findChild(picker, "colorPickerB")
        var hexInput = findChild(picker, "colorPickerHex")

        mouseClick(redSlider, redSlider.width - 1, redSlider.height / 2)
        compare(normalizedColor(picker.baseColor), "#FF3456")
        compare(redInput.text, "255")
        compare(hexInput.text, "#FF3456")

        mouseClick(greenSlider, 1, greenSlider.height / 2)
        compare(normalizedColor(picker.baseColor), "#FF0056")
        compare(greenInput.text, "0")
        compare(hexInput.text, "#FF0056")

        mouseClick(blueSlider, blueSlider.width - 1, blueSlider.height / 2)
        compare(normalizedColor(picker.baseColor), "#FF00FF")
        compare(blueInput.text, "255")
        compare(hexInput.text, "#FF00FF")
        compare(normalizedColor(picker.selectedColor), "#123456")
        compare(acceptedSpy.count, 0)
    }

    function test_invalid_hex_restores_last_valid_value() {
        openReferenceColor()
        var hexInput = findChild(picker, "colorPickerHex")

        replaceText(hexInput, "invalid")

        compare(hexInput.text, "#63316B")
        compare(normalizedColor(picker.baseColor), "#63316B")
        compare(normalizedColor(picker.selectedColor), "#63316B")
        compare(acceptedSpy.count, 0)
    }

    function test_invalid_rgb_restores_on_enter_and_focus_loss() {
        openReferenceColor()
        var redInput = findChild(picker, "colorPickerR")
        var greenInput = findChild(picker, "colorPickerG")
        var hexInput = findChild(picker, "colorPickerHex")

        typeText(redInput, "999")
        compare(redInput.text, "999")
        verify(!redInput.acceptableInput)
        keyClick(Qt.Key_Return)
        wait(0)
        compare(redInput.text, "99")

        typeText(greenInput, "999")
        compare(greenInput.text, "999")
        verify(!greenInput.acceptableInput)
        mouseClick(hexInput)
        tryCompare(greenInput, "activeFocus", false)
        compare(greenInput.text, "49")

        compare(normalizedColor(picker.baseColor), "#63316B")
        compare(normalizedColor(picker.selectedColor), "#63316B")
        compare(acceptedSpy.count, 0)
    }

    function test_candidate_accepts_once_and_closes_immediately() {
        openReferenceColor()
        tryVerify(function() {
            return findChild(picker.contentItem, "colorCandidate-0") !== null
        })
        var candidate = findChild(picker.contentItem, "colorCandidate-0")
        verify(candidate)

        mouseClick(candidate)

        compare(acceptedSpy.count, 1)
        compare(normalizedColor(acceptedSpy.signalArguments[0][0]), "#F8EBFA")
        compare(normalizedColor(picker.selectedColor), "#F8EBFA")
        compare(picker.visible, false)
    }

    function test_close_button_cancels_without_acceptance() {
        openReferenceColor()
        var closeButton = findChild(picker, "colorPickerClose")
        verify(closeButton)

        mouseClick(closeButton)

        compare(picker.visible, false)
        compare(acceptedSpy.count, 0)
        compare(normalizedColor(picker.selectedColor), "#63316B")
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
        compare(integratedEditedSpy.count, 0)
    }

    function test_candidate_accessibility_reports_scale_and_selection() {
        openReferenceColor()
        var candidate0 = findChild(picker.contentItem, "colorCandidate-0")
        var candidate5 = findChild(picker.contentItem, "colorCandidate-5")
        verify(candidate0 && candidate5)

        compare(candidate0.Accessible.role, Accessible.Button)
        verify(candidate0.Accessible.name.indexOf("#F8EBFA") >= 0)
        verify(candidate0.Accessible.name.indexOf("10") >= 0)
        compare(candidate0.Accessible.selected, false)
        compare(candidate0.activeFocusOnTab, true)
        verify(candidate5.Accessible.name.indexOf("#63316B") >= 0)
        verify(candidate5.Accessible.name.indexOf("100") >= 0)
        compare(candidate5.Accessible.selected, true)
    }

    function test_picker_focus_chain_and_keyboard_activation() {
        testCase.Window.window.requestActivate()
        tryCompare(testCase.Window.window, "active", true)
        openReferenceColor()
        var hexInput = findChild(picker, "colorPickerHex")
        var closeButton = findChild(picker, "colorPickerClose")
        compare(hexInput.nextItemInFocusChain(true).objectName,
                closeButton.objectName)
        closeButton.forceActiveFocus(Qt.TabFocusReason)
        verify(closeButton.activeFocus)
        keyClick(Qt.Key_Return)
        tryCompare(picker, "visible", false)
        compare(acceptedSpy.count, 0)

        openReferenceColor()
        var blueSlider = findChild(picker, "colorPickerBSlider")
        var candidate0 = findChild(picker.contentItem, "colorCandidate-0")
        compare(blueSlider.nextItemInFocusChain(true).objectName,
                candidate0.objectName)
        candidate0.forceActiveFocus(Qt.TabFocusReason)
        verify(candidate0.activeFocus)
        keyClick(Qt.Key_Space)
        tryCompare(picker, "visible", false)
        compare(acceptedSpy.count, 1)
        compare(normalizedColor(acceptedSpy.signalArguments[0][0]), "#F8EBFA")

        acceptedSpy.clear()
        openReferenceColor()
        candidate0 = findChild(picker.contentItem, "colorCandidate-0")
        var candidate1 = findChild(picker.contentItem, "colorCandidate-1")
        compare(candidate0.nextItemInFocusChain(true).objectName,
                candidate1.objectName)
        candidate1.forceActiveFocus(Qt.TabFocusReason)
        verify(candidate1.activeFocus)
        keyClick(Qt.Key_Return)
        tryCompare(picker, "visible", false)
        compare(acceptedSpy.count, 1)
        compare(normalizedColor(acceptedSpy.signalArguments[0][0]), "#E9D2EC")
    }

    function test_escape_cancels_without_acceptance() {
        openReferenceColor()
        findChild(picker, "colorPickerHex").forceActiveFocus()

        keyClick(Qt.Key_Escape)

        tryCompare(picker, "visible", false)
        compare(acceptedSpy.count, 0)
        compare(normalizedColor(picker.selectedColor), "#63316B")
    }

    function test_outside_press_cancels_without_acceptance() {
        openReferenceColor()

        mouseClick(testHost, 4, 4)

        tryCompare(picker, "visible", false)
        compare(acceptedSpy.count, 0)
        compare(normalizedColor(picker.selectedColor), "#63316B")
    }

    function test_color_field_commits_candidate_and_cancellation_preserves_setting() {
        SettingsController.waveformUnplayedColor = "#63316B"
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
        compare(normalizedColor(integratedPicker.baseColor), "#63316B")

        tryVerify(function() {
            return findChild(integratedPicker.contentItem,
                             "colorCandidate-0") !== null
        })
        mouseClick(findChild(integratedPicker.contentItem, "colorCandidate-0"))

        tryVerify(function() {
            return normalizedColor(SettingsController.waveformUnplayedColor)
                    === "#F8EBFA"
        })
        compare(integratedEditedSpy.count, 1)
        compare(integratedEditedSpy.signalArguments[0][0], "#F8EBFA")
        compare(integratedPicker.visible, false)

        mouseClick(integratedField, integratedField.width / 2,
                   integratedField.height / 2)
        tryCompare(integratedPicker, "visible", true)
        mouseClick(findChild(integratedPicker, "colorPickerClose"))

        compare(integratedPicker.visible, false)
        compare(normalizedColor(SettingsController.waveformUnplayedColor),
                "#F8EBFA")
        compare(integratedEditedSpy.count, 1)
    }

    function test_color_field_is_focusable_accessible_and_keyboard_operable() {
        SettingsController.waveformUnplayedColor = "#63316B"
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
        compare(normalizedColor(SettingsController.waveformUnplayedColor),
                "#63316B")
    }

    function test_theme_switch_updates_chrome_not_candidates() {
        SettingsController.themeMode = 0
        openReferenceColor()
        var candidatesBefore = picker.candidateColors.slice(0)
        var darkChrome = picker.background.color.toString()
        compare(darkChrome, Theme.elevated.toString())

        SettingsController.themeMode = 1
        wait(0)

        compare(picker.background.color.toString(), Theme.elevated.toString())
        verify(picker.background.color.toString() !== darkChrome)
        compare(picker.candidateColors.join(","), candidatesBefore.join(","))
        compare(normalizedColor(picker.baseColor), "#63316B")
        compare(normalizedColor(picker.selectedColor), "#63316B")
        compare(acceptedSpy.count, 0)
    }

    function test_picker_controls_have_names_and_roles() {
        SettingsController.themeMode = 0
        openReferenceColor()
        var hex = findChild(picker, "colorPickerHex")
        var close = findChild(picker, "colorPickerClose")
        var redSlider = findChild(picker, "colorPickerRSlider")
        var candidate = findChild(picker.contentItem, "colorCandidate-0")
        verify(hex && close && redSlider && candidate)
        compare(hex.Accessible.role, Accessible.EditableText)
        verify(hex.Accessible.name.length > 0)
        compare(close.Accessible.role, Accessible.Button)
        verify(close.Accessible.name.length > 0)
        compare(redSlider.Accessible.role, Accessible.Slider)
        verify(redSlider.Accessible.name.length > 0)
        compare(candidate.Accessible.role, Accessible.Button)
        verify(candidate.Accessible.name.indexOf("#") >= 0)
    }

    function test_settings_drive_runtime_theme_and_cancel_restores_tokens() {
        SettingsController.themeMode = 1
        SettingsController.accentMode = 0
        SettingsController.highlightFollowAccent = true
        wait(0)

        var initialAccent = ThemeManager.accent.toString()
        var initialHighlight = ThemeManager.highlight.toString()
        var initialSemantic = ThemeManager.success.toString()

        SettingsController.beginEdit()
        SettingsController.accentMode = 1
        SettingsController.accentPreset = "systemBlue"
        SettingsController.highlightFollowAccent = false
        SettingsController.highlightMode = 1
        SettingsController.highlightPreset = "purple"

        tryVerify(function() {
            return ThemeManager.accent.toString() !== initialAccent
                    && ThemeManager.highlight.toString() !== initialHighlight
        })
        compare(Theme.accent, ThemeManager.accent)
        compare(Theme.activeSelection, ThemeManager.highlight)
        compare(ThemeManager.success.toString(), initialSemantic)

        SettingsController.cancelEdit()
        tryVerify(function() {
            return ThemeManager.accent.toString() === initialAccent
                    && ThemeManager.highlight.toString() === initialHighlight
        })
    }

    function test_theme_selectors_apply_presets_and_keyboard_activation() {
        SettingsController.accentMode = 0
        var defaultButton = findChild(accentSelector, "accentSelectorDefault")
        var blue = findChild(accentSelector, "accentSelectorPreset-systemBlue")
        verify(defaultButton && blue)
        verify(defaultButton.checked)
        compare(blue.Accessible.role, Accessible.Button)
        verify(blue.width >= 24)
        verify(blue.height >= 24)
        verify(blue.Accessible.name.indexOf("systemBlue") < 0)
        verify(blue.Accessible.name.indexOf("系统蓝") >= 0)
        verify(!blue.selectionCueVisible)
        blue.forceActiveFocus()
        tryVerify(function() { return blue.focusCueVisible })
        keyClick(Qt.Key_Space)
        tryCompare(SettingsController, "accentMode", 1)
        tryCompare(SettingsController, "accentPreset", "systemBlue")
        verify(blue.checked)
        verify(blue.selectionCueVisible)
        verify(blue.focusCueVisible)
    }

    function test_theme_selectors_keep_blue_accent_and_purple_highlight_independent() {
        SettingsController.accentMode = 1
        SettingsController.accentPreset = "systemBlue"
        SettingsController.highlightFollowAccent = false
        var purple = findChild(highlightSelector, "highlightSelectorPreset-purple")
        verify(purple)
        tryCompare(highlightSelector, "enabled", true)
        purple.clicked()
        tryCompare(SettingsController, "highlightMode", 1)
        tryCompare(SettingsController, "highlightPreset", "purple")
        compare(SettingsController.accentPreset, "systemBlue")
    }

    function test_representative_controls_use_accent_highlight_focus_and_disabled_tokens() {
        SettingsController.themeMode = 1
        SettingsController.accentMode = 1
        SettingsController.accentPreset = "systemBlue"
        SettingsController.highlightFollowAccent = false
        SettingsController.highlightMode = 1
        SettingsController.highlightPreset = "purple"
        wait(0)

        verify(Theme.accent.toString() !== Theme.highlight.toString())
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

    function test_follow_accent_disables_highlight_and_restores_independent_choice() {
        SettingsController.highlightFollowAccent = false
        SettingsController.highlightMode = 1
        SettingsController.highlightPreset = "purple"
        tryCompare(highlightSelector, "enabled", true)
        SettingsController.highlightFollowAccent = true
        tryCompare(highlightSelector, "enabled", false)
        compare(SettingsController.highlightPreset, "purple")
        SettingsController.highlightFollowAccent = false
        tryCompare(highlightSelector, "enabled", true)
        compare(SettingsController.highlightMode, 1)
        compare(SettingsController.highlightPreset, "purple")
    }

    function test_theme_selector_custom_candidate_previews_and_transactions_cancel_or_commit() {
        SettingsController.beginEdit()
        SettingsController.accentMode = 0
        var custom = findChild(accentSelector, "accentSelectorCustomField")
        verify(custom)
        custom.clicked()
        var customPicker = findChild(custom, "colorFieldPicker")
        tryCompare(customPicker, "visible", true)
        tryVerify(function() {
            return findChild(customPicker.contentItem, "colorCandidate-0") !== null
        })
        mouseClick(findChild(customPicker.contentItem, "colorCandidate-0"))
        tryCompare(SettingsController, "accentMode", 2)
        tryCompare(SettingsController, "accentCustomColor", "#FFF5EC")
        verify(custom.selectionCueVisible)
        SettingsController.cancelEdit()
        tryCompare(SettingsController, "accentMode", 0)

        SettingsController.beginEdit()
        SettingsController.accentMode = 1
        SettingsController.accentPreset = "purple"
        SettingsController.commitEdit()
        compare(SettingsController.accentMode, 1)
        compare(SettingsController.accentPreset, "purple")
    }
}
