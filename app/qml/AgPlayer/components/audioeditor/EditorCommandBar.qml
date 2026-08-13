import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: bar
    signal exportSelectionRequested()
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

    component DirectButton: ToolButton {
        required property string commandName
        property string label
        property string iconName
        property string shortcutText: ""
        property bool commandEnabled: AudioEditorController.hasDocument
            && !AudioEditorController.busy
        signal invoked()
        objectName: "editorCommand_" + commandName
        enabled: commandEnabled
        implicitWidth: Math.max(60, directRow.implicitWidth + 16)
        implicitHeight: 44
        onClicked: invoked()
        Accessible.name: label
        ToolTip.visible: hovered
        ToolTip.text: label + (shortcutText.length > 0
            ? "  (" + shortcutText + ")" : "")
        contentItem: RowLayout {
            id: directRow
            spacing: 5
            ThemedIcon {
                source: Theme.icon(iconName)
                tint: parent.parent.enabled ? Theme.iconPrimary : Theme.secondaryText
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            Text {
                text: parent.parent.label
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
        CommandButton { actionId: "editor.cropToSelection"; label: qsTr("裁剪"); shortcutText: "Ctrl+T"; iconName: "crop-line" }
        CommandButton { actionId: "editor.silenceSelection"; label: qsTr("静音"); shortcutText: "Ctrl+L"; iconName: "volume-mute-line" }
        CommandButton { actionId: "editor.fadeIn"; label: qsTr("淡入"); shortcutText: "Ctrl+Alt+I"; iconName: "restore-line" }
        CommandButton { actionId: "editor.fadeOut"; label: qsTr("淡出"); shortcutText: "Ctrl+Alt+O"; iconName: "restore-line" }
        DirectButton {
            commandName: "normalize"; label: qsTr("标准化"); iconName: "equalizer-line"
            onInvoked: AudioEditorController.normalize()
        }
        DirectButton {
            commandName: "insertSilence"; label: qsTr("插入静音"); iconName: "volume-mute-line"
            onInvoked: AudioEditorController.insertSilence(
                AudioEditorController.positionMs * AudioEditorController.sampleRate / 1000,
                AudioEditorController.sampleRate)
        }
        DirectButton {
            commandName: "addMarker"; label: qsTr("标记"); shortcutText: "Ctrl+M"; iconName: "pushpin-fill"
            onInvoked: AudioEditorController.addMarker(
                qsTr("标记 %1").arg(AudioEditorController.markers.length + 1),
                AudioEditorController.positionMs * AudioEditorController.sampleRate / 1000)
        }
        DirectButton {
            commandName: "exportSelection"; label: qsTr("导出选区"); iconName: "download-line"
            commandEnabled: AudioEditorController.selectionStart >= 0
                && !AudioEditorController.busy
            onInvoked: bar.exportSelectionRequested()
        }
        DirectButton {
            commandName: "clearSelection"; label: qsTr("取消选区"); shortcutText: "Ctrl+Shift+A"; iconName: "close-fill"
            commandEnabled: AudioEditorController.selectionStart >= 0
            onInvoked: AudioEditorController.clearSelection()
        }
        DirectButton {
            commandName: "clearDocument"; label: qsTr("清空文件"); shortcutText: "Ctrl+W"; iconName: "delete-bin-line"
            onInvoked: AudioEditorController.clearDocument()
        }
        Item { Layout.fillWidth: true }
        CommandButton { actionId: "editor.export"; label: qsTr("导出"); shortcutText: "Ctrl+Shift+E"; iconName: "download-line" }
    }
}
