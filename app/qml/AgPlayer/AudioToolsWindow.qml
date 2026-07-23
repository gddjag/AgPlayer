import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

// Frameless audio tools window. 240px left sidebar navigation + right content
// area with a StackLayout bound to AudioToolsController.currentTool.
// Implemented: tool 0 (Format Convert), tool 2 (Speed Adjust), tool 3 (Pitch
// Shift), tool 4 (Info Edit). Tool 1 (Light Edit) still shows a ComingSoonPage.
Window {
    id: window
    objectName: "audioToolsWindow"
    visible: false
    width: 1200
    height: 780
    minimumWidth: 960
    minimumHeight: 600
    flags: Qt.FramelessWindowHint
    color: Theme.background
    title: qsTr("AgPlayer Audio Tools")

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ToolSidebar {
            Layout.fillHeight: true
            window: window
            currentTool: AudioToolsController.currentTool
            onToolSelected: function(index) {
                AudioToolsController.selectTool(index)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            StackLayout {
                id: contentStack
                anchors.fill: parent
                currentIndex: AudioToolsController.currentTool

                FormatConvertPage {}
                ComingSoonPage { toolName: qsTr("Light Edit") }
                SpeedAdjustPage {}
                PitchShiftPage {}
                InfoEditPage {}
            }
        }
    }

    // Drag the window from any empty area. z: -1 keeps this MouseArea below
    // the RowLayout so sidebar buttons and form controls receive presses first.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: -1
        onPressed: function(mouse) {
            window.startSystemMove()
        }
    }
}
