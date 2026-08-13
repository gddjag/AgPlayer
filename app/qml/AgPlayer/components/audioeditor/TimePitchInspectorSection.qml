import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: section
    color: Theme.elevated
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: collapsed ? 42 : 350
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

            Button {
                text: qsTr("BPM 检测")
                enabled: AudioEditorController.hasDocument
                onClicked: AudioEditorController.detectBpm()
            }
            Label {
                text: AudioEditorController.originalBpm > 0
                    ? AudioEditorController.originalBpm.toFixed(2) : "--"
                horizontalAlignment: Text.AlignRight
            }
            Label { text: qsTr("原始 BPM") }
            Label {
                text: AudioEditorController.originalBpm > 0
                    ? AudioEditorController.originalBpm.toFixed(2) : "--"
                horizontalAlignment: Text.AlignRight
            }
            Label { text: qsTr("目标 BPM") }
            SpinBox {
                from: 20; to: 400
                value: Math.round(AudioEditorController.targetBpm > 0
                    ? AudioEditorController.targetBpm : 100)
                enabled: AudioEditorController.hasDocument
                         && AudioEditorController.originalBpm > 0
                onValueModified: AudioEditorController.setTargetBpm(value)
            }
            Label { text: qsTr("速度") }
            SpinBox {
                from: 50; to: 200
                value: Math.round(AudioEditorController.speedPercent)
                enabled: AudioEditorController.hasDocument
                onValueModified: AudioEditorController.setSpeedPercent(value)
            }
            Label { text: qsTr("保持音调") }
            Switch {
                checked: AudioEditorController.keepPitch
                enabled: AudioEditorController.hasDocument
                onToggled: AudioEditorController.setKeepPitch(checked)
            }
            Label { text: qsTr("升降半音") }
            SpinBox {
                id: semitoneControl
                from: -12; to: 12; value: 0
                enabled: AudioEditorController.hasDocument
                onValueModified: AudioEditorController.setPitch(value, centsControl.value)
            }
            Label { text: qsTr("音分微调") }
            SpinBox {
                id: centsControl
                from: -99; to: 99; value: 0
                enabled: AudioEditorController.hasDocument
                onValueModified: AudioEditorController.setPitch(semitoneControl.value, value)
            }
        }
        Button {
            visible: !section.collapsed
            Layout.fillWidth: true
            text: qsTr("应用处理")
            enabled: AudioEditorController.hasDocument
            onClicked: AudioEditorController.applyTimePitch()
        }
        Item { Layout.fillHeight: true }
    }
}
