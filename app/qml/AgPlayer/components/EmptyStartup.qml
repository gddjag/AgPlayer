import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "emptyStartup"
    color: "transparent"

    signal openFileRequested()
    signal importFolderRequested()

    ColumnLayout {
        id: startupActionArea
        objectName: "startupActionArea"
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.spacingXs
        width: Math.min(parent.width - 48, 640)
        spacing: Theme.spacingSm

        Text {
            objectName: "startupTitle"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("开始播放你的音乐")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 28
            font.weight: Font.Medium
            font.letterSpacing: 3
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            text: qsTr("打开或拖拽音频文件到此处开始播放")
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 22
            spacing: 20

            Button {
                id: openFileButton
                objectName: "openFileButton"
                Layout.preferredWidth: 190
                Layout.preferredHeight: 52
                text: qsTr("打开文件")
                icon.source: Theme.icon("folder-open-line")
                icon.color: Theme.onBrandGradientText
                icon.width: 20
                icon.height: 20
                palette.buttonText: Theme.onBrandGradientText
                font.pixelSize: 16
                Accessible.name: text
                focusPolicy: Qt.StrongFocus
                onClicked: root.openFileRequested()

                background: Rectangle {
                    color: parent.pressed ? Theme.violet
                                          : parent.hovered ? Qt.lighter(Theme.violet, 1.08)
                                                           : Theme.cyan
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.waveformBlue }
                        GradientStop { position: 1.0; color: Theme.violet }
                    }
                    border.color: parent.visualFocus ? Theme.primaryText : Theme.cyan
                    border.width: parent.visualFocus ? 2 : 1
                    radius: Theme.radiusSm
                }
            }

            Button {
                id: importFolderButton
                objectName: "importFolderButton"
                Layout.preferredWidth: 190
                Layout.preferredHeight: 52
                text: qsTr("导入文件夹")
                icon.source: Theme.icon("folder-open-line")
                icon.color: Theme.iconPrimary
                icon.width: 20
                icon.height: 20
                palette.buttonText: Theme.primaryText
                font.pixelSize: 16
                Accessible.name: text
                focusPolicy: Qt.StrongFocus
                onClicked: root.importFolderRequested()

                background: Rectangle {
                    color: parent.pressed || parent.hovered
                           ? Theme.hoverSurface
                           : Theme.isLight ? Theme.panel : "transparent"
                    border.color: parent.visualFocus ? Theme.cyan : Theme.border
                    border.width: parent.visualFocus ? 2 : 1
                    radius: Theme.radiusSm
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingLg
            text: qsTr("支持 MP3、WAV、FLAC、AAC、OGG、M4A 等音频格式")
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 13
        }
    }
}
