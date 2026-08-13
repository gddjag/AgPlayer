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
        property string helpText: label
        property string shortcutText: ""
        readonly property string hoverText: helpText + (shortcutText.length > 0
            ? "  (" + shortcutText + ")" : "")
        objectName: "editorCommand_" + actionId.substring("editor.".length)
        enabled: {
            const documentRevision = AudioEditorController.modified
            return AudioEditorController.actionEnabled(actionId)
        }
        implicitWidth: Math.max(60, contentRow.implicitWidth + 18)
        implicitHeight: 44
        onClicked: AudioEditorController.triggerAction(actionId)
        Accessible.name: label
        ToolTip.visible: hovered
        ToolTip.text: hoverText
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

        CommandButton { actionId: "editor.open"; label: qsTr("打开"); shortcutText: "Ctrl+O"; iconName: "folder-open-line" }
        CommandButton { actionId: "editor.newRecording"; label: qsTr("新建录音"); shortcutText: "Ctrl+R"; iconName: "checkbox-blank-circle-fill" }
        CommandButton { actionId: "editor.save"; label: qsTr("保存"); shortcutText: "Ctrl+S"; iconName: "download-line" }
        ToolSeparator {}
        CommandButton { actionId: "editor.undo"; label: qsTr("撤销"); shortcutText: "Ctrl+Z"; iconName: "arrow-go-back-line" }
        CommandButton { actionId: "editor.redo"; label: qsTr("重做"); shortcutText: "Ctrl+Y"; iconName: "arrow-go-forward-line" }
        ToolSeparator {}
        CommandButton { actionId: "editor.cut"; label: qsTr("剪切"); shortcutText: "Ctrl+X"; iconName: "scissors-cut-line" }
        CommandButton { actionId: "editor.copy"; label: qsTr("复制"); shortcutText: "Ctrl+C"; iconName: "file-copy-line" }
        CommandButton { actionId: "editor.paste"; label: qsTr("粘贴"); shortcutText: "Ctrl+V"; iconName: "file-copy-line" }
        CommandButton { actionId: "editor.deleteSelection"; label: qsTr("删除"); shortcutText: "Delete"; iconName: "delete-bin-line" }
        ToolSeparator {}
        CommandButton { actionId: "editor.cropToSelection"; label: qsTr("裁剪"); shortcutText: "Ctrl+T"; iconName: "crop-line" }
        CommandButton { actionId: "editor.silenceSelection"; label: qsTr("静音"); shortcutText: "Ctrl+L"; iconName: "volume-mute-line" }
        CommandButton { actionId: "editor.fadeIn"; label: qsTr("淡入"); shortcutText: "Ctrl+Alt+I"; iconName: "restore-line" }
        CommandButton { actionId: "editor.fadeOut"; label: qsTr("淡出"); shortcutText: "Ctrl+Alt+O"; iconName: "restore-line" }
        CommandButton { actionId: "editor.moreMenu"; label: qsTr("更多"); iconName: "equalizer-line" }
        Item { Layout.fillWidth: true }
        CommandButton { actionId: "editor.export"; label: qsTr("导出"); shortcutText: "Ctrl+Shift+E"; iconName: "download-line" }
    }
}
