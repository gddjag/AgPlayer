import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    function beginInteraction(ratio) { overviewInteraction.beginDrag(ratio) }
    function updateInteraction(ratio) { overviewInteraction.dragTo(ratio) }
    color: Theme.panel
    border.color: Theme.border
    radius: Theme.radiusSm

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12

        Rectangle {
            id: overview
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.isLight ? "#EAF2F2" : "#132124"
            radius: 4
            border.color: Theme.border
            clip: true

            AudioEditorWaveformItem {
                anchors.fill: parent
                anchors.margins: 4
                channelPeaks: AudioEditorController.channelPeaks
                waveformColor: Theme.isLight ? "#2B9692" : "#297E7B"
            }
            Rectangle {
                id: viewportWindow
                x: parent.width * AudioEditorController.viewport.overviewStartRatio
                width: parent.width * AudioEditorController.viewport.overviewWidthRatio
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: "#121BCBC3"
                border.color: Theme.cyan
                border.width: 1
            }
            MouseArea {
                id: overviewInteraction
                objectName: "overviewInteraction"
                anchors.fill: parent
                enabled: AudioEditorController.hasDocument
                cursorShape: Qt.SizeHorCursor
                property real pressRatio: 0
                property real initialStartRatio: 0
                property bool grabbedWindow: false
                function beginDrag(ratio) {
                    pressRatio = ratio
                    initialStartRatio = AudioEditorController.viewport.overviewStartRatio
                    const windowEnd = initialStartRatio
                        + AudioEditorController.viewport.overviewWidthRatio
                    grabbedWindow = pressRatio >= initialStartRatio
                        && pressRatio <= windowEnd
                    if (!grabbedWindow) {
                        AudioEditorController.viewport.moveOverviewWindow(
                            pressRatio
                            - AudioEditorController.viewport.overviewWidthRatio / 2)
                        initialStartRatio = AudioEditorController.viewport.overviewStartRatio
                        grabbedWindow = true
                    }
                }
                function dragTo(ratio) {
                    if (!grabbedWindow) return
                    AudioEditorController.viewport.moveOverviewWindow(
                        initialStartRatio + ratio - pressRatio)
                }
                onPressed: mouse => beginDrag(mouse.x / Math.max(1, width))
                onPositionChanged: mouse => {
                    if (pressed)
                        dragTo(mouse.x / Math.max(1, width))
                }
            }
        }

        ToolButton {
            icon.source: Theme.icon("subtract-line")
            Layout.preferredWidth: 30
            Accessible.name: qsTr("缩小")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: AudioEditorController.viewport.zoomAt(0.8, overview.width / 2)
        }
        Slider {
            Layout.preferredWidth: 110
            from: 0; to: 1
            value: AudioEditorController.viewport.overviewStartRatio
            onMoved: AudioEditorController.viewport.moveOverviewWindow(value)
        }
        ToolButton {
            icon.source: Theme.icon("add-line")
            Layout.preferredWidth: 30
            Accessible.name: qsTr("放大")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: AudioEditorController.viewport.zoomAt(1.25, overview.width / 2)
        }
        ComboBox {
            Layout.preferredWidth: 92
            model: [qsTr("缩放级别"), "100%", "200%", "400%"]
            onActivated: {
                if (currentIndex > 0)
                    AudioEditorController.viewport.setVisibleRange(
                        0, AudioEditorController.totalFrames
                           / Math.pow(2, currentIndex - 1))
            }
        }
    }
}
