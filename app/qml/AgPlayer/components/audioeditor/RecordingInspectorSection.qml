import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: section
    color: Theme.elevated
    border.color: Theme.border
    radius: Theme.radiusSm
    implicitHeight: collapsed ? 42 : 342
    property bool collapsed: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        ToolButton {
            Layout.fillWidth: true
            text: (section.collapsed ? "▸  " : "▾  ") + qsTr("录音")
            font.bold: true
            onClicked: section.collapsed = !section.collapsed
        }
        GridLayout {
            visible: !section.collapsed
            columns: 2
            Layout.fillWidth: true
            columnSpacing: 8
            rowSpacing: 6

            Label { text: qsTr("输入设备") }
            ComboBox { Layout.fillWidth: true; model: [qsTr("未检测到设备")]; enabled: false }
            Label { text: qsTr("输入声道") }
            ComboBox { Layout.fillWidth: true; model: [qsTr("立体声")]; enabled: false }
            Label { text: qsTr("采样率") }
            ComboBox { Layout.fillWidth: true; model: ["44100 Hz", "48000 Hz"]; enabled: false }
            Label { text: qsTr("输入电平") }
            ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: 0 }
            Label { text: qsTr("录音模式") }
            RowLayout {
                RadioButton { text: qsTr("新建录音"); checked: true }
                RadioButton { text: qsTr("插入到光标") }
            }
            Label { text: qsTr("监听") }
            Switch { checked: false; enabled: false }
            Label { text: qsTr("输出格式") }
            ComboBox { Layout.fillWidth: true; model: ["WAV (PCM 24 bit)"]; enabled: false }
            Label { text: qsTr("保存位置") }
            TextField { Layout.fillWidth: true; readOnly: true; placeholderText: qsTr("选择录音目录") }
        }
        Item { Layout.fillHeight: true }
    }
}
