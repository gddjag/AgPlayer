import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: bar
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm

    component CommandButton: ToolButton {
        required property string actionId
        property string label
        property string iconName
        enabled: {
            const documentRevision = AudioEditorController.modified
            return AudioEditorController.actionEnabled(actionId)
        }
        implicitWidth: Math.max(60, contentRow.implicitWidth + 18)
        implicitHeight: 44
        onClicked: AudioEditorController.triggerAction(actionId)
        contentItem: RowLayout {
            id: contentRow
            spacing: 6
            ThemedIcon {
                source: Theme.icon(iconName)
                tint: parent.parent.enabled ? Theme.iconPrimary : Theme.secondaryText
                sourceSize.width: 18
                sourceSize.height: 18
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            Text {
                text: label
                color: parent.parent.enabled ? Theme.primaryText : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 13
            }
        }
        background: Rectangle {
            color: parent.hovered && parent.enabled ? Theme.hoverSurface : "transparent"
            radius: Theme.radiusSm
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 2

        CommandButton { actionId: "editor.open"; label: qsTr("打开"); iconName: "folder-open-line" }
        CommandButton { actionId: "editor.newRecording"; label: qsTr("新建录音"); iconName: "checkbox-blank-circle-fill" }
        CommandButton { actionId: "editor.save"; label: qsTr("保存"); iconName: "download-line" }
        ToolSeparator {}
        CommandButton { actionId: "editor.undo"; label: qsTr("撤销"); iconName: "arrow-go-back-line" }
        CommandButton { actionId: "editor.redo"; label: qsTr("重做"); iconName: "arrow-go-forward-line" }
        ToolSeparator {}
        CommandButton { actionId: "editor.cut"; label: qsTr("剪切"); iconName: "scissors-cut-line" }
        CommandButton { actionId: "editor.copy"; label: qsTr("复制"); iconName: "file-copy-line" }
        CommandButton { actionId: "editor.paste"; label: qsTr("粘贴"); iconName: "file-copy-line" }
        CommandButton { actionId: "editor.deleteSelection"; label: qsTr("删除"); iconName: "delete-bin-line" }
        ToolSeparator {}
        CommandButton { actionId: "editor.cropToSelection"; label: qsTr("裁剪"); iconName: "crop-line" }
        CommandButton { actionId: "editor.silenceSelection"; label: qsTr("静音"); iconName: "volume-mute-line" }
        CommandButton { actionId: "editor.fadeIn"; label: qsTr("淡入"); iconName: "restore-line" }
        CommandButton { actionId: "editor.fadeOut"; label: qsTr("淡出"); iconName: "restore-line" }
        CommandButton { actionId: "editor.moreMenu"; label: qsTr("更多"); iconName: "equalizer-line" }
        Item { Layout.fillWidth: true }
        CommandButton { actionId: "editor.export"; label: qsTr("导出"); iconName: "download-line" }
    }
}
