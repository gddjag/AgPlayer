import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm

    property alias cloneText: cloneTextArea.text

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("2  克隆文本")
                color: Theme.primaryText
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Label {
                text: cloneTextArea.length + " / 2000"
                color: Theme.secondaryText
                font.pixelSize: 11
            }
            Button {
                text: qsTr("清空")
                enabled: cloneTextArea.length > 0
                onClicked: cloneTextArea.clear()
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: cloneTextArea
                objectName: "voiceCloneTextArea"
                placeholderText: qsTr("输入要生成的人声内容")
                wrapMode: TextEdit.Wrap
                onTextChanged: {
                    if (length > 2000) remove(2000, length)
                }
                color: Theme.primaryText
                Accessible.name: qsTr("克隆文本")
            }
        }
    }
}
