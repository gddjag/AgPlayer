import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Popup {
    id: root

    objectName: "agColorPicker"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(292, Overlay.overlay ? Overlay.overlay.width - 20 : 292)
    height: Math.min(248, Overlay.overlay ? Overlay.overlay.height - 20 : 248)
    margins: 10
    padding: 10

    property color workingColor: "#D27722"
    property string editingLabel: ""
    property bool closeHandled: false
    property bool controlsReady: false

    readonly property string workingHex: normalizeHex(workingColor)
    readonly property real hue: workingColor.hsvHue < 0 ? 0 : workingColor.hsvHue
    readonly property real saturation: workingColor.hsvSaturation
    readonly property real brightness: workingColor.hsvValue

    signal applied(color color)
    signal cancelled()

    function normalizeHex(value) {
        var text = String(value || "").trim().toUpperCase()
        if (/^#[0-9A-F]{6}$/.test(text))
            return text
        if (/^#[0-9A-F]{3}$/.test(text))
            return "#" + text.charAt(1) + text.charAt(1)
                    + text.charAt(2) + text.charAt(2)
                    + text.charAt(3) + text.charAt(3)
        if (value && value.r !== undefined) {
            var channel = function(component) {
                return Math.max(0, Math.min(255, Math.round(component * 255)))
                        .toString(16).padStart(2, "0").toUpperCase()
            }
            return "#" + channel(value.r) + channel(value.g) + channel(value.b)
        }
        return ""
    }

    function rgb() {
        return {
            r: Math.round(workingColor.r * 255),
            g: Math.round(workingColor.g * 255),
            b: Math.round(workingColor.b * 255)
        }
    }

    function setWorkingHex(value) {
        var normalized = normalizeHex(value)
        if (!normalized.length)
            return false
        workingColor = normalized
        return true
    }

    function setRgb(red, green, blue) {
        if (!Number.isInteger(red) || !Number.isInteger(green)
                || !Number.isInteger(blue) || red < 0 || red > 255
                || green < 0 || green > 255 || blue < 0 || blue > 255)
            return false
        workingColor = Qt.rgba(red / 255, green / 255, blue / 255, 1)
        return true
    }

    function setRgbChannel(channel, value) {
        var number = Number(value)
        if (!Number.isInteger(number) || number < 0 || number > 255)
            return false
        var channels = rgb()
        channels[channel] = number
        return setRgb(channels.r, channels.g, channels.b)
    }

    function validRgbInput(input) {
        return /^(0|[1-9][0-9]{0,2})$/.test(input.text)
                && Number(input.text) >= 0 && Number(input.text) <= 255
    }

    function synchronizeInputs() {
        if (!controlsReady)
            return
        var channels = rgb()
        if (!hexInput.activeFocus)
            hexInput.text = workingHex
        if (!redInput.activeFocus)
            redInput.text = String(channels.r)
        if (!greenInput.activeFocus)
            greenInput.text = String(channels.g)
        if (!blueInput.activeFocus)
            blueInput.text = String(channels.b)
    }

    function setHsv(nextHue, nextSaturation, nextBrightness) {
        workingColor = Qt.hsva(Math.max(0, Math.min(1, nextHue)),
                               Math.max(0, Math.min(1, nextSaturation)),
                               Math.max(0, Math.min(1, nextBrightness)), 1)
    }

    function setSvAt(pointX, pointY) {
        setHsv(hue, Math.max(0, Math.min(1, pointX / svPlane.width)),
               Math.max(0, Math.min(1, 1 - pointY / svPlane.height)))
    }

    function setHueAt(pointY) {
        setHsv(Math.max(0, Math.min(1, pointY / hueRail.height)), saturation,
               brightness)
    }

    function positionInsideOverlay() {
        var overlay = Overlay.overlay
        if (!overlay)
            return
        x = Math.max(10, Math.round((overlay.width - width) / 2))
        y = Math.max(10, Math.round((overlay.height - height) / 2))
    }

    function openForColor(initialColor, label) {
        var normalized = normalizeHex(initialColor)
        workingColor = normalized.length ? normalized : "#000000"
        editingLabel = label || ""
        closeHandled = false
        synchronizeInputs()
        positionInsideOverlay()
        open()
    }

    function applyWorkingColor() {
        if (closeHandled)
            return
        var normalizedHex = normalizeHex(hexInput.text)
        if (!normalizedHex.length || !validRgbInput(redInput)
                || !validRgbInput(greenInput) || !validRgbInput(blueInput))
            return
        setRgb(Number(redInput.text), Number(greenInput.text),
               Number(blueInput.text))
        closeHandled = true
        applied(workingColor)
        close()
    }

    function cancelPicker() {
        if (closeHandled)
            return
        closeHandled = true
        cancelled()
        close()
    }

    Component.onCompleted: {
        controlsReady = true
        synchronizeInputs()
    }
    onWorkingColorChanged: synchronizeInputs()
    onOpened: positionInsideOverlay()
    onClosed: {
        if (!closeHandled) {
            closeHandled = true
            cancelled()
        }
    }

    background: Rectangle {
        radius: 12
        color: Theme.elevated
        border.width: 1
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 5

        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 16 : 0
            text: root.editingLabel
            visible: text.length > 0
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            spacing: 6

            Item {
                id: svPlane
                objectName: "colorPickerSvPlane"
                Layout.fillWidth: true
                Layout.fillHeight: true
                activeFocusOnTab: true
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("色相饱和度和明度")
                KeyNavigation.tab: hueRail
                KeyNavigation.priority: KeyNavigation.BeforeItem

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#FFFFFF" }
                        GradientStop { position: 1; color: Qt.hsva(root.hue, 1, 1, 1) }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: "#000000" }
                    }
                }
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    x: Math.max(0, Math.min(svPlane.width - width,
                                             root.saturation * svPlane.width - width / 2))
                    y: Math.max(0, Math.min(svPlane.height - height,
                                             (1 - root.brightness) * svPlane.height - height / 2))
                    color: "transparent"
                    border.width: svPlane.activeFocus ? 2 : 1
                    border.color: root.brightness > 0.55 ? "#1B1B1B" : "#FFFFFF"
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: function(mouse) { root.setSvAt(mouse.x, mouse.y) }
                    onPositionChanged: function(mouse) {
                        if (pressed)
                            root.setSvAt(mouse.x, mouse.y)
                    }
                }
                Keys.onLeftPressed: function(event) {
                    root.setHsv(root.hue, root.saturation - 0.02, root.brightness)
                    event.accepted = true
                }
                Keys.onRightPressed: function(event) {
                    root.setHsv(root.hue, root.saturation + 0.02, root.brightness)
                    event.accepted = true
                }
                Keys.onUpPressed: function(event) {
                    root.setHsv(root.hue, root.saturation, root.brightness + 0.02)
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) {
                    root.setHsv(root.hue, root.saturation, root.brightness - 0.02)
                    event.accepted = true
                }
            }

            Item {
                id: hueRail
                objectName: "colorPickerHueRail"
                Layout.preferredWidth: 14
                Layout.fillHeight: true
                activeFocusOnTab: true
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("色相")
                KeyNavigation.tab: hexInput
                KeyNavigation.priority: KeyNavigation.BeforeItem

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: "#FF0000" }
                        GradientStop { position: 0.17; color: "#FFFF00" }
                        GradientStop { position: 0.33; color: "#00FF00" }
                        GradientStop { position: 0.50; color: "#00FFFF" }
                        GradientStop { position: 0.67; color: "#0000FF" }
                        GradientStop { position: 0.83; color: "#FF00FF" }
                        GradientStop { position: 1.0; color: "#FF0000" }
                    }
                }
                Rectangle {
                    x: -2
                    y: Math.max(0, Math.min(hueRail.height - height,
                                             root.hue * hueRail.height - height / 2))
                    width: hueRail.width + 4
                    height: 3
                    radius: 2
                    color: "#FFFFFF"
                    border.width: 1
                    border.color: "#1B1B1B"
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: function(mouse) { root.setHueAt(mouse.y) }
                    onPositionChanged: function(mouse) {
                        if (pressed)
                            root.setHueAt(mouse.y)
                    }
                }
                Keys.onUpPressed: function(event) {
                    root.setHsv(root.hue - 0.02, root.saturation, root.brightness)
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) {
                    root.setHsv(root.hue + 0.02, root.saturation, root.brightness)
                    event.accepted = true
                }
            }

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 5
                color: root.workingColor
                border.width: 1
                border.color: Theme.border
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("当前颜色 %1").arg(root.workingHex)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            radius: 5
            color: Theme.panel
            border.width: 1
            border.color: hexInput.activeFocus ? Theme.focus : Theme.border

            TextInput {
                id: hexInput
                objectName: "colorPickerHex"
                anchors.fill: parent
                leftPadding: 7
                rightPadding: 7
                text: root.workingHex
                color: Theme.primaryText
                selectionColor: Theme.hoverSurface
                selectedTextColor: Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.DemiBold
                verticalAlignment: TextInput.AlignVCenter
                selectByMouse: true
                activeFocusOnTab: true
                maximumLength: 7
                inputMethodHints: Qt.ImhPreferUppercase
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("十六进制颜色")
                KeyNavigation.tab: redInput
                KeyNavigation.priority: KeyNavigation.BeforeItem
                Keys.priority: Keys.BeforeItem
                onEditingFinished: root.setWorkingHex(text)
                onAccepted: root.setWorkingHex(text)
                Keys.onReturnPressed: function(event) {
                    root.setWorkingHex(text)
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    root.setWorkingHex(text)
                    event.accepted = true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            spacing: 5

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 5
                color: Theme.panel
                border.width: 1
                border.color: redInput.activeFocus ? Theme.focus : Theme.border
                TextInput {
                    id: redInput
                    objectName: "colorPickerR"
                    anchors.fill: parent
                    text: String(root.rgb().r)
                    color: Theme.primaryText
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    selectByMouse: true
                    activeFocusOnTab: true
                    maximumLength: 3
                    validator: IntValidator { bottom: 0; top: 255 }
                    inputMethodHints: Qt.ImhDigitsOnly
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("红色通道")
                    KeyNavigation.tab: greenInput
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    Keys.priority: Keys.BeforeItem
                    onTextChanged: if (activeFocus && acceptableInput) root.setRgbChannel("r", text)
                    onEditingFinished: root.setRgbChannel("r", text)
                    onAccepted: root.setRgbChannel("r", text)
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            root.setRgbChannel("r", text)
                            event.accepted = true
                        }
                    }
                    Keys.onReturnPressed: function(event) { root.setRgbChannel("r", text); event.accepted = true }
                    Keys.onEnterPressed: function(event) { root.setRgbChannel("r", text); event.accepted = true }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 5
                color: Theme.panel
                border.width: 1
                border.color: greenInput.activeFocus ? Theme.focus : Theme.border
                TextInput {
                    id: greenInput
                    objectName: "colorPickerG"
                    anchors.fill: parent
                    text: String(root.rgb().g)
                    color: Theme.primaryText
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    selectByMouse: true
                    activeFocusOnTab: true
                    maximumLength: 3
                    validator: IntValidator { bottom: 0; top: 255 }
                    inputMethodHints: Qt.ImhDigitsOnly
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("绿色通道")
                    KeyNavigation.tab: blueInput
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    Keys.priority: Keys.BeforeItem
                    onTextChanged: if (activeFocus && acceptableInput) root.setRgbChannel("g", text)
                    onEditingFinished: root.setRgbChannel("g", text)
                    onAccepted: root.setRgbChannel("g", text)
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            root.setRgbChannel("g", text)
                            event.accepted = true
                        }
                    }
                    Keys.onReturnPressed: function(event) { root.setRgbChannel("g", text); event.accepted = true }
                    Keys.onEnterPressed: function(event) { root.setRgbChannel("g", text); event.accepted = true }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 5
                color: Theme.panel
                border.width: 1
                border.color: blueInput.activeFocus ? Theme.focus : Theme.border
                TextInput {
                    id: blueInput
                    objectName: "colorPickerB"
                    anchors.fill: parent
                    text: String(root.rgb().b)
                    color: Theme.primaryText
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    selectByMouse: true
                    activeFocusOnTab: true
                    maximumLength: 3
                    validator: IntValidator { bottom: 0; top: 255 }
                    inputMethodHints: Qt.ImhDigitsOnly
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("蓝色通道")
                    KeyNavigation.tab: applyButton
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    Keys.priority: Keys.BeforeItem
                    onTextChanged: if (activeFocus && acceptableInput) root.setRgbChannel("b", text)
                    onEditingFinished: root.setRgbChannel("b", text)
                    onAccepted: root.setRgbChannel("b", text)
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            root.setRgbChannel("b", text)
                            event.accepted = true
                        }
                    }
                    Keys.onReturnPressed: function(event) { root.setRgbChannel("b", text); event.accepted = true }
                    Keys.onEnterPressed: function(event) { root.setRgbChannel("b", text); event.accepted = true }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 27
            spacing: 6

            AbstractButton {
                id: cancelButton
                objectName: "colorPickerCancel"
                Layout.fillWidth: true
                Layout.fillHeight: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("取消颜色更改")
                KeyNavigation.tab: svPlane
                KeyNavigation.priority: KeyNavigation.BeforeItem
                onClicked: root.cancelPicker()
                contentItem: Text {
                    text: qsTr("取消")
                    color: Theme.primaryText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                }
                background: Rectangle {
                    radius: 5
                    color: cancelButton.hovered ? Theme.hoverSurface : Theme.panel
                    border.width: cancelButton.visualFocus ? 2 : 1
                    border.color: cancelButton.visualFocus ? Theme.focus : Theme.border
                }
            }

            AbstractButton {
                id: applyButton
                objectName: "colorPickerApply"
                Layout.fillWidth: true
                Layout.fillHeight: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("应用颜色")
                KeyNavigation.tab: cancelButton
                KeyNavigation.priority: KeyNavigation.BeforeItem
                onClicked: root.applyWorkingColor()
                Keys.onReturnPressed: function(event) {
                    root.applyWorkingColor()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    root.applyWorkingColor()
                    event.accepted = true
                }
                contentItem: Text {
                    text: qsTr("应用")
                    color: Theme.isLight ? "#FFFFFF" : "#1B1B1B"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                background: Rectangle {
                    radius: 5
                    color: Theme.accent
                    border.width: applyButton.visualFocus ? 2 : 0
                    border.color: Theme.focus
                }
            }
        }
    }
}
