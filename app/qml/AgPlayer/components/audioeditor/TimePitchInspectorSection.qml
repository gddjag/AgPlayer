import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: section
    color: Theme.elevated
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: collapsed ? 42 : 326
    property bool collapsed: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 7

        ToolButton {
            Layout.fillWidth: true
            text: (section.collapsed ? "▸  " : "▾  ") + qsTr("速度与音高")
            font.bold: true
            onClicked: section.collapsed = !section.collapsed
        }
        GridLayout {
            visible: !section.collapsed
            columns: 2
            Layout.fillWidth: true
            rowSpacing: 5

            Button { text: qsTr("BPM 检测"); enabled: AudioEditorController.hasDocument }
            TextField { text: "--"; readOnly: true; horizontalAlignment: Text.AlignRight }
            Label { text: qsTr("原始 BPM") }
            TextField { text: "--"; readOnly: true; horizontalAlignment: Text.AlignRight }
            Label { text: qsTr("目标 BPM") }
            SpinBox { from: 1; to: 400; value: 100; enabled: AudioEditorController.hasDocument }
            Label { text: qsTr("速度") }
            SpinBox { from: 25; to: 400; value: 100; enabled: AudioEditorController.hasDocument }
            Label { text: qsTr("保持音调") }
            Switch { checked: true; enabled: AudioEditorController.hasDocument }
            Label { text: qsTr("升降半音") }
            SpinBox { from: -24; to: 24; value: 0; enabled: AudioEditorController.hasDocument }
            Label { text: qsTr("音分微调") }
            SpinBox { from: -100; to: 100; value: 0; enabled: AudioEditorController.hasDocument }
        }
        Button {
            visible: !section.collapsed
            Layout.fillWidth: true
            text: qsTr("应用处理")
            enabled: AudioEditorController.hasDocument
        }
        Item { Layout.fillHeight: true }
    }
}
