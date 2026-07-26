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
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 20
        width: Math.min(parent.width - 48, 640)
        spacing: Theme.spacingMd

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 180
            Layout.bottomMargin: 40

            Image {
                anchors.fill: parent
                source: "qrc:/qt/qml/AgPlayer/assets/brand/empty-start-waveform.png"
                fillMode: Image.PreserveAspectCrop
                smooth: true
                opacity: 1.0
            }

            Image {
                anchors.centerIn: parent
                width: 132
                height: 132
                source: "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
                fillMode: Image.PreserveAspectFit
                smooth: true
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("开始播放你的音乐")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 28
            font.weight: Font.Medium
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("打开或拖拽音频文件到此处开始播放")
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 14
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingSm
            spacing: Theme.spacingMd

            Button {
                id: openFileButton
                objectName: "openFileButton"
                text: qsTr("打开文件")
                icon.source: Theme.icon("folder-open-fill")
                icon.color: Theme.primaryText
                palette.buttonText: Theme.primaryText
                Accessible.name: text
                focusPolicy: Qt.StrongFocus
                onClicked: root.openFileRequested()

                background: Rectangle {
                    implicitWidth: 190
                    implicitHeight: 52
                    color: parent.pressed ? Theme.violet
                                           : parent.hovered ? Theme.violet : Theme.cyan
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
                text: qsTr("导入文件夹")
                icon.source: Theme.icon("folder-open-fill")
                icon.color: Theme.primaryText
                palette.buttonText: Theme.primaryText
                Accessible.name: text
                focusPolicy: Qt.StrongFocus
                onClicked: root.importFolderRequested()

                background: Rectangle {
                    implicitWidth: 190
                    implicitHeight: 52
                    color: parent.pressed ? Theme.panel
                                           : parent.hovered ? Theme.border : "transparent"
                    border.color: parent.visualFocus ? Theme.cyan : Theme.border
                    border.width: parent.visualFocus ? 2 : 1
                    radius: Theme.radiusSm
                }

            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingXs
            text: qsTr("支持 MP3、WAV、FLAC、AAC、OGG、M4A 等音频格式")
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: 12
        }
    }
}
