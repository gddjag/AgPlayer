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

    // The startup panel shares a compact window with the bottom transport.
    // Scale its vertical rhythm from the height actually allocated by the
    // layout, so no text or button can intrude into that transport area.
    readonly property real contentScale: Math.min(1.0, Math.max(0.60,
        (height - 8) / 185))

    ColumnLayout {
        id: startupActionArea
        objectName: "startupActionArea"
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Math.max(2, Theme.spacingXs * root.contentScale)
        width: Math.min(parent.width - 48, 640)
        spacing: Math.max(3, Theme.spacingSm * root.contentScale)

        Text {
            objectName: "startupTitle"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("开始播放你的音乐")
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Math.round(28 * root.contentScale)
            font.weight: Font.Medium
            font.letterSpacing: 3
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Math.round(6 * root.contentScale)
            text: qsTr("打开或拖拽音频文件到此处开始播放")
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Math.round(14 * root.contentScale)
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Math.round(22 * root.contentScale)
            spacing: Math.max(8, Math.round(20 * root.contentScale))

            Button {
                id: openFileButton
                objectName: "openFileButton"
                Layout.preferredWidth: Math.round(190 * root.contentScale)
                Layout.preferredHeight: Math.round(52 * root.contentScale)
                text: qsTr("打开文件")
                icon.source: Theme.icon("folder-open-line")
                icon.color: Theme.onBrandGradientText
                icon.width: Math.max(14, Math.round(20 * root.contentScale))
                icon.height: Math.max(14, Math.round(20 * root.contentScale))
                palette.buttonText: Theme.onBrandGradientText
                font.pixelSize: Math.max(12, Math.round(16 * root.contentScale))
                Accessible.name: text
                focusPolicy: Qt.StrongFocus
                onClicked: root.openFileRequested()

                background: Rectangle {
                    color: parent.pressed ? Theme.accentPressed
                                          : parent.hovered ? Theme.accentHover
                                                           : Theme.accent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.waveformBlue }
                        GradientStop { position: 1.0; color: Theme.violet }
                    }
                    border.color: parent.visualFocus ? Theme.focus : Theme.accentBorder
                    border.width: parent.visualFocus ? 2 : 1
                    radius: Theme.radiusSm
                }
            }

            Button {
                id: importFolderButton
                objectName: "importFolderButton"
                Layout.preferredWidth: Math.round(190 * root.contentScale)
                Layout.preferredHeight: Math.round(52 * root.contentScale)
                text: qsTr("导入文件夹")
                icon.source: Theme.icon("folder-open-line")
                icon.color: Theme.iconPrimary
                icon.width: Math.max(14, Math.round(20 * root.contentScale))
                icon.height: Math.max(14, Math.round(20 * root.contentScale))
                palette.buttonText: Theme.primaryText
                font.pixelSize: Math.max(12, Math.round(16 * root.contentScale))
                Accessible.name: text
                focusPolicy: Qt.StrongFocus
                onClicked: root.importFolderRequested()

                background: Rectangle {
                    color: parent.pressed || parent.hovered
                           ? Theme.hoverSurface
                           : Theme.isLight ? Theme.panel : "transparent"
                    border.color: parent.visualFocus ? Theme.focus : Theme.border
                    border.width: parent.visualFocus ? 2 : 1
                    radius: Theme.radiusSm
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Math.max(4, Math.round(Theme.spacingLg * root.contentScale))
            text: qsTr("支持 MP3、WAV、FLAC、AAC、OGG、M4A 等音频格式")
            color: Theme.secondaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Math.max(10, Math.round(13 * root.contentScale))
        }
    }
}
