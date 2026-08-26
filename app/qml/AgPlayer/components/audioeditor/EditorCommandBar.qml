import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: bar
    color: "transparent"

    signal importRequested()
    signal saveProjectRequested()
    property int actionRevision: 0
    readonly property real referenceCommandWidth: 1175
    readonly property real commandSpacingWidth: 12 * commandRow.spacing
    readonly property real availableCommandWidth: Math.max(0,
        width - commandSpacingWidth - commandRow.spacing)
    readonly property real referenceScale: Math.min(1.0,
        availableCommandWidth / referenceCommandWidth)

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
        property bool mirrorIcon: false
        property real referenceWidth: 0
        objectName: "editorCommand_" + commandName
        enabled: commandEnabled
        Layout.fillWidth: false
        Layout.preferredWidth: referenceWidth * bar.referenceScale
        Layout.minimumWidth: 0
        Layout.fillHeight: true
        Accessible.name: label
        ToolTip.visible: hovered
        ToolTip.text: label

        contentItem: ColumnLayout {
            spacing: 2
            ThemedIcon {
                source: Theme.icon(iconName)
                tint: parent.parent.enabled ? "#f4f8ff" : "#718096"
                sourceSize.width: 24
                sourceSize.height: 24
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                Layout.alignment: Qt.AlignHCenter
                transform: Scale {
                    origin.x: 12
                    xScale: parent.parent.mirrorIcon ? -1 : 1
                }
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
        id: commandRow
        anchors.fill: parent
        spacing: 8

        CommandButton {
            commandName: "importAudio"
            label: qsTr("导入音频")
            iconName: "folder-open-line"
            referenceWidth: 132
            onClicked: bar.importRequested()
        }
        CommandButton {
            commandName: "saveProject"
            label: qsTr("保存工程")
            iconName: "save-3-line"
            referenceWidth: 140
            commandEnabled: AudioEditorController.hasDocument
                && !AudioEditorController.busy
            onClicked: bar.saveProjectRequested()
        }
        CommandButton {
            commandName: "select"
            label: qsTr("选择")
            iconName: "cursor-line"
            referenceWidth: 80
            selected: AudioEditorController.activeTool === "select"
            onClicked: AudioEditorController.setActiveTool("select")
        }
        CommandButton {
            commandName: "split"
            label: qsTr("分割")
            iconName: "scissors-cut-line"
            referenceWidth: 80
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
            referenceWidth: 86
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.deleteSelection")
            onClicked: AudioEditorController.triggerAction(
                "editor.deleteSelection")
        }
        CommandButton {
            commandName: "crop"
            label: qsTr("裁剪")
            iconName: "crop-line"
            referenceWidth: 83
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.cropToSelection")
            onClicked: AudioEditorController.triggerAction(
                "editor.cropToSelection")
        }
        CommandButton {
            commandName: "copy"
            label: qsTr("复制")
            iconName: "file-copy-line"
            referenceWidth: 81
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.copy")
            onClicked: AudioEditorController.triggerAction("editor.copy")
        }
        CommandButton {
            commandName: "paste"
            label: qsTr("粘贴")
            iconName: "clipboard-line"
            referenceWidth: 91
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.paste")
            onClicked: AudioEditorController.triggerAction("editor.paste")
        }
        CommandButton {
            commandName: "fadeIn"
            label: qsTr("淡入")
            iconName: "bar-chart-line"
            referenceWidth: 77
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.fadeIn")
            onClicked: AudioEditorController.triggerAction("editor.fadeIn")
        }
        CommandButton {
            commandName: "fadeOut"
            label: qsTr("淡出")
            iconName: "bar-chart-line"
            referenceWidth: 75
            mirrorIcon: true
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.fadeOut")
            onClicked: AudioEditorController.triggerAction("editor.fadeOut")
        }
        CommandButton {
            commandName: "mute"
            label: qsTr("静音片段")
            iconName: "volume-mute-line"
            referenceWidth: 95
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.silenceSelection")
            onClicked: AudioEditorController.triggerAction(
                "editor.silenceSelection")
        }
        CommandButton {
            commandName: "noiseReduction"
            label: qsTr("降噪")
            iconName: "sound-module-line"
            referenceWidth: 73
            commandEnabled: AudioEditorController.hasDocument
                && !AudioEditorController.busy
            onClicked: AudioEditorController.reduceNoise()
        }
        CommandButton {
            commandName: "clear"
            label: qsTr("清除")
            iconName: "brush-line"
            referenceWidth: 82
            commandEnabled: AudioEditorController.selectionStart >= 0
                || AudioEditorController.activeTool !== "select"
            onClicked: AudioEditorController.clearTransientState()
        }
    }
}
