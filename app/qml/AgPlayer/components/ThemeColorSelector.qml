import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

ColumnLayout {
    id: root

    property string objectNamePrefix: "themeColor"
    property string title: ""
    property int selectedMode: 0
    property string selectedPreset: "aurora"
    property int customKind: 0
    property color customColor: "#D27722"
    property color customColorMiddle: "#D27722"
    property color customColorEnd: "#D27722"

    signal defaultRequested()
    signal presetRequested(string preset)
    signal customConfigurationRequested(
        int kind, string start, string middle, string end)

    function colorText(value) {
        return String(value || "").toUpperCase()
    }

    function presetName(id) {
        switch (id) {
        case "aurora": return qsTr("Aurora")
        case "seaGlass": return qsTr("Sea Glass")
        case "sunset": return qsTr("Sunset")
        case "lavenderMist": return qsTr("Lavender Mist")
        case "morningGlow": return qsTr("Morning Glow")
        default: return id
        }
    }

    function requestCustom(kind, start, middle, end) {
        root.customConfigurationRequested(
                    kind, root.colorText(start), root.colorText(middle),
                    root.colorText(end))
    }

    Layout.fillWidth: true
    spacing: Theme.spacingXs
    implicitHeight: choiceRow.implicitHeight
                    + (customEditor.expanded
                       ? spacing + customEditor.implicitHeight : 0)
    Accessible.role: Accessible.Grouping
    Accessible.name: root.title

    RowLayout {
        id: choiceRow
        Layout.fillWidth: true
        spacing: Math.max(2, Theme.spacingXs / 2)

        Button {
            id: defaultButton
            objectName: root.objectNamePrefix + "Default"
            checkable: true
            checked: root.selectedMode === 0
            text: qsTr("默认")
            focusPolicy: Qt.StrongFocus
            leftPadding: 6
            rightPadding: checked ? 22 : 6
            Layout.preferredHeight: 30
            Accessible.role: Accessible.Button
            Accessible.name: text + (checked ? qsTr("，已选择") : "")
            onClicked: root.defaultRequested()

            contentItem: Text {
                text: defaultButton.text
                color: defaultButton.checked ? Theme.accentText : Theme.primaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                implicitWidth: 64
                implicitHeight: 30
                radius: Theme.radiusSm
                color: defaultButton.checked ? Theme.accent : Theme.background
                border.width: defaultButton.activeFocus || defaultButton.checked ? 2 : 1
                border.color: defaultButton.activeFocus ? Theme.focus
                              : defaultButton.checked ? Theme.accent : Theme.border

                Rectangle {
                    id: defaultSelectionCue
                    objectName: root.objectNamePrefix + "DefaultSelectionCue"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 3
                    width: 16
                    height: 16
                    radius: width / 2
                    visible: defaultButton.checked
                    color: Theme.background
                    border.width: 1
                    border.color: Theme.primaryText

                    ThemedIcon {
                        anchors.centerIn: parent
                        source: Theme.icon("check-line")
                        tint: Theme.primaryText
                        sourceSize.width: 10
                        sourceSize.height: 10
                    }
                }
            }
        }

        Repeater {
            model: ThemeManager.recommendedPresets

            delegate: AbstractButton {
                id: swatch
                required property var modelData

                objectName: root.objectNamePrefix + "Preset-" + modelData.id
                checkable: true
                checked: root.selectedMode === 1
                         && root.selectedPreset === modelData.id
                property bool selectionCueVisible: checked
                property bool focusCueVisible: activeFocus
                implicitWidth: 46
                implicitHeight: 30
                Layout.preferredWidth: 46
                Layout.preferredHeight: 30
                padding: 0
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: root.presetName(modelData.id) + " "
                                 + root.colorText(modelData.start) + " "
                                 + root.colorText(modelData.middle) + " "
                                 + root.colorText(modelData.end)
                                 + (checked ? qsTr("，已选择") : "")
                onClicked: root.presetRequested(modelData.id)
                Keys.onSpacePressed: function(event) {
                    swatch.clicked()
                    event.accepted = true
                }
                Keys.onReturnPressed: function(event) {
                    swatch.clicked()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    swatch.clicked()
                    event.accepted = true
                }

                background: Rectangle {
                    radius: Theme.radiusSm
                    border.width: swatch.checked ? 2 : 1
                    border.color: swatch.checked ? Theme.accent : Theme.border
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: swatch.modelData.start }
                        GradientStop { position: 0.5; color: swatch.modelData.middle }
                        GradientStop { position: 1.0; color: swatch.modelData.end }
                    }

                    Rectangle {
                        id: presetSelectionCue
                        objectName: swatch.objectName + "SelectionCue"
                        anchors.centerIn: parent
                        visible: swatch.checked
                        width: 18
                        height: 18
                        radius: width / 2
                        color: Theme.background
                        border.width: 1
                        border.color: Theme.primaryText

                        ThemedIcon {
                            anchors.centerIn: parent
                            source: Theme.icon("check-line")
                            tint: Theme.primaryText
                            sourceSize.width: 12
                            sourceSize.height: 12
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: Theme.radiusSm + 2
                        color: "transparent"
                        border.width: swatch.focusCueVisible ? 2 : 0
                        border.color: Theme.focus
                        visible: swatch.focusCueVisible
                    }
                }
            }
        }

        Button {
            id: customButton
            objectName: root.objectNamePrefix + "Custom"
            checkable: true
            checked: root.selectedMode === 2
            property bool selectionCueVisible: checked
            property bool focusCueVisible: activeFocus
            text: qsTr("自定义")
            focusPolicy: Qt.StrongFocus
            Layout.preferredHeight: 30
            Accessible.role: Accessible.Button
            Accessible.name: text + (checked ? qsTr("，已选择") : "")
            onClicked: root.requestCustom(
                           root.customKind, root.customColor,
                           root.customColorMiddle, root.customColorEnd)

            contentItem: RowLayout {
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: customButton.text
                    color: customButton.checked ? Theme.accentText
                                                : Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                ThemedIcon {
                    visible: customButton.checked
                    source: Theme.icon("check-line")
                    tint: Theme.accentText
                    sourceSize.width: 12
                    sourceSize.height: 12
                }
            }
            background: Rectangle {
                implicitWidth: 72
                implicitHeight: 30
                radius: Theme.radiusSm
                color: customButton.checked ? Theme.accent : Theme.background
                border.width: customButton.activeFocus || customButton.checked ? 2 : 1
                border.color: customButton.activeFocus ? Theme.focus
                              : customButton.checked ? Theme.accent : Theme.border
            }
        }

        Item { Layout.fillWidth: true }
    }

    ColumnLayout {
        id: customEditor
        objectName: root.objectNamePrefix + "CustomEditor"
        property bool expanded: root.selectedMode === 2
        visible: expanded
        Layout.fillWidth: true
        spacing: Theme.spacingSm

        RowLayout {
            spacing: 0

            Button {
                id: solidButton
                objectName: root.objectNamePrefix + "CustomSolid"
                text: qsTr("Solid")
                checkable: true
                checked: root.customKind === 0
                focusPolicy: Qt.StrongFocus
                Layout.preferredWidth: 58
                Layout.preferredHeight: 32
                Accessible.role: Accessible.Button
                Accessible.name: text + (checked ? qsTr("，已选择") : "")
                onClicked: root.requestCustom(
                               0, root.customColor,
                               root.customColorMiddle, root.customColorEnd)
                Keys.onSpacePressed: function(event) {
                    solidButton.clicked()
                    event.accepted = true
                }
                Keys.onReturnPressed: function(event) {
                    solidButton.clicked()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    solidButton.clicked()
                    event.accepted = true
                }
            }

            Button {
                id: gradientButton
                objectName: root.objectNamePrefix + "CustomGradient"
                text: qsTr("Gradient")
                checkable: true
                checked: root.customKind === 1
                focusPolicy: Qt.StrongFocus
                Layout.preferredWidth: 72
                Layout.preferredHeight: 32
                Accessible.role: Accessible.Button
                Accessible.name: text + (checked ? qsTr("，已选择") : "")
                onClicked: root.requestCustom(
                               1, root.customColor,
                               root.customColorMiddle, root.customColorEnd)
                Keys.onSpacePressed: function(event) {
                    gradientButton.clicked()
                    event.accepted = true
                }
                Keys.onReturnPressed: function(event) {
                    gradientButton.clicked()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    gradientButton.clicked()
                    event.accepted = true
                }
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                spacing: 2
                Label {
                    text: qsTr("Preview")
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
                Rectangle {
                    id: customPreview
                    objectName: "skinCustomPreview"
                    Layout.preferredWidth: 90
                    Layout.preferredHeight: 32
                    radius: Theme.radiusSm
                    border.width: 1
                    border.color: Theme.border
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0.0
                            color: ThemeManager.backdropStart
                        }
                        GradientStop {
                            position: 0.5
                            color: ThemeManager.backdropMiddle
                        }
                        GradientStop {
                            position: 1.0
                            color: ThemeManager.backdropEnd
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ColumnLayout {
                spacing: 2
                Label {
                    text: qsTr("Start")
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
                ColorField {
                    id: customStart
                    objectName: "skinCustomStart"
                    colorValue: root.customColor
                    editingLabel: qsTr("Start")
                    onColorEdited: function(value) {
                        root.requestCustom(
                                    root.customKind, value,
                                    root.customColorMiddle, root.customColorEnd)
                    }
                }
            }

            ColumnLayout {
                visible: root.customKind === 1
                spacing: 2
                Label {
                    text: qsTr("Middle")
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
                ColorField {
                    id: customMiddle
                    objectName: "skinCustomMiddle"
                    colorValue: root.customColorMiddle
                    editingLabel: qsTr("Middle")
                    onColorEdited: function(value) {
                        root.requestCustom(1, root.customColor, value,
                                           root.customColorEnd)
                    }
                }
            }

            ColumnLayout {
                visible: root.customKind === 1
                spacing: 2
                Label {
                    text: qsTr("End")
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
                ColorField {
                    id: customEnd
                    objectName: "skinCustomEnd"
                    colorValue: root.customColorEnd
                    editingLabel: qsTr("End")
                    onColorEdited: function(value) {
                        root.requestCustom(1, root.customColor,
                                           root.customColorMiddle, value)
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
