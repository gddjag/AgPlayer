import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

ColumnLayout {
    id: root

    property string objectNamePrefix: "themeColor"
    property string title: ""
    property int selectedMode: 0
    property string selectedPreset: "systemBlue"
    property color customColor: "#D27722"
    signal defaultRequested()
    signal presetRequested(string preset)
    signal customRequested(color color)

    readonly property var presets: [
        { id: "systemBlue", color: "#007AFF" },
        { id: "indigo", color: "#5856D6" },
        { id: "purple", color: "#AF52DE" },
        { id: "pink", color: "#FF2D55" },
        { id: "red", color: "#FF3B30" },
        { id: "orange", color: "#FF9500" },
        { id: "gold", color: "#FFCC00" },
        { id: "green", color: "#34C759" },
        { id: "teal", color: "#30B0C7" },
        { id: "cyan", color: "#32ADE6" }
    ]

    Layout.fillWidth: true
    spacing: Theme.spacingXs
    Accessible.role: Accessible.Grouping
    Accessible.name: root.title

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingXs

        Button {
            id: defaultButton
            objectName: root.objectNamePrefix + "Default"
            checkable: true
            checked: root.selectedMode === 0
            text: qsTr("默认")
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
                implicitWidth: 58
                implicitHeight: 28
                radius: Theme.radiusSm
                color: defaultButton.checked ? Theme.accent : Theme.background
                border.width: defaultButton.checked ? 2 : 1
                border.color: defaultButton.activeFocus || defaultButton.checked
                              ? Theme.accent : Theme.border
            }
        }

        Repeater {
            model: root.presets

            delegate: AbstractButton {
                id: swatch
                required property var modelData
                objectName: root.objectNamePrefix + "Preset-" + modelData.id
                checkable: true
                property bool selectionCueVisible: swatch.checked
                checked: root.selectedMode === 1
                         && root.selectedPreset === modelData.id
                focusPolicy: Qt.StrongFocus
                implicitWidth: checked ? 26 : 22
                implicitHeight: checked ? 26 : 22
                padding: 0
                Accessible.role: Accessible.Button
                Accessible.name: root.title + " " + modelData.id
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
                    radius: width / 2
                    color: swatch.modelData.color
                    border.width: swatch.checked ? 2 : 1
                    border.color: swatch.checked
                                  ? (swatch.modelData.id === "gold"
                                     ? "#1B1B1B" : "#FFFFFF")
                                  : (swatch.activeFocus ? Theme.accent : Theme.border)

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: swatch.checked ? 3 : 0
                        radius: width / 2
                        color: "transparent"
                        border.width: swatch.checked ? 1 : 0
                        border.color: swatch.checked
                                      ? (swatch.modelData.id === "gold"
                                         ? "#FFFFFF" : "#1B1B1B")
                                      : "transparent"
                    }

                    ThemedIcon {
                        anchors.centerIn: parent
                        visible: swatch.checked
                        source: Theme.icon("check-line")
                        tint: swatch.modelData.id === "gold" ? "#1B1B1B" : "#FFFFFF"
                        sourceSize.width: 12
                        sourceSize.height: 12
                    }
                }
            }
        }

        Item {
            implicitWidth: customField.implicitWidth
            implicitHeight: customField.implicitHeight

            ColorField {
                id: customField
                anchors.fill: parent
                objectName: root.objectNamePrefix + "CustomField"
                property bool selectionCueVisible: root.selectedMode === 2
                colorValue: root.customColor
                enabled: root.enabled
                Accessible.name: qsTr("自定义颜色 %1").arg(root.customColor)
                onColorEdited: function(value) {
                    root.customRequested(value)
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: Theme.radiusSm + 2
                color: "transparent"
                border.width: root.selectedMode === 2 ? 2 : 0
                border.color: Theme.accent
                visible: root.selectedMode === 2
                z: 1
            }

            ThemedIcon {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: -4
                visible: root.selectedMode === 2
                source: Theme.icon("check-line")
                tint: Theme.primaryText
                sourceSize.width: 12
                sourceSize.height: 12
                z: 2
            }
        }

        Label {
            text: qsTr("自定义")
            color: root.selectedMode === 2 && root.enabled
                   ? Theme.primaryText : Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 12
        }
    }
}
