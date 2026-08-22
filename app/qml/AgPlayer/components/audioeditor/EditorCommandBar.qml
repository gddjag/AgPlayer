import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: bar
    color: "#071a2d"
    border.color: "#23415d"
    border.width: 1
    radius: 7

    signal importRequested()
    signal saveProjectRequested()
    property int actionRevision: 0

    Connections {
        target: AudioEditorController.actions
        function onDataChanged() { bar.actionRevision += 1 }
    }

    component CommandButton: Button {
        required property string commandName
        required property string label
        required property string iconName
        property bool commandEnabled: true
        property bool selected: false
        objectName: "editorCommand_" + commandName
        enabled: commandEnabled
        Layout.fillWidth: true
        Layout.fillHeight: true
        Accessible.name: label
        ToolTip.visible: hovered
        ToolTip.text: label

        contentItem: ColumnLayout {
            spacing: 2
            ThemedIcon {
                source: Theme.icon(iconName)
                tint: parent.parent.enabled ? "#f4f8ff" : "#718096"
                sourceSize.width: 22
                sourceSize.height: 22
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: parent.parent.label
                color: parent.parent.enabled ? "#f4f8ff" : "#718096"
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                Layout.alignment: Qt.AlignHCenter
            }
        }
        background: Rectangle {
            color: parent.selected ? "#0867ed"
                : parent.hovered && parent.enabled ? "#123452" : "#0a2138"
            border.color: parent.selected ? "#2587ff" : "#294662"
            border.width: 1
            radius: 6
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 7

        CommandButton {
            commandName: "importAudio"
            label: qsTr("导入音频")
            iconName: "folder-open-line"
            onClicked: bar.importRequested()
        }
        CommandButton {
            commandName: "saveProject"
            label: qsTr("保存工程")
            iconName: "download-line"
            commandEnabled: AudioEditorController.hasDocument
                && !AudioEditorController.busy
            onClicked: bar.saveProjectRequested()
        }
        CommandButton {
            commandName: "select"
            label: qsTr("选择")
            iconName: "arrow-right-s-line"
            selected: AudioEditorController.activeTool === "select"
            onClicked: AudioEditorController.setActiveTool("select")
        }
        CommandButton {
            commandName: "split"
            label: qsTr("分割")
            iconName: "scissors-cut-line"
            selected: AudioEditorController.activeTool === "scissors"
            commandEnabled: AudioEditorController.hasDocument
                && !AudioEditorController.busy
            onClicked: {
                AudioEditorController.setActiveTool("scissors")
            }
        }
        CommandButton {
            commandName: "delete"
            label: qsTr("删除")
            iconName: "delete-bin-line"
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.deleteSelection")
            onClicked: AudioEditorController.triggerAction(
                "editor.deleteSelection")
        }
        CommandButton {
            commandName: "crop"
            label: qsTr("裁剪")
            iconName: "crop-line"
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.cropToSelection")
            onClicked: AudioEditorController.triggerAction(
                "editor.cropToSelection")
        }
        CommandButton {
            commandName: "copy"
            label: qsTr("复制")
            iconName: "file-copy-line"
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.copy")
            onClicked: AudioEditorController.triggerAction("editor.copy")
        }
        CommandButton {
            commandName: "paste"
            label: qsTr("粘贴")
            iconName: "file-copy-line"
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.paste")
            onClicked: AudioEditorController.triggerAction("editor.paste")
        }
        CommandButton {
            commandName: "fadeIn"
            label: qsTr("淡入")
            iconName: "equalizer-line"
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.fadeIn")
            onClicked: AudioEditorController.triggerAction("editor.fadeIn")
        }
        CommandButton {
            commandName: "fadeOut"
            label: qsTr("淡出")
            iconName: "equalizer-line"
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.fadeOut")
            onClicked: AudioEditorController.triggerAction("editor.fadeOut")
        }
        CommandButton {
            commandName: "mute"
            label: qsTr("静音片段")
            iconName: "volume-mute-line"
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.silenceSelection")
            onClicked: AudioEditorController.triggerAction(
                "editor.silenceSelection")
        }
        CommandButton {
            commandName: "clear"
            label: qsTr("清除")
            iconName: "close-line"
            commandEnabled: AudioEditorController.selectionStart >= 0
                || AudioEditorController.activeTool !== "select"
            onClicked: AudioEditorController.clearTransientState()
        }
    }
}
