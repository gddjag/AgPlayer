import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgPlayer

Control {
    id: root
    property color colorValue: "#000000" // theme-color-allow: color picker value domain
    property color defaultColor: colorValue
    property string targetProperty: ""
    property bool showText: true
    property bool livePreview: false
    property string colorLabel: qsTr("颜色")
    readonly property bool pickerVisible: picker.visible
    signal colorEdited(string value)
    signal colorPreviewed(string value)

    onColorEdited: function(value) {
        if (targetProperty.length > 0)
            SettingsController[targetProperty] = value
    }

    implicitWidth: showText ? 106 : 32
    implicitHeight: 32
    padding: 1
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: colorLabel + " " + root.normalized(root.colorValue)
    hoverEnabled: true
    ToolTip.visible: hovered || activeFocus
    ToolTip.text: Accessible.name
    Keys.onSpacePressed: openPicker()
    Keys.onReturnPressed: openPicker()

    function normalized(value) {
        var text = String(value || "").trim().toUpperCase()
        return /^#[0-9A-F]{6}$/.test(text) ? text : ""
    }

    function openPicker() {
        picker.setWorkingColor(root.colorValue)
        picker.open()
    }

    contentItem: RowLayout {
        spacing: root.showText ? 6 : 0

        Rectangle {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            Layout.alignment: Qt.AlignCenter
            radius: 4
            color: root.colorValue
            border.color: Theme.border
            TapHandler { onTapped: root.openPicker() }
        }

        TextField {
            id: field
            visible: root.showText
            Layout.fillWidth: root.showText
            text: root.colorValue.toString().toUpperCase()
            selectByMouse: true
            color: acceptableInput ? Theme.primaryText : Theme.favoriteRed
            validator: RegularExpressionValidator {
                regularExpression: /^#[0-9A-Fa-f]{6}$/
            }
            onEditingFinished: {
                var value = root.normalized(text)
                if (value.length > 0) root.colorEdited(value)
                else text = root.colorValue.toString().toUpperCase()
            }
            background: null
        }
    }

    background: Rectangle {
        color: Theme.background
        border.color: !root.showText || field.acceptableInput
                      ? Theme.border : Theme.favoriteRed
        radius: Theme.radiusSm
    }

    Popup {
        id: picker
        objectName: "colorFieldPicker"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(296, parent ? parent.width - 24 : 296)
        height: Math.min(312, parent ? parent.height - 24 : 312)
        padding: 10
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property real hue: 0
        property real saturation: 0
        property real brightness: 0
        property color workingColor: root.colorValue
        onOpened: Qt.callLater(function() {
            if (picker.visible)
                confirmButton.forceActiveFocus(Qt.PopupFocusReason)
        })

        function updateWorkingColor() {
            workingColor = Qt.hsva(hue, saturation, brightness, 1.0)
            hexField.text = workingColor.toString().toUpperCase()
            previewWorkingColor()
        }

        function previewWorkingColor() {
            if (root.livePreview && visible)
                root.colorPreviewed(workingColor.toString().toUpperCase())
        }

        function setWorkingColor(value) {
            workingColor = value
            hue = workingColor.hsvHue >= 0 ? workingColor.hsvHue : 0
            saturation = workingColor.hsvSaturation
            brightness = workingColor.hsvValue
            hexField.text = workingColor.toString().toUpperCase()
            previewWorkingColor()
        }

        function applyHexText(value) {
            var normalized = root.normalized(value)
            if (normalized.length > 0) {
                setWorkingColor(normalized)
                return true
            }
            hexField.text = workingColor.toString().toUpperCase()
            return false
        }

        function restoreDefault() {
            setWorkingColor(root.defaultColor)
        }

        function acceptColor() {
            root.colorEdited(workingColor.toString().toUpperCase())
            close()
        }

        background: Rectangle {
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            radius: Theme.radiusMd
        }

        contentItem: ColumnLayout {
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("选择颜色")
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 22
                    radius: 4
                    color: picker.workingColor
                    border.color: Theme.border
                }
            }

            Item {
                objectName: "colorPickerSaturationValue"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 86

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSm
                    color: Qt.hsva(picker.hue, 1, 1, 1)
                    layer.enabled: true

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "white" }
                            GradientStop { position: 1; color: "transparent" }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 1; color: "black" }
                        }
                    }
                }

                Rectangle {
                    x: Math.max(0, Math.min(parent.width - width,
                                            picker.saturation * parent.width
                                            - width / 2))
                    y: Math.max(0, Math.min(parent.height - height,
                                            (1 - picker.brightness) * parent.height
                                            - height / 2))
                    width: 14
                    height: 14
                    radius: 7
                    color: "transparent"
                    border.color: "white"
                    border.width: 2
                }

                MouseArea {
                    anchors.fill: parent
                    function updateColor(mouse) {
                        picker.saturation = Math.max(0, Math.min(1,
                                                   mouse.x / Math.max(1, width)))
                        picker.brightness = Math.max(0, Math.min(1,
                                                   1 - mouse.y / Math.max(1, height)))
                        picker.updateWorkingColor()
                    }
                    onPressed: function(mouse) { updateColor(mouse) }
                    onPositionChanged: function(mouse) {
                        if (pressed) updateColor(mouse)
                    }
                }
            }

            Slider {
                id: hueSlider
                objectName: "colorPickerHueSlider"
                Layout.fillWidth: true
                Layout.preferredHeight: 22
                from: 0
                to: 1
                value: picker.hue
                onMoved: {
                    picker.hue = value
                    picker.updateWorkingColor()
                }
                background: Rectangle {
                    x: hueSlider.leftPadding
                    y: hueSlider.topPadding + hueSlider.availableHeight / 2 - height / 2
                    width: hueSlider.availableWidth
                    height: 8
                    radius: 4
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.00; color: "#ff0000" } // theme-color-allow: color picker hue spectrum
                        GradientStop { position: 0.17; color: "#ffff00" } // theme-color-allow: color picker hue spectrum
                        GradientStop { position: 0.33; color: "#00ff00" } // theme-color-allow: color picker hue spectrum
                        GradientStop { position: 0.50; color: "#00ffff" } // theme-color-allow: color picker hue spectrum
                        GradientStop { position: 0.67; color: "#0000ff" } // theme-color-allow: color picker hue spectrum
                        GradientStop { position: 0.83; color: "#ff00ff" } // theme-color-allow: color picker hue spectrum
                        GradientStop { position: 1.00; color: "#ff0000" } // theme-color-allow: color picker hue spectrum
                    }
                }
                handle: Rectangle {
                    x: hueSlider.leftPadding + hueSlider.visualPosition
                       * (hueSlider.availableWidth - width)
                    y: hueSlider.topPadding
                       + hueSlider.availableHeight / 2 - height / 2
                    width: Theme.sliderHandleExtent
                    height: Theme.sliderHandleExtent
                    radius: width / 2
                    color: Theme.controlHandle
                    border.width: 1
                    border.color: hueSlider.activeFocus ? Theme.focus
                                                        : Theme.borderStrong
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("色号")
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
                }

                ThemedTextField {
                    id: hexField
                    objectName: "colorPickerHexField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    selectByMouse: true
                    placeholderText: "#RRGGBB"
                    error: !acceptableInput
                    font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption
                    horizontalAlignment: TextInput.AlignHCenter
                    validator: RegularExpressionValidator {
                        regularExpression: /^#[0-9A-Fa-f]{6}$/
                    }
                    onAccepted: picker.applyHexText(text)
                    onEditingFinished: picker.applyHexText(text)
                    Accessible.name: qsTr("十六进制色号，可复制粘贴")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                Repeater {
                    model: [
                        { name: "H", value: Math.round(picker.hue * 360) + "°" },
                        { name: "S", value: Math.round(picker.saturation * 100) + "%" },
                        { name: "V", value: Math.round(picker.brightness * 100) + "%" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "colorPicker" + modelData.name + "Value"
                        Layout.preferredWidth: 72
                        Layout.minimumWidth: 64
                        Layout.maximumWidth: 72
                        Layout.preferredHeight: 25
                        color: Theme.background
                        border.color: Theme.border
                        radius: Theme.radiusSm
                        Text {
                            anchors.centerIn: parent
                            text: modelData.name + " " + modelData.value
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    objectName: "colorPickerRestoreButton"
                    Layout.preferredWidth: 76
                    Layout.maximumWidth: 76
                    Layout.preferredHeight: 28
                    text: qsTr("恢复默认")
                    onClicked: picker.restoreDefault()
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "colorPickerCancelButton"
                    Layout.preferredWidth: 58
                    Layout.maximumWidth: 58
                    Layout.preferredHeight: 28
                    text: qsTr("取消")
                    onClicked: picker.close()
                }
                Button {
                    id: confirmButton
                    objectName: "colorPickerConfirmButton"
                    Layout.preferredWidth: 58
                    Layout.maximumWidth: 58
                    Layout.preferredHeight: 28
                    text: qsTr("确定")
                    focus: true
                    onClicked: picker.acceptColor()
                }
            }
        }
    }
}
