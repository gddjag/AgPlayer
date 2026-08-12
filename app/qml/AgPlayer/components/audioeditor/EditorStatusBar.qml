import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: Theme.panel
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 18
        Text { text: AudioEditorController.hasDocument ? qsTr("就绪") : qsTr("未打开音频"); color: Theme.secondaryText; font.pixelSize: 11 }
        Item { Layout.fillWidth: true }
        Text { text: qsTr("选区范围：--"); color: Theme.secondaryText; font.pixelSize: 11 }
        Item { Layout.fillWidth: true }
        Text { text: qsTr("采样率 --   位深 --   声道 --   时长 --"); color: Theme.secondaryText; font.pixelSize: 11 }
    }
}
