import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

ListView {
    id: root
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    property var trackModel: LibraryModel
    property var playlistModel: PlaylistModel
    property string selectedCategory: "all"
    property string searchText: ""
    model: trackModel

    readonly property bool customPlaylistSelected:
        selectedCategory !== "all"
        && selectedCategory !== "favorites"
        && selectedCategory !== "history"

    function formatTime(ms): string {
        if (ms <= 0)
            return "--:--"
        var totalSec = Math.floor(ms / 1000)
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
    }

    function formatBpm(bpm): string {
        return bpm > 0 ? Math.round(bpm).toString() : "--"
    }

    function isCurrentTrack(id): bool {
        return PlaybackController.currentTrackId
               && PlaybackController.currentTrackId === id
    }

    function escapeHtml(value): string {
        return String(value).replace(/&/g, "&amp;")
                            .replace(/</g, "&lt;")
                            .replace(/>/g, "&gt;")
    }

    function highlighted(value): string {
        var plain = value || "---"
        var query = root.searchText.trim()
        if (query.length === 0)
            return root.escapeHtml(plain)
        var matchIndex = plain.toLowerCase().indexOf(query.toLowerCase())
        if (matchIndex < 0)
            return root.escapeHtml(plain)
        return root.escapeHtml(plain.slice(0, matchIndex))
             + "<span style=\"background-color:#6b2875;color:#ffffff;\">"
             + root.escapeHtml(plain.slice(matchIndex, matchIndex + query.length))
             + "</span>"
             + root.escapeHtml(plain.slice(matchIndex + query.length))
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

            HeaderText { text: "#"; Layout.preferredWidth: 40 }
            HeaderText { text: qsTr("歌曲"); Layout.fillWidth: true; Layout.minimumWidth: 160 }
            HeaderText { text: qsTr("收藏"); Layout.preferredWidth: 72 }
            HeaderText { text: qsTr("艺术家"); Layout.preferredWidth: 100 }
            HeaderText { text: qsTr("专辑"); Layout.preferredWidth: 110 }
            HeaderText { text: qsTr("评分"); Layout.preferredWidth: 88 }
            HeaderText { text: qsTr("BPM"); Layout.preferredWidth: 56 }
            HeaderText {
                text: qsTr("时长")
                Layout.preferredWidth: 56
                horizontalAlignment: Text.AlignRight
            }
            Item { Layout.preferredWidth: 30 }
        }
    }

    delegate: Rectangle {
        id: delegateRoot
        required property int index
        required property string trackId
        required property string title
        required property string artist
        required property string album
        required property url coverUrl
        required property bool favorite
        required property int rating
        required property double bpm
        required property double durationMs
        required property bool available
        required property string importError

        width: root.width
        height: 48
        color: root.isCurrentTrack(trackId) ? Theme.panel : "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: root.isCurrentTrack(delegateRoot.trackId) ? Theme.cyan : "transparent"
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingLg
            spacing: 0

            Text {
                text: delegateRoot.index + 1
                color: delegateRoot.available ? Theme.secondaryText : Theme.border
                font.family: Theme.fontPrimary
                font.pixelSize: 13
                Layout.preferredWidth: 40
            }

            RowLayout {
                spacing: Theme.spacingSm
                Layout.fillWidth: true
                Layout.minimumWidth: 160

                Image {
                    source: delegateRoot.coverUrl && delegateRoot.coverUrl !== ""
                            ? delegateRoot.coverUrl : Theme.icon("music-2-fill")
                    sourceSize.width: 32
                    sourceSize.height: 32
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    text: root.highlighted(delegateRoot.title || qsTr("未知标题"))
                    textFormat: Text.RichText
                    color: delegateRoot.available ? Theme.primaryText : Theme.secondaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            ToolButton {
                icon.source: delegateRoot.favorite
                             ? Theme.icon("heart-fill") : Theme.icon("heart-line")
                icon.color: delegateRoot.favorite ? Theme.favoriteRed : Theme.secondaryText
                icon.width: 16
                icon.height: 16
                Accessible.name: delegateRoot.favorite
                                 ? qsTr("取消收藏") : qsTr("添加收藏")
                focusPolicy: Qt.TabFocus
                Layout.preferredWidth: 72
                onClicked: if (root.trackModel && root.trackModel.setFavorite)
                               root.trackModel.setFavorite(
                                   delegateRoot.index, !delegateRoot.favorite)
                background: HoverBackground {}
            }

            BodyText {
                text: root.highlighted(delegateRoot.artist)
                textFormat: Text.RichText
                trackAvailable: delegateRoot.available
                Layout.preferredWidth: 100
            }
            BodyText {
                text: root.highlighted(delegateRoot.album)
                textFormat: Text.RichText
                trackAvailable: delegateRoot.available
                Layout.preferredWidth: 110
            }

            RowLayout {
                spacing: 1
                visible: SettingsController.autoReadRating
                Layout.preferredWidth: visible ? 88 : 0

                Repeater {
                    model: 5
                    delegate: ThemedIcon {
                        required property int index
                        source: index < delegateRoot.rating
                                ? Theme.icon("star-fill") : Theme.icon("star-line")
                        tint: index < delegateRoot.rating
                              ? Theme.ratingColor(index) : Theme.iconSecondary
                        sourceSize.width: 12
                        sourceSize.height: 12
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        TapHandler {
                            onTapped: {
                                var next = index + 1
                                if (delegateRoot.rating === next)
                                    next = 0
                                if (root.trackModel && root.trackModel.setRating)
                                    root.trackModel.setRating(delegateRoot.index, next)
                            }
                        }
                    }
                }
            }

            BodyText {
                text: root.formatBpm(delegateRoot.bpm)
                trackAvailable: delegateRoot.available
                Layout.preferredWidth: 56
            }
            BodyText {
                text: root.formatTime(delegateRoot.durationMs)
                trackAvailable: delegateRoot.available
                Layout.preferredWidth: 56
                horizontalAlignment: Text.AlignRight
            }

            ToolButton {
                Layout.preferredWidth: 30
                text: "\u22ee"
                Accessible.name: qsTr("歌单操作")
                onClicked: {
                    trackMenu.targetTrackId = delegateRoot.trackId
                    trackMenu.popup()
                }
                background: HoverBackground {}
            }
        }

        Text {
            visible: !delegateRoot.available && delegateRoot.importError.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 44
            text: delegateRoot.importError
            color: Theme.favoriteRed
            font.family: Theme.fontPrimary
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onDoubleTapped: if (delegateRoot.available
                                && root.trackModel && root.trackModel.playSourceRow)
                                 root.trackModel.playSourceRow(delegateRoot.index)
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                trackMenu.targetTrackId = delegateRoot.trackId
                trackMenu.popup()
            }
        }
    }

    Menu {
        id: trackMenu
        property string targetTrackId

        MenuItem {
            visible: root.customPlaylistSelected
            text: qsTr("从当前歌单移除")
            onTriggered: root.playlistModel.removeTrack(
                             root.selectedCategory, trackMenu.targetTrackId)
        }
        MenuSeparator { visible: root.customPlaylistSelected }

        Instantiator {
            model: root.playlistModel
            delegate: MenuItem {
                required property string playlistId
                required property string name
                text: qsTr("添加到 %1").arg(name)
                enabled: !root.playlistModel.containsTrack(
                             playlistId, trackMenu.targetTrackId)
                onTriggered: root.playlistModel.addTrack(
                                 playlistId, trackMenu.targetTrackId)
            }
            onObjectAdded: function(index, object) {
                trackMenu.insertItem(index + 2, object)
            }
            onObjectRemoved: function(index, object) {
                trackMenu.removeItem(object)
            }
        }
    }

    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    component HeaderText: Text {
        color: Theme.secondaryText
        font.family: Theme.fontPrimary
        font.pixelSize: 12
        font.weight: Font.Medium
    }

    component BodyText: Text {
        property bool trackAvailable: true
        color: trackAvailable ? Theme.secondaryText : Theme.border
        font.family: Theme.fontPrimary
        font.pixelSize: 13
        elide: Text.ElideRight
    }

    component HoverBackground: Rectangle {
        color: parent.pressed ? Theme.cyan
              : parent.visualFocus ? Theme.border
              : parent.hovered ? Theme.border : "transparent"
        border.color: parent.visualFocus ? Theme.cyan : "transparent"
        border.width: parent.visualFocus ? 2 : 0
        radius: Theme.radiusSm
    }
}
