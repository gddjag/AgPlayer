import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer
import "PlayerPresentation.js" as PlayerPresentation

Rectangle {
    id: root
    objectName: "miniPlayerControls"
    color: "transparent"

    property var playback: PlaybackController
    property var windows: WindowController
    property var waveformSession: null
    property bool waveformActive: true
    readonly property var frequencyWaveformSettings:
        SettingsController.frequencyColorWaveform
    property var rawWaveformLayers: ({})
    property real waveformDurationMs: 0
    property int libraryRevision: 0
    readonly property var actionProfile: PlayerPresentation.profile("mini")
    // The complete decoded PCM duration is the waveform clock; metadata is a
    // fallback only until analysis finishes.
    readonly property real effectiveDurationMs: waveformDurationMs > 0
                                                ? waveformDurationMs
                                                : playback && playback.durationMs > 0
                                                  ? playback.durationMs : 0

    property alias playPauseButton: playPauseButton
    property alias previousButton: previousButton
    property alias nextButton: nextButton
    property alias modeButton: modeButton
    property alias favoriteButton: favoriteButton
    property alias muteButton: muteButton
    property alias volumeSlider: volumeSlider

    function formatTime(ms): string {
        var total = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(total / 60) + ":" + ((total % 60) < 10 ? "0" : "") + (total % 60)
    }
    function currentRow(): int {
        return playback ? LibraryModel.indexForTrackId(playback.currentTrackId) : -1
    }
    function currentTrackValue(role): variant {
        // Keep every metadata binding dependent on the model notification;
        // QML cannot infer that a value retrieved through LibraryModel.data()
        // changes when a different role of the same row changes.
        if (libraryRevision < 0)
            return ""
        var row = currentRow()
        return row >= 0 ? LibraryModel.data(LibraryModel.index(row, 0), role) : ""
    }
    function currentTrackFavorite(): bool { return !!currentTrackValue(LibraryModel.FavoriteRole) }
    function currentTrackRating(): int {
        var value = parseInt(currentTrackValue(LibraryModel.RatingRole), 10)
        return isNaN(value) ? 0 : Math.max(0, Math.min(5, value))
    }
    function currentTrackTags(): string {
        var tags = currentTrackValue(LibraryModel.TagsRole)
        if (!tags || tags.length === 0)
            return ""
        var values = []
        for (var index = 0; index < tags.length; ++index)
            values.push(String(tags[index]))
        return values.join("、")
    }
    function coverSource(): string {
        var cover = currentTrackValue(LibraryModel.CoverUrlRole)
        return cover ? cover : "qrc:/qt/qml/AgPlayer/assets/brand/logo-mark.png"
    }
    function shapeSpectrum(values) {
        var source = values || []
        if (source.length === 0)
            return []
        var half = 64
        var result = new Array(half * 2)
        for (var index = 0; index < half; ++index) {
            var sourceIndex = Math.min(
                source.length - 1, Math.floor(index * source.length / half))
            var target = Math.min(1, Math.max(
                0, Math.sqrt(Number(source[sourceIndex]) || 0) * 1.35))
            result[index] = target
            result[half * 2 - 1 - index] = target
        }
        return result
    }
    function applyWaveformMode() {
        if (!waveformActive) {
            fullTrackWaveform.layers = ({})
            fullTrackWaveform.peaks = []
            return
        }
        if (SettingsController.waveformMode === 2) {
            fullTrackWaveform.peaks = root.shapeSpectrum(
                        playback ? playback.spectrum : [])
        } else {
            fullTrackWaveform.layers = rawWaveformLayers || ({})
        }
    }
    function loadWaveform() {
        rawWaveformLayers = waveformActive && waveformSession ? waveformSession.layers : ({})
        waveformDurationMs = waveformSession
                ? Number(waveformSession.durationMs || 0) : 0
        applyWaveformMode()
    }
    function modeName(): string {
        switch (playback ? playback.mode : PlaybackController.Sequential) {
        case PlaybackController.RepeatOne: return qsTr("单曲循环")
        case PlaybackController.Shuffle: return qsTr("随机播放")
        case PlaybackController.RepeatAll: return qsTr("列表循环")
        default: return qsTr("顺序播放")
        }
    }

    Connections {
        target: LibraryModel
        function onDataChanged() { ++root.libraryRevision }
        function onModelReset() { ++root.libraryRevision }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14; anchors.rightMargin: 14
        anchors.topMargin: 8; anchors.bottomMargin: 4
        spacing: 14

        Rectangle {
            objectName: "miniCover"
            Layout.preferredWidth: 128; Layout.preferredHeight: 128
            radius: Theme.radiusSm; color: Theme.panel; clip: true
            FallbackCoverImage {
                id: miniCoverImage
                requestedSource: root.coverSource()
                anchors.fill: parent
                anchors.margins: usingFallback ? 16 : 0
                fillMode: Image.PreserveAspectFit
                smooth: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 2
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20
                Text {
                    id: miniTrackTitle
                    objectName: "miniTrackTitle"
                    text: root.currentTrackValue(LibraryModel.TitleRole) || qsTr("未加载歌曲")
                    color: Theme.primaryText; font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeSection; font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, Math.max(0, parent.width - favoriteButton.width - 6))
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                }
                ToolButton {
                    id: favoriteButton
                    objectName: "miniFavoriteButton"
                    width: 16
                    height: 16
                    anchors.left: miniTrackTitle.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    padding: 0
                    icon.source: root.currentTrackFavorite() ? Theme.icon("heart-fill") : Theme.icon("heart-line")
                    icon.color: root.currentTrackFavorite() ? Theme.favoriteRed : Theme.secondaryText
                    icon.width: 16; icon.height: 16; enabled: root.currentRow() >= 0
                    onClicked: if (playback) playback.toggleFavorite(); background: null
                }
            }
            RowLayout {
                objectName: "miniMetadataRow"
                Layout.fillWidth: true; Layout.preferredHeight: 26; spacing: 4
                Text {
                    id: miniArtist
                    objectName: "miniArtist"
                    text: root.currentTrackValue(LibraryModel.ArtistRole)
                          || qsTr("未知艺术家")
                    color: Theme.secondaryText; font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption; elide: Text.ElideRight; wrapMode: Text.NoWrap
                    Layout.preferredWidth: Math.min(implicitWidth, 90)
                    Layout.maximumWidth: 90
                    ToolTip.visible: miniArtistHover.hovered && truncated
                    ToolTip.text: text
                    HoverHandler { id: miniArtistHover }
                }
                Text {
                    objectName: "miniArtistAlbumSeparator"
                    text: "·"; color: Theme.secondaryText; font.pixelSize: Theme.fontSizeCaption
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    id: miniAlbum
                    objectName: "miniAlbum"
                    text: root.currentTrackValue(LibraryModel.AlbumRole)
                          || qsTr("未知专辑")
                    color: Theme.secondaryText; font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption; elide: Text.ElideRight; wrapMode: Text.NoWrap
                    Layout.preferredWidth: Math.min(implicitWidth, 90)
                    Layout.maximumWidth: 90
                    ToolTip.visible: miniAlbumHover.hovered && truncated
                    ToolTip.text: text
                    HoverHandler { id: miniAlbumHover }
                }
                Text {
                    objectName: "miniTagSeparator"
                    text: "·"; visible: miniTags.visible
                    color: Theme.tagSecondaryText; font.pixelSize: Theme.fontSizeCaption
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    id: miniTags
                    objectName: "miniTags"
                    text: root.currentTrackTags()
                    visible: text.length > 0
                    color: Theme.tagSecondaryText; font.family: Theme.fontPrimary
                    font.pixelSize: Theme.fontSizeCaption; elide: Text.ElideRight; wrapMode: Text.NoWrap
                    Layout.preferredWidth: Math.min(implicitWidth, 70)
                    Layout.maximumWidth: 70
                    ToolTip.visible: miniTagsHover.hovered && truncated
                    ToolTip.text: text
                    HoverHandler { id: miniTagsHover }
                }
                RowLayout {
                    id: miniRating
                    objectName: "miniRating"; spacing: 1
                    Layout.alignment: Qt.AlignVCenter
                    Repeater {
                        model: 5
                        delegate: ThemedIcon {
                            required property int index
                            objectName: "miniRatingStar-" + index
                            source: index < root.currentTrackRating() ? Theme.icon("star-fill") : Theme.icon("star-line")
                            tint: index < root.currentTrackRating() ? Theme.ratingColor(index) : Theme.iconSecondary
                            sourceSize.width: 16; sourceSize.height: 16
                            Layout.preferredWidth: sourceSize.width
                            Layout.preferredHeight: sourceSize.height
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            Item {
                objectName: "miniWaveformContainer"
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                clip: true

                FullTrackWaveformView {
                    id: fullTrackWaveform
                    objectName: "miniFullTrackWaveform"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 36
                    waveformObjectName: "miniWaveform"
                    playedClipObjectName: "miniWaveformPlayedClip"
                    playedWaveformObjectName: "miniPlayedWaveform"
                    interactionObjectName: "miniWaveformInteractionSurface"
                    hoverGuideObjectName: "miniWaveformHoverGuide"
                    hoverCapsuleObjectName: "miniWaveformHoverTimeCapsule"
                    playbackGuideObjectName: "miniWaveformPlaybackGuide"
                    duration: root.effectiveDurationMs
                    position: playback ? playback.positionMs : 0
                    playbackGuideColor: Theme.playbackGuide
                    onSeekRequested: function(positionMs) {
                        if (playback) playback.seek(positionMs)
                    }
                }
                Text {
                    objectName: "miniElapsedTime"
                    anchors.left: parent.left; anchors.bottom: parent.bottom
                    text: root.formatTime(playback ? playback.positionMs : 0)
                    color: Theme.secondaryText; font.pixelSize: Theme.fontSizeCaption
                }
                Text {
                    objectName: "miniDurationTime"
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    text: root.formatTime(playback ? playback.durationMs : 0)
                    color: Theme.secondaryText; font.pixelSize: Theme.fontSizeCaption
                }
            }
            RowLayout {
                id: transport
                objectName: "miniTransport"
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 3
                Item { Layout.fillWidth: true }
                ToolButton {
                    id: waveformModeButton
                    objectName: "miniWaveformModeButton"
                    visible: PlayerPresentation.hasAction(
                                 root.actionProfile, "miniWaveformModeButton")
                    Layout.preferredWidth: 28
                    Layout.minimumWidth: Layout.preferredWidth
                    Layout.maximumWidth: Layout.preferredWidth
                    Layout.preferredHeight: 28
                    icon.source: Theme.icon("waveform-switch")
                    icon.color: Theme.iconPrimary
                    icon.width: 16
                    icon.height: 16
                    Accessible.name: qsTr("切换波形样式")
                    ToolTip.text: Accessible.name
                    ToolTip.visible: hovered
                    onClicked: SettingsController.cycleWaveformMode()
                    background: null
                }
                ToolButton {
                    id: previousButton; Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    objectName: "miniPreviousButton"
                    icon.source: Theme.icon("skip-back-fill"); icon.color: Theme.primaryText
                    icon.width: 19; icon.height: 19; onClicked: if (playback) playback.previous(); background: null
                }
                ToolButton {
                    id: playPauseButton; Layout.preferredWidth: 34; Layout.preferredHeight: 34
                    objectName: "miniPlayPauseButton"
                    icon.source: playback && playback.state === PlaybackController.Playing ? Theme.icon("pause-fill") : Theme.icon("play-fill")
                    icon.color: Theme.primaryText; icon.width: 18; icon.height: 18
                    onClicked: if (playback) playback.togglePlayback()
                    background: Rectangle {
                        objectName: "miniPlayButtonBody"
                        radius: width / 2; color: Theme.panel; border.width: 3
                        border.color: playback && playback.state === PlaybackController.Playing
                                      ? Theme.playRingPlaying : Theme.playRingPaused
                    }
                }
                ToolButton {
                    id: nextButton; Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    objectName: "miniNextButton"
                    icon.source: Theme.icon("skip-forward-fill"); icon.color: Theme.primaryText
                    icon.width: 19; icon.height: 19; onClicked: if (playback) playback.next(); background: null
                }
                ToolButton {
                    id: modeButton; Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    objectName: "miniModeButton"
                    icon.source: !playback || playback.mode === PlaybackController.Sequential ? Theme.icon("play-order-line")
                               : playback.mode === PlaybackController.Shuffle ? Theme.icon("shuffle-arrows-line")
                               : playback.mode === PlaybackController.RepeatOne ? Theme.icon("repeat-one-line-alt")
                               : Theme.icon("repeat-list-line")
                    icon.color: Theme.iconPrimary; icon.width: 16; icon.height: 16
                    Accessible.name: root.modeName(); ToolTip.text: Accessible.name; ToolTip.visible: hovered
                    onClicked: if (playback) playback.cycleMode(); background: null
                }
                Item {
                    id: volumeControl
                    objectName: "miniVolumeControl"
                    property alias expandedForQa: volumeControl.expanded
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    property bool expanded: false

                    Timer {
                        id: miniVolumeOpenTimer
                        interval: 120
                        onTriggered: volumeControl.expanded = true
                    }
                    Timer {
                        id: miniVolumeCloseTimer
                        objectName: "miniVolumeCloseTimer"
                        // Leave enough time to cross the small gap from the
                        // mute button to the right-hand volume slider.
                        interval: 2000
                        onTriggered: {
                            if (!volumeSlider.pressed && !flyoutHover.hovered)
                                volumeControl.expanded = false
                        }
                    }

                    ToolButton {
                        id: muteButton
                        objectName: "miniMuteButton"
                        width: 28
                        height: parent.height
                        anchors.left: parent.left
                        icon.source: playback && playback.muted ? Theme.icon("volume-mute-line") : Theme.icon("volume-up-line")
                        icon.color: Theme.primaryText; icon.width: 16; icon.height: 16
                        onClicked: if (playback) playback.toggleMuted(); background: null
                    }
                    HoverHandler {
                        id: volumeHover
                        onHoveredChanged: {
                            if (hovered) {
                                miniVolumeCloseTimer.stop()
                                miniVolumeOpenTimer.restart()
                            } else {
                                miniVolumeOpenTimer.stop()
                                miniVolumeCloseTimer.restart()
                            }
                        }
                    }

                    Rectangle {
                        id: volumeFlyout
                        objectName: "miniVolumeFlyout"
                        visible: opacity > 0
                        enabled: volumeControl.expanded
                        x: volumeControl.expanded ? muteButton.width : 0
                        y: (parent.height - height) / 2
                        opacity: volumeControl.expanded ? 1 : 0
                        z: 20
                        width: parent.width - muteButton.width
                        height: 32
                        radius: Theme.radiusSm
                        color: Theme.elevated
                        border.color: Theme.border

                        Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                        HoverHandler {
                            id: flyoutHover
                            onHoveredChanged: {
                                if (hovered) miniVolumeCloseTimer.stop()
                                else miniVolumeCloseTimer.restart()
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 4
                            ThemedSlider {
                                id: volumeSlider
                                objectName: "miniVolumeSlider"
                                Layout.preferredWidth: 60
                                Layout.preferredHeight: Theme.controlHeightCompact
                                from: 0; to: 1
                                value: playback && !playback.muted ? playback.volume : 0
                                onMoved: if (playback) playback.setVolume(value)
                                onPressedChanged: {
                                    if (pressed) {
                                        miniVolumeCloseTimer.stop()
                                        volumeControl.expanded = true
                                    } else {
                                        miniVolumeCloseTimer.restart()
                                    }
                                }
                                trackColor: Theme.border
                                handleColor: Theme.primaryText
                            }
                            Text {
                                objectName: "miniVolumePercent"
                                text: Math.round((playback && !playback.muted ? playback.volume : 0) * 100) + "%"
                                color: Theme.primaryText
                                font.pixelSize: Theme.fontSizeCaption
                                Layout.preferredWidth: 34
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
    Connections {
        target: playback; ignoreUnknownSignals: true
        function onTrackIndexChanged() { root.loadWaveform() }
        function onCurrentTrackIdChanged() { root.loadWaveform() }
        function onSpectrumChanged() {
            if (root.waveformActive && SettingsController.waveformMode === 2)
                root.applyWaveformMode()
        }
    }
    Connections {
        target: root.waveformSession
        ignoreUnknownSignals: true
        function onLayersChanged() { root.loadWaveform() }
        function onDurationMsChanged() { root.loadWaveform() }
    }
    Connections {
        target: SettingsController
        function onWaveformModeChanged() { root.applyWaveformMode() }
    }
    onWaveformSessionChanged: root.loadWaveform()
    onWaveformActiveChanged: root.loadWaveform()
    Component.onCompleted: root.loadWaveform()
}
