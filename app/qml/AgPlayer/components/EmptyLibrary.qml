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
        id: emptyContent
        objectName: "emptyLibraryContent"
        anchors.centerIn: parent
        width: Math.min(520, Math.max(0, root.width - 24))
        spacing: root.playlistMode && root.width <= 360
                 ? Theme.spacingSm : Theme.spacingLg
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
            font.pixelSize: root.playlistMode && root.width <= 360 ? 8 : 13
            font.letterSpacing: root.playlistMode && root.width <= 360
                                ? -1.5 : 0
            minimumPixelSize: 7
            fontSizeMode: root.playlistMode ? Text.HorizontalFit
                                              : Text.FixedSize
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: root.playlistMode ? Text.NoWrap : Text.WordWrap
            Layout.maximumWidth: root.playlistMode ? emptyContent.width : 360
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
                      : parent.pressed ? Theme.accentPressed
                      : parent.hovered ? Theme.accentHover
                      : "transparent"
                border.color: parent.visualFocus ? Theme.focus : Theme.accentBorder
                border.width: parent.visualFocus ? 2 : 1
                radius: Theme.radiusSm
                implicitHeight: 36
            }

            contentItem: Text {
                id: importLabel
                text: parent.text
                color: parent.hovered ? Theme.accentText : Theme.accent
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
