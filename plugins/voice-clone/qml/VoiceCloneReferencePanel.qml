import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm

    property alias referencePath: pathField.text

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Label {
            text: qsTr("1  参考人声")
            color: Theme.primaryText
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusSm

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 28
                spacing: 8
                ThemedIcon {
                    Layout.alignment: Qt.AlignHCenter
                    source: Theme.icon("music-2-line")
                    tint: Theme.iconAccent
                    sourceSize.width: 30
                    sourceSize.height: 30
                }
                Label {
                    Layout.fillWidth: true
                    text: pathField.text === "" ? qsTr("选择参考音频文件") : qsTr("已选择真实文件路径")
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.primaryText
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("建议 3–30 秒；格式约束由当前模型提供")
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.secondaryText
                    font.pixelSize: 11
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: pathField
                objectName: "voiceCloneReferencePathField"
                Layout.fillWidth: true
                placeholderText: qsTr("输入或粘贴本地音频路径")
                Accessible.name: qsTr("参考音频路径")
            }
            ToolButton {
                objectName: "voiceCloneReferenceBrowseButton"
                icon.source: Theme.icon("folder-open-line")
                icon.color: Theme.iconPrimary
                Accessible.name: qsTr("浏览参考音频")
                onClicked: referenceDialog.open()
            }
        }
    }

    FileDialog {
        id: referenceDialog
        title: qsTr("选择参考音频")
        nameFilters: [qsTr("音频文件 (*.wav *.mp3 *.flac *.m4a)"), qsTr("所有文件 (*)")]
        onAccepted: pathField.text = decodeURIComponent(
                        selectedFile.toString().replace(/^file:\/\/\//, ""))
    }
}
