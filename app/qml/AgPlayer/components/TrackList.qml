import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

ListView {
    id: root
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    property var trackModel: LibraryModel
    model: root.trackModel

    function formatTime(ms): string {
        if (ms <= 0)
            return "--:--"
        var totalSec = Math.floor(ms / 1000)
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
    }

    function formatBpm(bpm): string {
        if (bpm <= 0)
            return "--"
        return Math.round(bpm)
    }

    function isCurrentTrack(trackId): bool {
        return PlaybackController.currentTrackId && PlaybackController.currentTrackId === trackId
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
                text: "#"
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 40
            }
            Text {
                text: qsTr("歌曲")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.fillWidth: true
                Layout.minimumWidth: 160
            }
            Text {
                text: qsTr("收藏")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 48
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                text: qsTr("艺术家")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 120
            }
            Text {
                text: qsTr("专辑")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 140
            }
            Text {
                text: qsTr("评分")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 80
            }
            Text {
                text: qsTr("BPM")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 56
            }
            Text {
                text: qsTr("时长")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                font.weight: Font.Medium
                Layout.preferredWidth: 56
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    delegate: Rectangle {
        id: delegateRoot
        width: root.width
        height: 48
        color: root.isCurrentTrack(trackId) ? Theme.panel : "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: root.isCurrentTrack(trackId) ? Theme.cyan : "transparent"
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingLg
            spacing: 0

            Text {
                text: index + 1
                color: available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                Layout.preferredWidth: 40
            }

            RowLayout {
                spacing: Theme.spacingSm
                Layout.fillWidth: true
                Layout.minimumWidth: 160

                Image {
                    source: coverUrl && coverUrl !== "" ? coverUrl
                                                         : Theme.icon("music-2-fill")
                    sourceSize.width: 32
                    sourceSize.height: 32
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    text: title || qsTr("Unknown title")
                    color: available ? Theme.primaryText : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    ToolTip.text: text
                    ToolTip.visible: titleRowHover.hovered
                    ToolTip.delay: 500

                    HoverHandler {
                        id: titleRowHover
                    }
                }
            }

            ToolButton {
                icon.source: favorite ? Theme.icon("heart-fill") : Theme.icon("heart-line")
                icon.color: favorite ? Theme.favoriteRed : Theme.secondaryText
                icon.width: 16
                icon.height: 16
                Accessible.name: favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                focusPolicy: Qt.TabFocus
                Layout.preferredWidth: 48
                onClicked: {
                    if (root.trackModel && root.trackModel.setFavorite) {
                        root.trackModel.setFavorite(index, !favorite)
                    }
                }

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

            Text {
                text: artist || "---"
                color: available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.preferredWidth: 120
                ToolTip.text: text
                ToolTip.visible: artistRowHover.hovered && text !== "---"
                ToolTip.delay: 500

                HoverHandler {
                    id: artistRowHover
                }
            }

            Text {
                text: album || "---"
                color: available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.preferredWidth: 140
                ToolTip.text: text
                ToolTip.visible: albumRowHover.hovered && text !== "---"
                ToolTip.delay: 500

                HoverHandler {
                    id: albumRowHover
                }
            }

            RowLayout {
                spacing: 1
                Layout.preferredWidth: 80

                Repeater {
                    model: 5
                    delegate: Image {
                        source: index < rating ? Theme.icon("star-fill") : Theme.icon("star-line")
                        sourceSize.width: 12
                        sourceSize.height: 12
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        fillMode: Image.PreserveAspectFit
                    }
                }
            }

            Text {
                text: root.formatBpm(bpm)
                color: available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 56
            }

            Text {
                text: root.formatTime(durationMs)
                color: available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 56
                horizontalAlignment: Text.AlignRight
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
                if (available && root.trackModel && root.trackModel.playSourceRow) {
                    root.trackModel.playSourceRow(index)
                }
                mouse.accepted = true
            }
        }
    }

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }
}
