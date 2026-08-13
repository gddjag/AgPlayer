import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: section
    color: Theme.elevated
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: collapsed ? 38 : 292
    property bool collapsed: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        ToolButton {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            text: (section.collapsed ? "▸  " : "▾  ") + qsTr("速度与音高")
            font.bold: true
            onClicked: section.collapsed = !section.collapsed
        }
        GridLayout {
            visible: !section.collapsed
            columns: 2
            Layout.fillWidth: true
            rowSpacing: 2

            component CompactSpinBox: SpinBox {
                implicitHeight: 28
            }

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
            CompactSpinBox {
                id: originalBpmControl
                from: 0; to: 400
                value: Math.round(AudioEditorController.originalBpm)
                enabled: AudioEditorController.hasDocument
                textFromValue: function(value) {
                    return value > 0 ? value.toString() : "--"
                }
                valueFromText: function(text) {
                    const parsed = Number.fromLocaleString(locale, text)
                    return isNaN(parsed) ? 0 : parsed
                }
                onValueModified: AudioEditorController.setOriginalBpm(value)
            }
            Label { text: qsTr("目标 BPM") }
            CompactSpinBox {
                from: 20; to: 400
                value: Math.round(AudioEditorController.targetBpm > 0
                    ? AudioEditorController.targetBpm : 100)
                enabled: AudioEditorController.hasDocument
                         && AudioEditorController.originalBpm > 0
                onValueModified: AudioEditorController.setTargetBpm(value)
            }
            Label { text: qsTr("速度") }
            CompactSpinBox {
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
            CompactSpinBox {
                id: semitoneControl
                from: -12; to: 12; value: 0
                enabled: AudioEditorController.hasDocument
                onValueModified: AudioEditorController.setPitch(value, centsControl.value)
            }
            Label { text: qsTr("音分微调") }
            CompactSpinBox {
                id: centsControl
                from: -99; to: 99; value: 0
                enabled: AudioEditorController.hasDocument
                onValueModified: AudioEditorController.setPitch(semitoneControl.value, value)
            }
        }
        Button {
            visible: !section.collapsed
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            text: AudioEditorController.timePitchPreviewActive
                ? qsTr("预览中 · 应用处理") : qsTr("应用处理")
            enabled: AudioEditorController.hasDocument
            onClicked: AudioEditorController.applyTimePitch()
        }
        Item { Layout.fillHeight: true }
    }
}
