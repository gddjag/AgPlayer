import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "audioEditorPage"
    color: Theme.background
    clip: true

    FileDialog {
        id: openDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("音频文件 (*.wav *.flac *.mp3 *.aac *.m4a *.ogg *.opus *.wma)")]
        onAccepted: AudioEditorController.openFile(selectedFile)
    }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("WAV 音频 (*.wav)"), qsTr("FLAC 音频 (*.flac)"),
                      qsTr("MP3 音频 (*.mp3)"), qsTr("AAC 音频 (*.m4a *.aac)"),
                      qsTr("Ogg Vorbis (*.ogg)"), qsTr("Opus 音频 (*.opus)")]
        onAccepted: AudioEditorController.saveAs(selectedFile)
    }
    FileDialog {
        id: exportDialog
        property bool selectionOnly: false
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: saveDialog.nameFilters
        onAccepted: AudioEditorController.exportTo(selectedFile, selectionOnly)
    }
    Menu {
        id: moreMenu
        MenuItem { text: qsTr("增益…"); onTriggered: gainDialog.open() }
        MenuItem { text: qsTr("峰值归一化"); onTriggered: AudioEditorController.normalize() }
        MenuItem { text: qsTr("插入 1 秒静音"); onTriggered: AudioEditorController.insertSilence(
                AudioEditorController.positionMs * AudioEditorController.sampleRate / 1000,
                AudioEditorController.sampleRate) }
        MenuSeparator {}
        MenuItem {
            text: qsTr("导出选区…")
            enabled: AudioEditorController.selectionStart >= 0
            onTriggered: { exportDialog.selectionOnly = true; exportDialog.open() }
        }
    }
    Dialog {
        id: gainDialog
        title: qsTr("调整增益")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: AudioEditorController.applyGain(gainValue.value)
        contentItem: RowLayout {
            Label { text: qsTr("增益") }
            SpinBox { id: gainValue; from: -60; to: 24; value: 0 }
            Label { text: "dB" }
        }
    }
    Connections {
        target: AudioEditorController
        function onOpenRequested() { openDialog.open() }
        function onSaveAsRequested() { saveDialog.open() }
        function onExportRequested() {
            exportDialog.selectionOnly = false
            exportDialog.open()
        }
        function onMoreMenuRequested() { moreMenu.popup() }
    }

    focus: true
    Keys.onPressed: event => {
        const control = (event.modifiers & Qt.ControlModifier) !== 0
        if (control && event.key === Qt.Key_O) openDialog.open()
        else if (control && event.key === Qt.Key_S) AudioEditorController.save()
        else if (control && event.key === Qt.Key_Z) AudioEditorController.triggerAction("editor.undo")
        else if (control && event.key === Qt.Key_Y) AudioEditorController.triggerAction("editor.redo")
        else if (control && event.key === Qt.Key_X) AudioEditorController.triggerAction("editor.cut")
        else if (control && event.key === Qt.Key_C) AudioEditorController.triggerAction("editor.copy")
        else if (control && event.key === Qt.Key_V) AudioEditorController.triggerAction("editor.paste")
        else if (event.key === Qt.Key_Delete) AudioEditorController.triggerAction("editor.deleteSelection")
        else if (event.key === Qt.Key_Space) AudioEditorController.playPause()
        else if (event.key === Qt.Key_Escape) AudioEditorController.stopPlayback()
        else return
        event.accepted = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EditorCommandBar {
            objectName: "editorCommandBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 56
        }

        FileSummaryBar {
            objectName: "fileSummaryBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 38
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 260
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 4
                spacing: 8

                EditorWaveformCanvas {
                    objectName: "editorWaveformCanvas"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                OverviewNavigator {
                    objectName: "overviewNavigator"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 66
                }

                EditorTransportBar {
                    objectName: "editorTransportBar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                }
            }

            Rectangle {
                id: inspector
                objectName: "editorInspector"
                readonly property int businessSectionCount: 2
                Layout.preferredWidth: page.width < 1100 ? 248 : 284
                Layout.minimumWidth: 224
                Layout.maximumWidth: 304
                Layout.fillHeight: true
                Layout.rightMargin: 4
                color: Theme.panel
                border.color: Theme.border
                radius: Theme.radiusSm

                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        width: inspector.width
                        spacing: 10

                        RecordingInspectorSection {
                            objectName: "recordingInspector"
                            Layout.fillWidth: true
                        }
                        TimePitchInspectorSection {
                            objectName: "timePitchInspector"
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        EditorStatusBar {
            objectName: "editorStatusBar"
            Layout.fillWidth: true
            Layout.preferredHeight: 28
        }
    }
}
