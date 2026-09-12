import QtQuick
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
            font.pixelSize: Theme.fontSizeSection
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
            font.pixelSize: Theme.fontSizeCaption
            fontSizeMode: Text.FixedSize
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(root.playlistMode ? 520 : 360,
                                            Math.max(0, root.width - 24))
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        ThemedButton {
            objectName: "emptyImportButton"
            text: root.playlistMode ? qsTr("Import music") : qsTr("Import audio")
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingSm
            prominent: true
            onClicked: root.importRequested()
        }
    }

    ImportStatusPanel {
        id: importStatus
        anchors.fill: parent
        onRetryRequested: root.importRequested()
    }
}
