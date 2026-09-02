import QtQuick
import QtQuick.Controls
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "UiDesignSystemControls"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Rectangle {
        anchors.fill: parent
        color: Theme.contentSurface
        z: -1
    }

    Column {
        x: 24
        y: 24
        spacing: Theme.spacingMd

            ThemedButton {
                id: primaryButton
                objectName: "designPrimaryButton"
                text: "主要操作"
                primary: true
            }

            ThemedIconButton {
                id: iconButton
                objectName: "designIconButton"
                accessibleName: "设置"
                iconSource: Theme.icon("settings-3-fill")
            }

            ThemedTextField {
                id: textField
                objectName: "designTextField"
                width: 220
                placeholderText: "搜索"
            }

            ThemedSlider {
                id: slider
                objectName: "designSlider"
                width: 220
                value: 0.5
            }

            ThemedListRow {
                id: standardRow
                objectName: "designStandardRow"
                width: 260
                text: "标准列表行"
                selected: true
            }

            ThemedListRow {
                id: mediaRow
                objectName: "designMediaRow"
                width: 260
                text: "媒体列表行"
                media: true
            }

            ThemedTabButton {
                id: tabButton
                objectName: "designTabButton"
                width: 220
                text: "音频工具"
                iconSource: Theme.icon("equalizer-line")
                selected: true
            }

            ThemedCheckBox { id: checkBox; text: "复选" }
            ThemedRadioButton {
                id: radioButton
                objectName: "designRadioButton"
                text: "单选"
                checked: true
            }
            ThemedSwitch { id: switchControl; text: "开关" }
            ThemedRangeSlider { id: rangeSlider; width: 220 }
            ThemedComboBox { id: comboBox; width: 220; model: ["A", "B"] }
    }

    ThemedDialog { id: themedDialog; title: "对话框" }
    ThemedToolTip { id: themedToolTip; text: "提示" }
    ThemedScrollBar { id: themedScrollBar; size: 0.5 }
    SignalSpy {
        id: primaryButtonClickSpy
        target: primaryButton
        signalName: "clicked"
    }

    function init() {
        SettingsController.themeMode = 0
        primaryButton.available = true
        primaryButton.loading = false
        textField.error = false
        textField.errorMessage = ""
        primaryButtonClickSpy.clear()
        mouseMove(testCase, testCase.width - 1, testCase.height - 1)
        primaryButton.forceActiveFocus()
        wait(0)
    }

    function test_controls_use_shared_geometry_and_typography() {
        compare(primaryButton.implicitHeight, Theme.controlHeight)
        compare(iconButton.implicitWidth, Theme.controlHeightCompact)
        compare(iconButton.implicitHeight, Theme.controlHeightCompact)
        compare(textField.implicitHeight, Theme.controlHeight)
        compare(textField.font.pixelSize, Theme.fontSizeBody)
        compare(slider.implicitHeight, Theme.controlHeight)
        compare(slider.background.height, Theme.sliderTrackHeight)
        compare(slider.handle.width, Theme.sliderHandleExtent)
        compare(slider.handle.height, Theme.sliderHandleExtent)
        compare(standardRow.implicitHeight, Theme.listRowHeight)
        compare(mediaRow.implicitHeight, Theme.mediaListRowHeight)
        compare(tabButton.implicitHeight, Theme.navigationRowHeight)
        compare(checkBox.implicitHeight, Theme.controlHeight)
        compare(radioButton.implicitHeight, Theme.controlHeight)
        compare(radioButton.indicator.border.color.toString(),
                Theme.accent.toString())
        compare(switchControl.implicitHeight, Theme.controlHeight)
        compare(rangeSlider.implicitHeight, Theme.controlHeight)
        compare(comboBox.implicitHeight, Theme.controlHeight)
        compare(themedDialog.background.radius, Theme.radiusMd)
        compare(themedToolTip.background.radius, Theme.radiusSm)
        compare(themedScrollBar.implicitWidth,
                Theme.minimumInteractionExtent)
    }

    function test_primary_button_and_selection_follow_theme_states() {
        compare(primaryButton.background.radius, Theme.radiusSm)
        tryCompare(primaryButton.background, "color", Theme.accent)
        compare(standardRow.background.color.toString(),
                Theme.selectedSurface.toString())

        primaryButton.available = false
        tryCompare(primaryButton.background, "color", Theme.disabled)
        primaryButton.available = true

        SettingsController.themeMode = 1
        tryCompare(Theme, "isLight", true)
        tryCompare(primaryButton.background, "color", Theme.accent)
        tryCompare(standardRow.background, "color", Theme.selectedSurface)
    }

    function test_radio_button_exposes_pressed_state() {
        mousePress(radioButton, 9, radioButton.height / 2, Qt.LeftButton)
        tryCompare(radioButton, "down", true)
        compare(radioButton.indicator.color.toString(),
                Theme.surfacePressed.toString())
        mouseRelease(radioButton, 9, radioButton.height / 2, Qt.LeftButton)
        tryCompare(radioButton, "down", false)
    }

    function test_controls_expose_accessible_names_and_focus() {
        compare(iconButton.Accessible.name, "设置")
        primaryButton.forceActiveFocus()
        tryVerify(function() { return primaryButton.activeFocus })
        compare(primaryButton.background.border.width, 2)
        compare(primaryButton.background.border.color.toString(),
                Theme.focus.toString())
    }

    function test_button_hover_pressed_loading_and_input_error_states() {
        mouseMove(primaryButton, primaryButton.width / 2,
                  primaryButton.height / 2)
        tryVerify(function() { return primaryButton.hovered })
        tryCompare(primaryButton.background, "color", Theme.accentHover)

        mousePress(primaryButton, primaryButton.width / 2,
                   primaryButton.height / 2)
        tryVerify(function() { return primaryButton.down })
        tryCompare(primaryButton.background, "color", Theme.accentPressed)
        mouseRelease(primaryButton, primaryButton.width / 2,
                     primaryButton.height / 2)

        const originalWidth = primaryButton.implicitWidth
        primaryButtonClickSpy.clear()
        primaryButton.loading = true
        tryCompare(primaryButton.background, "color", Theme.disabled)
        compare(primaryButton.enabled, false)
        compare(primaryButton.implicitWidth, originalWidth)
        const busyIndicator = findChild(primaryButton,
                                        "themedButtonBusyIndicator")
        verify(busyIndicator && busyIndicator.running)
        mouseClick(primaryButton, primaryButton.width / 2,
                   primaryButton.height / 2)
        compare(primaryButtonClickSpy.count, 0)
        primaryButton.loading = false

        textField.errorMessage = "无效内容"
        textField.error = true
        const inputFrame = findChild(textField, "themedTextFieldFrame")
        const errorLabel = findChild(textField, "themedTextFieldErrorLabel")
        compare(inputFrame.border.color.toString(),
                Theme.danger.toString())
        compare(textField.Accessible.description, "无效内容")
        verify(errorLabel && errorLabel.visible)
        compare(errorLabel.text, "无效内容")
        verify(textField.implicitHeight > Theme.controlHeight)
        textField.error = false
    }

    function test_shared_controls_enable_desktop_hover_and_keyboard_focus() {
        compare(textField.hoverEnabled, true)
        compare(checkBox.hoverEnabled, true)
        compare(switchControl.hoverEnabled, true)
        compare(rangeSlider.hoverEnabled, true)
        compare(comboBox.hoverEnabled, true)
        compare(tabButton.hoverEnabled, true)
        compare(checkBox.focusPolicy, Qt.StrongFocus)
        compare(switchControl.focusPolicy, Qt.StrongFocus)
        compare(rangeSlider.focusPolicy, Qt.StrongFocus)
        compare(comboBox.focusPolicy, Qt.StrongFocus)
    }
}
