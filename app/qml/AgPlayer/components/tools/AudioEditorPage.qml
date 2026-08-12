import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: page
    objectName: "audioEditorPage"
    color: Theme.background
    clip: true

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
                        Item { Layout.fillHeight: true }
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
