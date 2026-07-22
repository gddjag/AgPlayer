import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel

    signal importRequested()

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingLg

        Image {
            source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
            sourceSize.width: 96
            sourceSize.height: 96
            Layout.preferredWidth: 96
            Layout.preferredHeight: 96
            Layout.alignment: Qt.AlignHCenter
            fillMode: Image.PreserveAspectFit
            opacity: 0.5
        }

        Text {
            text: qsTr("Your library is empty")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 18
            font.weight: Font.Medium
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: qsTr("Drag audio files into the window or click below to import")
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 13
            Layout.alignment: Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Layout.maximumWidth: 360
        }

        Button {
            objectName: "emptyImportButton"
            text: qsTr("Import audio")
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingSm
            focusPolicy: Qt.StrongFocus
            onClicked: root.importRequested()

            background: Rectangle {
                color: parent.hovered ? Theme.cyan : "transparent"
                border.color: Theme.cyan
                border.width: 1
                radius: Theme.radiusSm
                implicitWidth: importLabel.implicitWidth + Theme.spacingXl * 2
                implicitHeight: 36

                Text {
                    id: importLabel
                    anchors.centerIn: parent
                    text: parent.parent.text
                    color: parent.parent.hovered ? Theme.background : Theme.cyan
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                }
            }

            contentItem: Item {}
        }
    }
}
