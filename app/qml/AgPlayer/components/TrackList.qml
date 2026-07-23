import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

ListView {
    id: root
    clip: true
    model: LibraryModel
    boundsBehavior: Flickable.StopAtBounds

    function formatTime(ms): string {
        if (ms <= 0)
            return "--:--"
        var totalSec = Math.floor(ms / 1000)
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
    }

    header: Rectangle {
        width: root.width
        height: 32
        color: Theme.panel

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingLg
            spacing: 0

            Text {
                text: qsTr("Title")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.fillWidth: true
                Layout.minimumWidth: 200
            }
            Text {
                text: qsTr("Artist")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 160
            }
            Text {
                text: qsTr("Format")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 80
            }
            Text {
                text: qsTr("Duration")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 70
                horizontalAlignment: Text.AlignRight
            }
            Item { Layout.preferredWidth: 40 }
        }
    }

    delegate: Rectangle {
        id: delegateRoot
        width: root.width
        height: 44
        color: PlaybackController.trackIndex === index ? Theme.panel : "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: PlaybackController.trackIndex === index ? Theme.cyan : "transparent"
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingLg
            spacing: 0

            Text {
                text: title || qsTr("Unknown title")
                color: available ? Theme.primaryText : Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 200
                ToolTip.text: text
                ToolTip.visible: titleRowHover.hovered
                ToolTip.delay: 500

                HoverHandler {
                    id: titleRowHover
                }
            }

            Text {
                text: artist || qsTr("Unknown artist")
                color: available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.preferredWidth: 160
                ToolTip.text: text
                ToolTip.visible: artistRowHover.hovered
                ToolTip.delay: 500

                HoverHandler {
                    id: artistRowHover
                }
            }

            Text {
                text: format ? format.toUpperCase() : "--"
                color: available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 80
            }

            Text {
                text: root.formatTime(durationMs)
                color: available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 70
                horizontalAlignment: Text.AlignRight
            }

            ToolButton {
                icon.source: favorite ? Theme.icon("heart-fill") : Theme.icon("heart-line")
                icon.color: favorite ? Theme.favoriteRed : Theme.secondaryText
                icon.width: 16
                icon.height: 16
                Accessible.name: favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                focusPolicy: Qt.TabFocus
                Layout.preferredWidth: 40
                onClicked: LibraryModel.setFavorite(index, !favorite)

                background: Rectangle {
                    color: !parent.enabled ? "transparent"
                          : parent.pressed ? Theme.cyan
                          : parent.visualFocus ? Theme.border
                          : parent.hovered ? Theme.border
                          : "transparent"
                    border.color: parent.visualFocus ? Theme.cyan : "transparent"
                    border.width: parent.visualFocus ? 2 : 0
                    radius: Theme.radiusSm
                }
            }
        }

        Text {
            visible: !available && importError && importError.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: Theme.spacingLg
            text: importError
            color: Theme.favoriteRed
            font.family: Theme.fontPrimary
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            propagateComposedEvents: true
            onDoubleClicked: function(mouse) {
                if (available)
                    LibraryModel.playRow(index)
                mouse.accepted = true
            }
        }
    }

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }
}
