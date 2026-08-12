import QtQuick
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: canvas
    color: Theme.isLight ? "#F7FAFA" : "#11191B"
    border.color: Theme.border
    radius: Theme.radiusSm
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: Theme.isLight ? "#EEF3F3" : "#151F21"
            border.color: Theme.border

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                Repeater {
                    model: 9
                    Item {
                        width: Math.max(48, (canvas.width - 24) / 9)
                        height: 38
                        Rectangle {
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            width: 1
                            height: 10
                            color: Theme.border
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: "--:--"
                            color: Theme.secondaryText
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            AudioEditorWaveformItem {
                anchors.fill: parent
                anchors.margins: 14
                channelPeaks: []
                waveformColor: Theme.isLight ? "#169B97" : "#39C7C0"
            }

            Text {
                visible: !AudioEditorController.hasDocument
                anchors.centerIn: parent
                text: qsTr("打开音频或新建录音以开始编辑")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 14
            }

            Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.top: parent.top; anchors.topMargin: parent.height * 0.23; text: "L"; color: Theme.secondaryText; font.pixelSize: 11 }
            Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.top: parent.top; anchors.topMargin: parent.height * 0.72; text: "R"; color: Theme.secondaryText; font.pixelSize: 11 }
        }
    }
}
