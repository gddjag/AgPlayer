import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm

    property bool canGenerate: false
    property bool running: false
    property string errorText: ""
    signal generateRequested()
    signal cancelRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        Label {
            text: qsTr("3  输出设置")
            color: Theme.primaryText
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }
        Label { text: qsTr("输出格式"); color: Theme.secondaryText }
        TextField {
            Layout.fillWidth: true
            text: "WAV"
            readOnly: true
            Accessible.name: qsTr("输出格式")
        }
        Label {
            Layout.fillWidth: true
            text: root.running ? qsTr("正在由当前 Worker 生成")
                               : qsTr("采样率与声道由真实生成结果决定")
            color: Theme.secondaryText
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            visible: root.errorText !== ""
            text: root.errorText
            color: Theme.favoriteRed
            wrapMode: Text.Wrap
        }
        Item { Layout.fillHeight: true }
        Button {
            objectName: "voiceCloneGenerateButton"
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            visible: !root.running
            enabled: root.canGenerate
            text: qsTr("生成人声")
            icon.source: Theme.icon("play-fill")
            onClicked: root.generateRequested()
        }
        Button {
            objectName: "voiceCloneCancelButton"
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            visible: root.running
            text: qsTr("取消生成")
            icon.source: Theme.icon("close-fill")
            onClicked: root.cancelRequested()
        }
    }
}
