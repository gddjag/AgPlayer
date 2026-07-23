import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

// Placeholder page for unimplemented audio tools. Displays only the tool name
// and a "coming soon" label — no fake buttons or disabled controls.
Rectangle {
    id: root
    color: Theme.background

    property string toolName: ""

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingLg

        Image {
            source: Theme.icon("music-2-fill")
            sourceSize.width: 64
            sourceSize.height: 64
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            Layout.alignment: Qt.AlignHCenter
            opacity: 0.3
        }

        Text {
            text: root.toolName
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 24
            font.weight: Font.Medium
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: qsTr("Coming Soon")
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
