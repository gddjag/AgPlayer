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
    }

    function init() {
        SettingsController.themeMode = 0
        primaryButton.enabled = true
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
    }

    function test_primary_button_and_selection_follow_theme_states() {
        compare(primaryButton.background.radius, Theme.radiusSm)
        compare(primaryButton.background.color.toString(),
                Theme.accent.toString())
        compare(standardRow.background.color.toString(),
                Theme.selectedSurface.toString())

        primaryButton.enabled = false
        tryCompare(primaryButton.background, "color", Theme.disabled)
        primaryButton.enabled = true

        SettingsController.themeMode = 1
        tryCompare(Theme, "isLight", true)
        tryCompare(primaryButton.background, "color", Theme.accent)
        tryCompare(standardRow.background, "color", Theme.selectedSurface)
    }

    function test_controls_expose_accessible_names_and_focus() {
        compare(iconButton.Accessible.name, "设置")
        primaryButton.forceActiveFocus()
        tryVerify(function() { return primaryButton.activeFocus })
        compare(primaryButton.background.border.width, 2)
        compare(primaryButton.background.border.color.toString(),
                Theme.focus.toString())
    }
}
