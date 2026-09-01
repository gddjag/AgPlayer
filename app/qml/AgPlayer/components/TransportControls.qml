import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

RowLayout {
    id: root
    signal openEqualizerRequested()
    property bool compact: false
    property bool dense: false
    property bool showWaveformMode: true
    spacing: compact ? 4 : dense ? 8 : 16

    function playbackModeName() {
        switch (PlaybackController.mode) {
        case PlaybackController.Sequential: return qsTr("顺序播放")
        case PlaybackController.Shuffle: return qsTr("随机播放")
        case PlaybackController.RepeatOne: return qsTr("单曲循环")
        default: return qsTr("列表循环")
        }
    }

    ToolButton {
        objectName: "equalizerButton"
        visible: !root.compact
        flat: true
        icon.source: Theme.icon("equalizer-line")
        icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20
        contentItem.rotation: 90
        Accessible.name: qsTr("十八段图形均衡器")
        onClicked: root.openEqualizerRequested()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: null
    }
    ToolButton {
        objectName: "waveformModeButton"
        visible: root.showWaveformMode && !root.compact
        flat: true
        icon.source: Theme.icon("waveform-switch")
        icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20
        Accessible.name: qsTr("Change waveform mode")
        onClicked: SettingsController.cycleWaveformMode()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: null
    }
    ToolButton {
        objectName: "previousButton"
        flat: true
        icon.source: Theme.icon("skip-back-fill")
        icon.color: Theme.iconPrimary
        icon.width: 24; icon.height: 24
        Accessible.name: qsTr("Previous track")
        onClicked: PlaybackController.previous()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: null
    }
    ToolButton {
        id: playPauseButton
        objectName: "playPauseButton"
        Layout.preferredWidth: 52; Layout.preferredHeight: 52
        flat: true
        icon.source: PlaybackController.state === PlaybackController.Playing
                     ? Theme.icon("pause-fill") : Theme.icon("play-fill")
        icon.color: Theme.iconPrimary
        icon.width: 24; icon.height: 24
        scale: down ? 0.95 : hovered ? 1.05 : 1.0
        Accessible.name: PlaybackController.state === PlaybackController.Playing
                         ? qsTr("Pause") : qsTr("Play")
        onClicked: PlaybackController.togglePlayback()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        Behavior on scale { NumberAnimation { duration: playPauseButton.down ? 150 : 200; easing.type: Easing.OutCubic } }
        background: Rectangle {
            id: playButtonBody
            objectName: "playButtonBody"
            radius: width / 2
            color: playPauseButton.hovered ? Theme.hoverSurface : Theme.panel
            border.width: 3
            border.color: PlaybackController.state === PlaybackController.Playing
                          ? Theme.playRingPlaying : Theme.playRingPaused
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }
    ToolButton {
        objectName: "nextButton"
        flat: true
        icon.source: Theme.icon("skip-forward-fill")
        icon.color: Theme.iconPrimary
        icon.width: 24; icon.height: 24
        Accessible.name: qsTr("Next track")
        onClicked: PlaybackController.next()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: null
    }
    ToolButton {
        objectName: "modeButton"
        flat: true
        icon.source: {
            switch (PlaybackController.mode) {
            case PlaybackController.Sequential: return Theme.icon("play-order-line")
            case PlaybackController.RepeatOne: return Theme.icon("repeat-one-line-alt")
            case PlaybackController.Shuffle: return Theme.icon("shuffle-arrows-line")
            default: return Theme.icon("repeat-list-line")
            }
        }
        icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20
        Accessible.name: root.playbackModeName()
        onClicked: PlaybackController.cycleMode()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: null
    }
}
