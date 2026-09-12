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
    readonly property bool compactLayout: width < 1000
    readonly property real referenceCommandWidth: 1255
    readonly property real commandSpacingWidth: 14 * commandRow.spacing
    readonly property real availableCommandWidth: Math.max(0,
        width - commandSpacingWidth)
    readonly property real referenceScale: Math.min(1.0,
        availableCommandWidth / referenceCommandWidth)

    Connections {
        target: AudioEditorController.actions
        function onDataChanged() { bar.actionRevision += 1 }
    }

    component CommandButton: Button {
        id: commandButton
        required property string commandName
        required property string label
        required property string iconName
        required property string shortcutText
        property bool commandEnabled: true
        property bool selected: false
        property bool mirrorIcon: false
        property real referenceWidth: 0
        objectName: "editorCommand_" + commandName
        focusPolicy: Qt.NoFocus
        Keys.onSpacePressed: function(event) { event.accepted = true }
        enabled: commandEnabled
        Layout.fillWidth: bar.compactLayout
        Layout.preferredWidth: bar.compactLayout ? 0
                                                  : referenceWidth * bar.referenceScale
        Layout.minimumWidth: 0
        Layout.fillHeight: true
        Accessible.name: label
        ToolTip.visible: hovered
        ToolTip.text: label + "  (" + shortcutText + ")"

        contentItem: ColumnLayout {
            spacing: bar.compactLayout ? 0 : 2
            ThemedIcon {
                id: commandIcon
                readonly property int iconExtent: bar.compactLayout ? Theme.iconSizeMd : 24
                source: Theme.icon(iconName)
                tint: !commandButton.enabled ? Theme.textDisabled
                                             : commandButton.selected ? Theme.accentText
                                                                      : Theme.textPrimary
                sourceSize: Qt.size(iconExtent, iconExtent)
                Layout.preferredWidth: iconExtent
                Layout.preferredHeight: iconExtent
                Layout.alignment: Qt.AlignHCenter
                transform: Scale {
                    origin.x: commandIcon.width / 2
                    xScale: commandButton.mirrorIcon ? -1 : 1
                }
            }
            Text {
                visible: !bar.compactLayout
                text: commandButton.label
                color: !commandButton.enabled ? Theme.textDisabled
                                              : commandButton.selected ? Theme.accentText
                                                                       : Theme.textPrimary
                font.family: Theme.fontPrimary
                font.pixelSize: Theme.fontSizeBody
                Layout.alignment: Qt.AlignHCenter
            }
        }
        background: Rectangle {
            color: commandButton.selected ? Theme.accentPressed
                : commandButton.hovered && commandButton.enabled ? Theme.surfaceHover : Theme.surface
            border.color: commandButton.selected ? Theme.accentHover : Theme.borderStrong
            border.width: 1
            radius: 6
        }
    }

    component TrackToggleButton: ThemedIconButton {
        required property string label
        required property string iconName
        iconSource: Theme.icon(iconName)
        iconSize: Theme.iconSizeMd
        checkable: true
        enabled: AudioEditorController.hasDocument
            && !AudioEditorController.busy
        Layout.preferredWidth: 40 * bar.referenceScale
        Layout.minimumWidth: 32
        Layout.fillHeight: true
        accessibleName: label
        ToolTip.visible: hovered
        ToolTip.text: label
    }

    RowLayout {
        id: commandRow
        anchors.fill: parent
        spacing: bar.compactLayout ? Theme.spacingXs : Theme.spacingSm

        CommandButton {
            commandName: "importAudio"
            label: qsTr("导入音频")
            iconName: "folder-open-line"
            shortcutText: "Ctrl+O"
            referenceWidth: 132
            onClicked: bar.importRequested()
        }
        CommandButton {
            commandName: "saveProject"
            label: qsTr("保存工程")
            iconName: "save-3-line"
            shortcutText: "Ctrl+S"
            referenceWidth: 140
            commandEnabled: AudioEditorController.hasDocument
                && !AudioEditorController.busy
            onClicked: bar.saveProjectRequested()
        }
        CommandButton {
            commandName: "select"
            label: qsTr("选择")
            iconName: "cursor-line"
            shortcutText: "Ctrl+1"
            referenceWidth: 80
            selected: AudioEditorController.activeTool === "select"
            onClicked: AudioEditorController.setActiveTool("select")
        }
        CommandButton {
            commandName: "split"
            label: qsTr("分割")
            iconName: "scissors-cut-line"
            shortcutText: "S / Ctrl+B"
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
            shortcutText: "Delete"
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
            shortcutText: "Shift+C"
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
            shortcutText: "Ctrl+C"
            referenceWidth: 81
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.copy")
            onClicked: AudioEditorController.triggerAction("editor.copy")
        }
        CommandButton {
            commandName: "paste"
            label: qsTr("粘贴")
            iconName: "clipboard-line"
            shortcutText: "Ctrl+V"
            referenceWidth: 91
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.paste")
            onClicked: AudioEditorController.triggerAction("editor.paste")
        }
        CommandButton {
            commandName: "fadeIn"
            label: qsTr("淡入")
            iconName: "bar-chart-line"
            shortcutText: "I"
            referenceWidth: 77
            commandEnabled: bar.actionRevision >= 0
                && AudioEditorController.actionEnabled("editor.fadeIn")
            onClicked: AudioEditorController.triggerAction("editor.fadeIn")
        }
        CommandButton {
            commandName: "fadeOut"
            label: qsTr("淡出")
            iconName: "bar-chart-line"
            shortcutText: "O"
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
            shortcutText: "M"
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
            shortcutText: "Ctrl+N"
            referenceWidth: 73
            commandEnabled: AudioEditorController.hasDocument
                && AudioEditorController.totalFrames > 0
                && !AudioEditorController.busy
            onClicked: AudioEditorController.reduceNoise()
        }
        CommandButton {
            commandName: "clear"
            label: qsTr("清除")
            iconName: "brush-line"
            shortcutText: "Ctrl+Backspace"
            referenceWidth: 82
            commandEnabled: AudioEditorController.hasDocument
                && AudioEditorController.totalFrames > 0
                && !AudioEditorController.busy
            onClicked: AudioEditorController.clearTimeline()
        }
    }
}
