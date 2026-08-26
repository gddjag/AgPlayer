import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

RowLayout {
    id: root

    property string objectNamePrefix: "themeColor"
    property string title: ""
    property int selectedMode: 0
    property string selectedPreset: "systemBlue"
    property color customColor: "#D27722"
    readonly property var recommendedColors: [
        "#007AFF", "#8B5CF6", "#EC4899", "#F97316", "#22C55E"
    ]
    signal defaultRequested()
    signal presetRequested(string preset)
    signal customRequested(color color)

    Layout.fillWidth: true
    spacing: Theme.spacingSm
    Accessible.role: Accessible.Grouping
    Accessible.name: root.title

    Repeater {
        model: root.recommendedColors

        delegate: AbstractButton {
            id: swatch
            required property int index
            required property string modelData
            readonly property bool selectionCueVisible:
                root.selectedMode === 3
                && root.customColor.toString().toUpperCase() === modelData
            readonly property bool focusCueVisible: activeFocus

            objectName: root.objectNamePrefix + "Recommended-" + index
            focusPolicy: Qt.StrongFocus
            implicitWidth: 28
            implicitHeight: 28
            padding: 0
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("推荐颜色 %1").arg(modelData)
                             + (selectionCueVisible ? qsTr("，已选择") : "")
            Accessible.selected: selectionCueVisible
            onClicked: root.customRequested(modelData)
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

            background: Item {
                Rectangle {
                    id: swatchCircle
                    anchors.centerIn: parent
                    width: swatch.selectionCueVisible ? 26 : 22
                    height: width
                    radius: width / 2
                    color: swatch.modelData
                    border.width: swatch.selectionCueVisible ? 2 : 1
                    border.color: swatch.selectionCueVisible
                                  ? Theme.primaryText : Theme.border

                    ThemedIcon {
                        anchors.centerIn: parent
                        visible: swatch.selectionCueVisible
                        source: Theme.icon("check-line")
                        tint: "#FFFFFF"
                        sourceSize.width: 12
                        sourceSize.height: 12
                    }
                }

                Rectangle {
                    anchors.fill: swatchCircle
                    anchors.margins: -2
                    radius: width / 2
                    color: "transparent"
                    border.width: swatch.focusCueVisible ? 2 : 0
                    border.color: Theme.focus
                    visible: swatch.focusCueVisible
                }
            }
        }
    }

    Item { Layout.fillWidth: true }

    ColorField {
        id: customField
        objectName: root.objectNamePrefix + "CustomField"
        property bool selectionCueVisible: root.selectedMode === 3
        Layout.preferredWidth: 112
        colorValue: root.customColor
        enabled: root.enabled
        Accessible.name: qsTr("自定义颜色 %1").arg(root.customColor)
        onColorEdited: function(value) {
            root.customRequested(value)
        }
    }
}
