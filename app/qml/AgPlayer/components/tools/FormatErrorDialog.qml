import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "formatErrorDialog"
    property var converter
    property string summary: ""
    property string detail: ""
    modal: true
    anchors.centerIn: parent
    width: 560
    title: qsTr("转换失败详情")
    standardButtons: Dialog.Close
    contentItem: ColumnLayout {
        Text { text: root.summary; color: Theme.waveformRed; font.weight: Font.DemiBold }
        TextArea { Layout.fillWidth: true; Layout.preferredHeight: 180; readOnly: true; text: root.detail; wrapMode: TextEdit.Wrap }
        Button {
            text: qsTr("复制详细信息")
            enabled: root.detail.length > 0
            onClicked: root.converter.copyText(root.detail)
        }
    }
}
