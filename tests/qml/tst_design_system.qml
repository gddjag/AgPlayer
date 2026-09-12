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
        compare(iconButton.iconSize, Theme.iconSizeMd)
        compare(textField.implicitHeight, Theme.controlHeight)
        compare(textField.font.pixelSize, Theme.fontSizeBody)
        compare(slider.implicitHeight, Theme.controlHeight)
        compare(slider.background.height, Theme.sliderTrackHeight)
        compare(slider.handle.width, Theme.sliderHandleExtent)
        compare(slider.handle.height, Theme.sliderHandleExtent)
        compare(standardRow.implicitHeight, Theme.listRowHeight)
        compare(mediaRow.implicitHeight, Theme.mediaListRowHeight)
        compare(tabButton.implicitHeight, Theme.navigationRowHeight)
        compare(tabButton.iconSize, Theme.iconSizeMd)
        compare(checkBox.implicitHeight, Theme.controlHeight)
        compare(radioButton.implicitHeight, Theme.controlHeight)
        compare(radioButton.indicator.border.color.toString(),
                Theme.accent.toString())
        compare(switchControl.implicitHeight, Theme.controlHeight)
        compare(rangeSlider.implicitHeight, Theme.controlHeight)
        compare(comboBox.implicitHeight, Theme.controlHeight)
        compare(themedDialog.background.radius, Theme.radiusMd)
        compare(themedDialog.font.family, Theme.fontPrimary)
        compare(themedDialog.font.pixelSize, Theme.fontSizeBody)
        compare(themedToolTip.background.radius, Theme.radiusSm)
        compare(themedScrollBar.implicitWidth,
                Theme.minimumInteractionExtent)
    }

    function test_text_field_frame_follows_compact_height_and_preserves_error_footer() {
        var component = Qt.createComponent(Qt.resolvedUrl(
                    "../../app/qml/AgPlayer/components/ThemedTextField.qml"))
        compare(component.status, Component.Ready, component.errorString())
        var field = component.createObject(testCase, {"width": 160, "placeholderText": "输入"})
        verify(field)
        try {
            var frame = findChild(field, "themedTextFieldFrame")
            var footer = findChild(field, "themedTextFieldErrorLabel")
            compare(frame.height, Theme.controlHeight)
            field.height = 24
            compare(frame.height, 24)
            field.error = true
            field.errorMessage = "无效内容，请重新输入"
            wait(0)
            field.height = 24 + Theme.spacingXs + footer.implicitHeight
            compare(frame.height, 24)
            verify(footer.visible)
            compare(footer.y, frame.height + Theme.spacingXs)
            verify(footer.y + footer.height <= field.height)
            field.height = field.implicitHeight
            compare(frame.height, Theme.controlHeight)
            verify(footer.y + footer.height <= field.height)
        } finally {
            field.destroy()
        }
    }

    function test_text_field_placeholder_tracks_empty_and_input_states() {
        var component = Qt.createComponent(Qt.resolvedUrl(
                    "../../app/qml/AgPlayer/components/ThemedTextField.qml"))
        if (component.status === Component.Loading)
            tryCompare(component, "status", Component.Ready, 3000)
        compare(component.status, Component.Ready, component.errorString())
        var field = component.createObject(testCase, {
            "width": 180,
            "placeholderText": "搜索歌曲、艺术家、专辑或文件夹"
        })
        verify(field)
        try {
            var placeholder = findChild(field, "themedTextFieldPlaceholder")
            verify(placeholder)
            compare(placeholder.visible, true)
            compare(placeholder.text, field.placeholderText)
            compare(placeholder.font.family, field.font.family)
            compare(placeholder.width,
                    field.width - field.leftPadding - field.rightPadding)
            compare(placeholder.height,
                    field.height - field.topPadding - field.bottomPadding)
            verify(placeholder.truncated,
                   "long placeholder text must elide inside the input padding")

            field.text = "gapless"
            wait(0)
            compare(placeholder.visible, false)

            field.clear()
            wait(0)
            compare(placeholder.visible, true)

            field.enabled = false
            compare(placeholder.color.toString(),
                    Theme.textDisabled.toString())
            field.enabled = true
            field.errorMessage = "无效内容"
            field.error = true
            wait(0)
            var errorLabel = findChild(field, "themedTextFieldErrorLabel")
            verify(errorLabel && errorLabel.visible)
            compare(placeholder.height,
                    field.height - field.topPadding - field.bottomPadding)
            verify(placeholder.y + placeholder.height <= errorLabel.y,
                   "placeholder must stay above the error footer")
        } finally {
            field.destroy()
        }
    }

    function test_button_label_inherits_font_and_elides_to_available_width() {
        const originalText = primaryButton.text
        const originalWidth = primaryButton.width
        const originalPixelSize = primaryButton.font.pixelSize
        const originalWeight = primaryButton.font.weight
        const originalBold = primaryButton.font.bold
        primaryButton.text = "这是一个需要在窄按钮内截断的很长操作名称"
        primaryButton.width = 96
        primaryButton.font.pixelSize = 18
        wait(0)

        let label = null
        for (const child of primaryButton.contentItem.children) {
            if (child.text !== undefined && child.text === primaryButton.text) {
                label = child
                break
            }
        }
        verify(label, "ThemedButton must expose its rendered text label")
        compare(label.font.pixelSize, primaryButton.font.pixelSize)

        compare(label.font.weight, Font.Medium,
                "primary buttons must keep their default medium label weight")
        primaryButton.font.bold = true
        wait(0)
        verify(primaryButton.font.bold)
        verify(label.font.bold,
               "button labels must preserve a caller's font.bold override")

        primaryButton.font.bold = false
        primaryButton.font.weight = Font.DemiBold
        wait(0)
        compare(label.font.weight, primaryButton.font.weight)
        verify(label.width <= primaryButton.availableWidth + 0.5,
               "button label must stay inside the available content width")
        verify(label.truncated,
               "long button text must elide when the button is narrow")

        primaryButton.text = originalText
        primaryButton.width = originalWidth
        primaryButton.font.pixelSize = originalPixelSize
        primaryButton.font.weight = originalWeight
        primaryButton.font.bold = originalBold
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

    function test_icon_mouse_focus_has_no_border_but_keyboard_focus_remains() {
        iconButton.forceActiveFocus(Qt.MouseFocusReason)
        tryCompare(iconButton.background.border, "width", 0)
        textField.forceActiveFocus(Qt.OtherFocusReason)
        iconButton.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(iconButton.background.border, "width", 2)
        textField.forceActiveFocus(Qt.OtherFocusReason)
        iconButton.forceActiveFocus(Qt.MouseFocusReason)
        tryCompare(iconButton.background.border, "width", 0)
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
