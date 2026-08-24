import QtQuick
import QtQuick.Controls
import AgPlayer

Rectangle {
    id: navigation
    objectName: "audioToolsTopNav"
    color: Theme.panel
    border.color: Theme.border
    border.width: 1
    implicitHeight: 43

    property int currentTool: 0
    property Window window
    readonly property color activeLabelColor: Theme.primaryText
    signal toolSelected(int index)

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 49
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: 31

        Repeater {
            model: [qsTr("音频编辑"), qsTr("格式转换"),
                    qsTr("元数据修改"), qsTr("文件名处理")]
            delegate: Button {
                required property int index
                required property string modelData
                objectName: "audioToolNav_" + index
                width: 126
                height: 42
                flat: true
                text: modelData
                checked: navigation.currentTool === index
                focusPolicy: Qt.StrongFocus
                contentItem: Text {
                    text: parent.text
                    color: parent.checked ? navigation.activeLabelColor
                                          : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 16
                    font.weight: parent.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Item {
                    Rectangle {
                        visible: parent.parent.checked
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 3
                        radius: 1
                        color: Theme.accent
                    }
                }
                onClicked: navigation.toolSelected(index)
            }
        }
    }
}
