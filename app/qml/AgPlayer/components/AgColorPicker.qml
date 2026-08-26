import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer
import "ColorScale.js" as ColorScale

Popup {
    id: root

    objectName: "agColorPicker"
    property color baseColor: "#63316B"
    property color selectedColor: "#63316B"
    readonly property string normalizedBaseHex:
        ColorScale.normalizeHex(baseColor)
    readonly property var candidateColors:
        ColorScale.buildPalette(normalizedBaseHex)
    signal colorAccepted(color color)

    property bool synchronizingControls: false

    width: Math.max(0, Math.min(360, Overlay.overlay
                                ? Overlay.overlay.width - 20 : 360))
    margins: 10
    padding: 13
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function synchronizeControls(value) {
        var normalized = ColorScale.normalizeHex(value)
        var rgb = ColorScale.hexToRgb(normalized)
        if (!rgb)
            return

        synchronizingControls = true
        hexInput.text = normalized
        redInput.text = String(rgb.r)
        greenInput.text = String(rgb.g)
        blueInput.text = String(rgb.b)
        redSlider.value = rgb.r
        greenSlider.value = rgb.g
        blueSlider.value = rgb.b
        synchronizingControls = false
    }

    function setBaseHex(value) {
        var normalized = ColorScale.normalizeHex(value)
        if (normalized.length === 0)
            return false

        if (normalized !== normalizedBaseHex)
            baseColor = normalized
        else
            synchronizeControls(baseColor)
        return true
    }

    function setRgbFromInputs() {
        var red = Number(redInput.text)
        var green = Number(greenInput.text)
        var blue = Number(blueInput.text)
        var valid = Number.isInteger(red) && red >= 0 && red <= 255
                && Number.isInteger(green) && green >= 0 && green <= 255
                && Number.isInteger(blue) && blue >= 0 && blue <= 255
        if (!valid) {
            synchronizeControls(baseColor)
            return
        }
        setBaseHex(ColorScale.rgbToHex(red, green, blue))
    }

    function restoreInvalidRgbInput(input) {
        if (input.acceptableInput)
            return false
        synchronizeControls(baseColor)
        return true
    }

    function setChannel(channel, value) {
        var rgb = ColorScale.hexToRgb(normalizedBaseHex)
        if (!rgb)
            return
        rgb[channel] = Math.round(value)
        setBaseHex(ColorScale.rgbToHex(rgb.r, rgb.g, rgb.b))
    }

    function channelEndpoint(channel, value) {
        var rgb = ColorScale.hexToRgb(normalizedBaseHex)
        if (!rgb)
            return "#000000"
        rgb[channel] = value
        return ColorScale.rgbToHex(rgb.r, rgb.g, rgb.b)
    }

    function openForColor(initialColor) {
        var normalized = ColorScale.normalizeHex(initialColor)
        var value = normalized.length > 0 ? normalized : "#000000"
        selectedColor = value
        setBaseHex(value)
        open()
    }

    onBaseColorChanged: synchronizeControls(baseColor)
    Component.onCompleted: synchronizeControls(baseColor)

    background: Item {
        property color color: Theme.elevated

        Rectangle {
            x: 2
            y: 4
            width: Math.max(0, parent.width - 4)
            height: Math.max(0, parent.height - 1)
            radius: 16
            color: "transparent"
            border.width: 3
            border.color: Theme.isLight ? "#18000000" : "#50000000"
        }

        Rectangle {
            anchors.fill: parent
            radius: 16
            color: parent.color
            border.width: 1
            border.color: Theme.border
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 9

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 31
                radius: 8
                    color: Theme.panel
                    border.width: 1
                    border.color: hexInput.activeFocus ? Theme.focus
                                                   : Theme.border

                TextInput {
                    id: hexInput
                    objectName: "colorPickerHex"
                    anchors.fill: parent
                    leftPadding: 9
                    rightPadding: 9
                    color: Theme.primaryText
                    selectionColor: Theme.hoverSurface
                    selectedTextColor: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    verticalAlignment: TextInput.AlignVCenter
                    selectByMouse: true
                    activeFocusOnTab: true
                    maximumLength: 7
                    inputMethodHints: Qt.ImhPreferUppercase
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("十六进制颜色")
                    KeyNavigation.tab: closeButton
                    KeyNavigation.priority: KeyNavigation.BeforeItem

                    onEditingFinished: {
                        if (!root.setBaseHex(text))
                            text = root.normalizedBaseHex
                    }
                }
            }

            AbstractButton {
                id: closeButton
                objectName: "colorPickerClose"
                Layout.preferredWidth: 25
                Layout.preferredHeight: 25
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("关闭颜色选择器")
                KeyNavigation.tab: redInput
                KeyNavigation.priority: KeyNavigation.BeforeItem
                onClicked: root.close()
                Keys.onSpacePressed: function(event) {
                    closeButton.clicked()
                    event.accepted = true
                }
                Keys.onReturnPressed: function(event) {
                    closeButton.clicked()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    closeButton.clicked()
                    event.accepted = true
                }

                contentItem: Text {
                    text: "×"
                    color: Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 20
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: width / 2
                    color: closeButton.hovered ? Theme.hoverSurface
                                               : "transparent"
                    border.width: closeButton.visualFocus ? 2 : 0
                    border.color: Theme.focus
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            radius: 9
            color: Theme.panel
            border.width: 1
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 4
                        color: "transparent"
                        border.width: redInput.activeFocus ? 1 : 0
                        border.color: Theme.accent
                    }
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            text: "R"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }
                        TextInput {
                            id: redInput
                            objectName: "colorPickerR"
                            width: 34
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            horizontalAlignment: TextInput.AlignHCenter
                            selectByMouse: true
                            activeFocusOnTab: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            Accessible.role: Accessible.EditableText
                            Accessible.name: qsTr("红色通道")
                            KeyNavigation.tab: greenInput
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            onEditingFinished: root.setRgbFromInputs()
                            onActiveFocusChanged: {
                                if (!activeFocus)
                                    root.restoreInvalidRgbInput(redInput)
                            }
                            Keys.onReturnPressed: function(event) {
                                event.accepted = root.restoreInvalidRgbInput(redInput)
                            }
                            Keys.onEnterPressed: function(event) {
                                event.accepted = root.restoreInvalidRgbInput(redInput)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 18
                    color: Theme.border
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 4
                        color: "transparent"
                        border.width: greenInput.activeFocus ? 1 : 0
                        border.color: Theme.accent
                    }
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            text: "G"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }
                        TextInput {
                            id: greenInput
                            objectName: "colorPickerG"
                            width: 34
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            horizontalAlignment: TextInput.AlignHCenter
                            selectByMouse: true
                            activeFocusOnTab: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            Accessible.role: Accessible.EditableText
                            Accessible.name: qsTr("绿色通道")
                            KeyNavigation.tab: blueInput
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            onEditingFinished: root.setRgbFromInputs()
                            onActiveFocusChanged: {
                                if (!activeFocus)
                                    root.restoreInvalidRgbInput(greenInput)
                            }
                            Keys.onReturnPressed: function(event) {
                                event.accepted = root.restoreInvalidRgbInput(greenInput)
                            }
                            Keys.onEnterPressed: function(event) {
                                event.accepted = root.restoreInvalidRgbInput(greenInput)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 18
                    color: Theme.border
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 4
                        color: "transparent"
                        border.width: blueInput.activeFocus ? 1 : 0
                        border.color: Theme.accent
                    }
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            text: "B"
                            color: Theme.secondaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 11
                        }
                        TextInput {
                            id: blueInput
                            objectName: "colorPickerB"
                            width: 34
                            color: Theme.primaryText
                            font.family: Theme.fontPrimary
                            font.pixelSize: 12
                            horizontalAlignment: TextInput.AlignHCenter
                            selectByMouse: true
                            activeFocusOnTab: true
                            validator: IntValidator { bottom: 0; top: 255 }
                            Accessible.role: Accessible.EditableText
                            Accessible.name: qsTr("蓝色通道")
                            KeyNavigation.tab: redSlider
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            onEditingFinished: root.setRgbFromInputs()
                            onActiveFocusChanged: {
                                if (!activeFocus)
                                    root.restoreInvalidRgbInput(blueInput)
                            }
                            Keys.onReturnPressed: function(event) {
                                event.accepted = root.restoreInvalidRgbInput(blueInput)
                            }
                            Keys.onEnterPressed: function(event) {
                                event.accepted = root.restoreInvalidRgbInput(blueInput)
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5

            Slider {
                id: redSlider
                objectName: "colorPickerRSlider"
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                from: 0
                to: 255
                stepSize: 1
                live: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("红色通道滑块")
                KeyNavigation.tab: greenSlider
                KeyNavigation.priority: KeyNavigation.BeforeItem
                onMoved: if (!root.synchronizingControls)
                             root.setChannel("r", value)

                background: Rectangle {
                    x: redSlider.leftPadding
                    y: redSlider.topPadding
                            + (redSlider.availableHeight - height) / 2
                    width: redSlider.availableWidth
                    height: 7
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0
                            color: root.channelEndpoint("r", 0)
                        }
                        GradientStop {
                            position: 1
                            color: root.channelEndpoint("r", 255)
                        }
                    }
                }
                handle: SliderHandle {
                    slider: redSlider
                }
            }

            Slider {
                id: greenSlider
                objectName: "colorPickerGSlider"
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                from: 0
                to: 255
                stepSize: 1
                live: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("绿色通道滑块")
                KeyNavigation.tab: blueSlider
                KeyNavigation.priority: KeyNavigation.BeforeItem
                onMoved: if (!root.synchronizingControls)
                             root.setChannel("g", value)

                background: Rectangle {
                    x: greenSlider.leftPadding
                    y: greenSlider.topPadding
                            + (greenSlider.availableHeight - height) / 2
                    width: greenSlider.availableWidth
                    height: 7
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0
                            color: root.channelEndpoint("g", 0)
                        }
                        GradientStop {
                            position: 1
                            color: root.channelEndpoint("g", 255)
                        }
                    }
                }
                handle: SliderHandle {
                    slider: greenSlider
                }
            }

            Slider {
                id: blueSlider
                objectName: "colorPickerBSlider"
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                from: 0
                to: 255
                stepSize: 1
                live: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("蓝色通道滑块")
                onMoved: if (!root.synchronizingControls)
                             root.setChannel("b", value)

                background: Rectangle {
                    x: blueSlider.leftPadding
                    y: blueSlider.topPadding
                            + (blueSlider.availableHeight - height) / 2
                    width: blueSlider.availableWidth
                    height: 7
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0
                            color: root.channelEndpoint("b", 0)
                        }
                        GradientStop {
                            position: 1
                            color: root.channelEndpoint("b", 255)
                        }
                    }
                }
                handle: SliderHandle {
                    slider: blueSlider
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 5
            columnSpacing: 5
            rowSpacing: 5

            Repeater {
                model: root.candidateColors.length

                delegate: AbstractButton {
                    id: candidateButton
                    readonly property int candidateIndex: index
                    readonly property int scaleValue:
                        [10, 20, 30, 40, 50,
                         100, 120, 140, 160, 180][candidateIndex]
                    readonly property string candidateColor:
                        root.candidateColors[candidateIndex]
                    readonly property bool selected:
                        ColorScale.normalizeHex(root.selectedColor)
                        === candidateColor

                    objectName: "colorCandidate-" + candidateIndex
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 44
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("候选颜色 %1")
                                             .arg(candidateColor) + " " + scaleValue
                    Accessible.selected: selected
                    onClicked: {
                        root.selectedColor = candidateColor
                        root.colorAccepted(root.selectedColor)
                        root.close()
                    }
                    Keys.onSpacePressed: function(event) {
                        candidateButton.clicked()
                        event.accepted = true
                    }
                    Keys.onReturnPressed: function(event) {
                        candidateButton.clicked()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        candidateButton.clicked()
                        event.accepted = true
                    }

                    background: Rectangle {
                        radius: 8
                        color: candidateButton.candidateColor
                        border.width: candidateButton.visualFocus ? 3
                                                                  : candidateButton.selected ? 2 : 1
                        border.color: candidateButton.visualFocus
                                      ? (ColorScale.isLight(candidateButton.candidateColor)
                                         ? "#1B1B1B" : "#FFFFFF")
                                      : candidateButton.selected
                                      ? (ColorScale.isLight(candidateButton.candidateColor)
                                         ? "#1B1B1B" : "#FFFFFF")
                                      : Theme.border

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 3
                            radius: 5
                            color: "transparent"
                            border.width: candidateButton.selected ? 1 : 0
                            border.color: candidateButton.selected
                                          ? (ColorScale.isLight(candidateButton.candidateColor)
                                             ? "#FFFFFF" : "#1B1B1B")
                                          : "transparent"
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: candidateButton.hovered ? Theme.hoverSurface
                                                           : "transparent"
                            opacity: candidateButton.hovered ? 0.18 : 0
                        }

                        Rectangle {
                            visible: candidateButton.selected
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 3
                            width: 14
                            height: 14
                            radius: 7
                            color: ColorScale.isLight(candidateButton.candidateColor)
                                   ? "#1B1B1B" : "#FFFFFF"
                            ThemedIcon {
                                anchors.centerIn: parent
                                source: Theme.icon("check-line")
                                tint: ColorScale.isLight(candidateButton.candidateColor)
                                      ? "#FFFFFF" : "#1B1B1B"
                                sourceSize.width: 10
                                sourceSize.height: 10
                            }
                        }
                    }

                    contentItem: Column {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: candidateButton.scaleValue
                            color: ColorScale.isLight(candidateButton.candidateColor)
                                   ? "#251028" : "#FFFFFF"
                            font.family: Theme.fontPrimary
                            font.pixelSize: 9
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: candidateButton.candidateColor
                            color: ColorScale.isLight(candidateButton.candidateColor)
                                   ? "#251028" : "#FFFFFF"
                            font.family: Theme.fontPrimary
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }
    }

    component SliderHandle: Rectangle {
        required property Slider slider
        x: slider.leftPadding
           + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + (slider.availableHeight - height) / 2
        implicitWidth: 26
        implicitHeight: 17
        radius: height / 2
        color: "#FFFFFF"
        border.width: slider.visualFocus ? 2 : 1
        border.color: slider.visualFocus ? Theme.focus : "#B8B8B8"

        Rectangle {
            anchors.centerIn: parent
            width: 9
            height: 9
            radius: width / 2
            color: "#FFFFFF"
            border.width: 1
            border.color: "#8A8A8A"
        }
    }
}
