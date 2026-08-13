import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel
    property bool playlistMode: false

    signal importRequested()

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingLg
        visible: !importStatus.active

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
            objectName: "emptyLibraryTitle"
            text: root.playlistMode ? qsTr("Import music")
                                    : qsTr("Your library is empty")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 18
            font.weight: Font.Medium
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            objectName: "emptyLibraryFormats"
            text: root.playlistMode
                  ? qsTr("Supports MP3, WAV, FLAC, AAC, M4A, OGG, OPUS and WMA")
                  : qsTr("Drag audio files into the window or click below to import")
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
            text: root.playlistMode ? qsTr("Import music") : qsTr("Import audio")
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingSm
            focusPolicy: Qt.StrongFocus
            onClicked: root.importRequested()

            background: Rectangle {
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.cyan
                      : parent.hovered ? Theme.cyan
                      : "transparent"
                border.color: Theme.cyan
                border.width: parent.visualFocus ? 2 : 1
                radius: Theme.radiusSm
                implicitHeight: 36
            }

            contentItem: Text {
                id: importLabel
                text: parent.text
                color: parent.hovered ? Theme.background : Theme.cyan
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    ImportStatusPanel {
        id: importStatus
        anchors.fill: parent
        onRetryRequested: root.importRequested()
    }
}
