import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        spacing: 18

        Repeater {
            model: [qsTr("录音"), qsTr("停止"), qsTr("上一标记"), qsTr("下一标记")]
            ColumnLayout {
                spacing: 3
                ToolButton {
                    icon.source: Theme.icon(index === 0
                                            ? "checkbox-blank-circle-fill"
                                            : index === 1
                                              ? "checkbox-blank-line"
                                              : index === 2
                                                ? "skip-back-fill"
                                                : "skip-forward-fill")
                    icon.color: index === 0 ? Theme.waveformRed : Theme.iconPrimary
                    enabled: false
                }
                Label { text: modelData; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
            }
        }

        ToolButton {
            icon.source: Theme.icon("play-fill")
            icon.color: Theme.waveformGreen
            icon.width: 36
            icon.height: 36
            enabled: AudioEditorController.hasDocument
            Layout.preferredWidth: 76
            Layout.preferredHeight: 76
        }
        ColumnLayout {
            ToolButton {
                icon.source: Theme.icon("repeat-fill")
                icon.color: Theme.iconPrimary
                enabled: AudioEditorController.hasDocument
            }
            Label { text: qsTr("循环"); font.pixelSize: 11 }
        }
        ToolSeparator {}

        Repeater {
            model: [qsTr("当前位置"), qsTr("选区长度"), qsTr("总时长")]
            ColumnLayout {
                Layout.preferredWidth: 112
                Label { text: "--:--.---"; color: index === 0 ? Theme.waveformGreen : Theme.primaryText; font.pixelSize: 16 }
                Label { text: modelData; color: Theme.secondaryText; font.pixelSize: 11 }
            }
        }

        Item { Layout.fillWidth: true }
        ThemedIcon {
            source: Theme.icon("volume-up-fill")
            tint: Theme.iconPrimary
            sourceSize.width: 18
            sourceSize.height: 18
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
        }
        Slider { Layout.preferredWidth: 90; value: 0.7 }
        Label { text: "100%"; font.pixelSize: 11 }
        Slider { Layout.preferredWidth: 90; value: 0.5 }
        Label { text: "×1.00"; font.pixelSize: 11 }
    }
}
