import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: Theme.background
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 34

        Text {
            text: AudioEditorController.hasDocument ? qsTr("未命名音频") : qsTr("未打开音频")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 13
        }
        Repeater {
            model: [qsTr("格式 --"), qsTr("采样率 --"), qsTr("位深 --"),
                    qsTr("声道 --"), qsTr("时长 --"), qsTr("BPM --")]
            Text {
                text: modelData
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
            }
        }
        Item { Layout.fillWidth: true }
    }
}
