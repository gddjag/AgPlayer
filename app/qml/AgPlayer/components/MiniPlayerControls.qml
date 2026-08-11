import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "miniPlayerControls"
    color: "transparent"

    property var playback: PlaybackController
    property var windows: WindowController
    property var rawWaveformLayers: ({})
    property real waveformDurationMs: 0
    // The decoder's clock is authoritative for seek and playback progress.
    // Analysis duration is only a fallback before a playable source exists.
    readonly property real effectiveDurationMs: playback && playback.durationMs > 0
                                                ? playback.durationMs
                                                : waveformDurationMs

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
        var row = currentRow()
        return row >= 0 ? LibraryModel.data(LibraryModel.index(row, 0), role) : ""
    }
    function currentTrackFavorite(): bool { return !!currentTrackValue(LibraryModel.FavoriteRole) }
    function currentTrackRating(): int {
        var value = parseInt(currentTrackValue(LibraryModel.RatingRole), 10)
        return isNaN(value) ? 0 : Math.max(0, Math.min(5, value))
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
        var sourcePeak = 0
        for (var sourceOffset = 0; sourceOffset < source.length; ++sourceOffset)
            sourcePeak = Math.max(sourcePeak, Number(source[sourceOffset]) || 0)
        var gain = sourcePeak > 0 ? Math.max(1, 1.0 / sourcePeak) : 0
        var result = new Array(half * 2)
        for (var index = 0; index < half; ++index) {
            var sourceIndex = Math.min(
                source.length - 1, Math.floor(index * source.length / half))
            var target = Math.min(1, Math.max(
                0, (Number(source[sourceIndex]) || 0) * gain))
            result[index] = target
            result[half * 2 - 1 - index] = target
        }
        return result
    }
    function applyWaveformMode() {
        if (SettingsController.waveformMode === 2)
            waveform.peaks = root.shapeSpectrum(playback ? playback.spectrum : [])
        else waveform.layers = { mix: rawWaveformLayers.mix || [] }
    }
    function loadWaveform() {
        var path = currentTrackValue(LibraryModel.PathRole)
        rawWaveformLayers = ({}); waveformDurationMs = 0
        waveform.layers = ({}); waveform.peaks = []
        if (path && playback) WaveformProvider.loadForTrack(playback.currentTrackId, path)
    }
    function modeName(): string {
        switch (playback ? playback.mode : PlaybackController.Sequential) {
        case PlaybackController.RepeatOne: return qsTr("单曲循环")
        case PlaybackController.Shuffle: return qsTr("随机播放")
        case PlaybackController.RepeatAll: return qsTr("列表循环")
        default: return qsTr("顺序播放")
        }
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
            Image {
                anchors.fill: parent
                anchors.margins: root.currentRow() >= 0 ? 0 : 16
                source: root.coverSource(); fillMode: Image.PreserveAspectFit; smooth: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 1
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 18; spacing: 3
                Text {
                    objectName: "miniTrackTitle"
                    text: root.currentTrackValue(LibraryModel.TitleRole) || qsTr("未加载歌曲")
                    color: Theme.primaryText; font.family: Theme.fontPrimary
                    font.pixelSize: 16; font.weight: Font.DemiBold
                    elide: Text.ElideRight; Layout.fillWidth: true
                }
                RowLayout {
                    objectName: "miniRating"; spacing: 2
                    Repeater {
                        model: 5
                        delegate: ThemedIcon {
                            required property int index
                            source: index < root.currentTrackRating() ? Theme.icon("star-fill") : Theme.icon("star-line")
                            tint: index < root.currentTrackRating() ? Theme.ratingColor(index) : Theme.iconSecondary
                            sourceSize.width: 16; sourceSize.height: 16
                            Layout.preferredWidth: 17; Layout.preferredHeight: 18
                        }
                    }
                }
                ToolButton {
                    id: favoriteButton
                    objectName: "miniFavoriteButton"
                    Layout.preferredWidth: 32; Layout.preferredHeight: 32
                    icon.source: root.currentTrackFavorite() ? Theme.icon("heart-fill") : Theme.icon("heart-line")
                    icon.color: root.currentTrackFavorite() ? Theme.favoriteRed : Theme.secondaryText
                    icon.width: 21; icon.height: 21; enabled: root.currentRow() >= 0
                    onClicked: if (playback) playback.toggleFavorite(); background: null
                }
            }
            Text {
                text: (root.currentTrackValue(LibraryModel.ArtistRole) || qsTr("未知艺术家"))
                      + " · " + (root.currentTrackValue(LibraryModel.AlbumRole) || qsTr("未知专辑"))
                color: Theme.secondaryText; font.family: Theme.fontPrimary; font.pixelSize: 9
                elide: Text.ElideRight; Layout.fillWidth: true; Layout.preferredHeight: 12
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                WaveformItem {
                    id: waveform
                    objectName: "miniWaveform"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 36
                    position: playback ? playback.positionMs : 0; duration: root.effectiveDurationMs
                    visualMode: SettingsController.waveformMode
                    baseColor: SettingsController.waveformMode === 0
                               ? SettingsController.waveformSolidBaseColor
                               : (SettingsController.waveformMode === 2
                                  ? SettingsController.spectrumSolidColor
                                  : SettingsController.waveformRgbBaseColor)
                    progressColor: SettingsController.waveformSolidProgressColor
                    gradientStartColor: SettingsController.waveformMode === 2
                                        && SettingsController.spectrumColorMode === 0
                                        ? SettingsController.spectrumSolidColor
                                        : SettingsController.spectrumRgbStartColor
                    gradientMiddleColor: SettingsController.waveformMode === 2
                                         && SettingsController.spectrumColorMode === 0
                                         ? SettingsController.spectrumSolidColor
                                         : SettingsController.spectrumRgbMiddleColor
                    gradientEndColor: SettingsController.waveformMode === 2
                                      && SettingsController.spectrumColorMode === 0
                                      ? SettingsController.spectrumSolidColor
                                      : SettingsController.spectrumRgbEndColor
                    rgbProgress: SettingsController.waveformRgbProgress
                    amplitudeScale: SettingsController.waveformMode === 2
                                    ? 1.0 : SettingsController.waveformHeight
                    density: SettingsController.waveformMode === 2
                             ? 1.0 : SettingsController.waveformDensity
                    lineWidth: SettingsController.waveformMode === 2
                                ? 3.0 : SettingsController.waveformThickness
                    onSeekRequested: positionMs => { if (playback) playback.seek(positionMs) }
                }
                Text {
                    objectName: "miniElapsedTime"
                    anchors.left: parent.left; anchors.bottom: parent.bottom
                    text: root.formatTime(playback ? playback.positionMs : 0)
                    color: Theme.secondaryText; font.pixelSize: 11
                }
                Text {
                    objectName: "miniDurationTime"
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    text: root.formatTime(playback ? playback.durationMs : 0)
                    color: Theme.secondaryText; font.pixelSize: 11
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
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    icon.source: Theme.icon("waveform-switch")
                    icon.color: Theme.iconPrimary
                    icon.width: 20
                    icon.height: 20
                    Accessible.name: qsTr("切换波形样式")
                    ToolTip.text: Accessible.name
                    ToolTip.visible: hovered
                    onClicked: SettingsController.waveformMode =
                               (SettingsController.waveformMode + 1) % 3
                    background: null
                }
                ToolButton {
                    id: modeButton; Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    objectName: "miniModeButton"
                    icon.source: !playback || playback.mode === PlaybackController.Sequential ? Theme.icon("play-order-line")
                               : playback.mode === PlaybackController.Shuffle ? Theme.icon("shuffle-arrows-line")
                               : playback.mode === PlaybackController.RepeatOne ? Theme.icon("repeat-one-line-alt")
                               : Theme.icon("repeat-list-line")
                    icon.color: Theme.iconPrimary; icon.width: 20; icon.height: 20
                    Accessible.name: root.modeName(); ToolTip.text: Accessible.name; ToolTip.visible: hovered
                    onClicked: if (playback) playback.cycleMode(); background: null
                }
                ToolButton {
                    id: previousButton; Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    icon.source: Theme.icon("skip-back-fill"); icon.color: Theme.primaryText
                    icon.width: 19; icon.height: 19; onClicked: if (playback) playback.previous(); background: null
                }
                ToolButton {
                    id: playPauseButton; Layout.preferredWidth: 34; Layout.preferredHeight: 34
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
                    icon.source: Theme.icon("skip-forward-fill"); icon.color: Theme.primaryText
                    icon.width: 19; icon.height: 19; onClicked: if (playback) playback.next(); background: null
                }
                Item {
                    id: volumeControl
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
                        interval: 240
                        onTriggered: {
                            if (!volumeSlider.pressed && !volumeSlider.activeFocus
                                    && !flyoutHover.hovered)
                                volumeControl.expanded = false
                        }
                    }

                    ToolButton {
                        id: muteButton
                        anchors.fill: parent
                        icon.source: playback && playback.muted ? Theme.icon("volume-mute-line") : Theme.icon("volume-up-fill")
                        icon.color: Theme.primaryText; icon.width: 20; icon.height: 20
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
                        visible: opacity > 0
                        enabled: volumeControl.expanded
                        x: volumeControl.expanded ? -(width - parent.width + 4)
                                                  : -10
                        y: (parent.height - height) / 2
                        opacity: volumeControl.expanded ? 1 : 0
                        z: 20
                        width: 138
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
                            anchors.leftMargin: 8
                            anchors.rightMargin: 6
                            spacing: 6
                            Slider {
                                id: volumeSlider
                                objectName: "miniVolumeSlider"
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 24
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
                                background: Rectangle {
                                    x: volumeSlider.leftPadding
                                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                    width: volumeSlider.availableWidth; height: 3; radius: 1.5
                                    color: Theme.border
                                    Rectangle { width: volumeSlider.visualPosition * parent.width; height: parent.height; radius: parent.radius; color: Theme.accent }
                                }
                                handle: Rectangle {
                                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                    width: 8; height: 8; radius: 4; color: Theme.primaryText
                                }
                            }
                            Text {
                                objectName: "miniVolumePercent"
                                text: Math.round((playback && !playback.muted ? playback.volume : 0) * 100) + "%"
                                color: Theme.primaryText
                                font.pixelSize: 9
                                Layout.preferredWidth: 26
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
        function onSpectrumChanged() { if (SettingsController.waveformMode === 2) root.applyWaveformMode() }
    }
    Connections {
        target: WaveformProvider
        function onWaveformReady(path, layers) {
            if (path === root.currentTrackValue(LibraryModel.PathRole)) {
                root.rawWaveformLayers = layers
                root.waveformDurationMs = Math.max(
                    0, Number(layers._durationMs) || 0)
                root.applyWaveformMode()
            }
        }
    }
    Connections { target: SettingsController; function onWaveformModeChanged() { root.applyWaveformMode() } }
    Component.onCompleted: root.loadWaveform()
}
