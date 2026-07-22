import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    color: Theme.panel

    signal importRequested()

    // State 1: empty CTA (default)
    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingLg
        visible: !ImportController.busy && ImportController.errors.length === 0

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
                color: !parent.enabled ? "transparent"
                      : parent.pressed ? Theme.cyan
                      : parent.hovered ? Theme.cyan
                      : "transparent"
                border.color: Theme.cyan
                border.width: parent.visualFocus ? 2 : 1
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

    // State 2: importing progress
    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingMd
        visible: ImportController.busy

        Text {
            text: qsTr("Importing...")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 16
            font.weight: Font.Medium
            Layout.alignment: Qt.AlignHCenter
        }

        ProgressBar {
            value: ImportController.progress
            Layout.preferredWidth: 280
            Layout.alignment: Qt.AlignHCenter
        }
    }

    // State 3: import errors
    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingSm
        visible: !ImportController.busy && ImportController.errors.length > 0
        Layout.maximumWidth: 480

        Text {
            text: qsTr("Some files could not be imported")
            color: Theme.favoriteRed
            font.family: Theme.fontPrimary
            font.pixelSize: 15
            font.weight: Font.Medium
            Layout.alignment: Qt.AlignHCenter
        }

        Repeater {
            model: {
                var list = ImportController.errors
                if (list.length > 5) {
                    var shown = list.slice(0, 5)
                    shown.push(qsTr("... and %1 more").arg(list.length - 5))
                    return shown
                }
                return list
            }

            Text {
                text: modelData
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        Button {
            text: qsTr("Import audio")
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingMd
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
                implicitWidth: errorImportLabel.implicitWidth + Theme.spacingXl * 2
                implicitHeight: 36

                Text {
                    id: errorImportLabel
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
