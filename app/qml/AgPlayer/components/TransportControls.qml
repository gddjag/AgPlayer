import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

RowLayout {
    id: root
    signal openEqualizerRequested()
    property var playback: PlaybackController
    property bool compact: false
    property bool dense: false
    property bool showEqualizer: true
    property bool showWaveformMode: true
    property bool showPlaybackMode: true
    property bool highlightKeyboardFocus: false
    spacing: compact ? 4 : dense ? 8 : 16

    function playbackModeName() {
        switch (root.playback.mode) {
        case root.playback.Sequential: return qsTr("顺序播放")
        case root.playback.Shuffle: return qsTr("随机播放")
        case root.playback.RepeatOne: return qsTr("单曲循环")
        default: return qsTr("列表循环")
        }
    }

    ToolButton {
        objectName: "equalizerButton"
        visible: root.showEqualizer && !root.compact
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
        onClicked: root.playback.previous()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: Rectangle {
            color: root.highlightKeyboardFocus
                   ? (parent.down ? Theme.surfacePressed
                                  : parent.hovered ? Theme.hoverSurface
                                                   : "transparent")
                   : "transparent"
            border.width: root.highlightKeyboardFocus && parent.activeFocus ? 1 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }
    ToolButton {
        id: playPauseButton
        objectName: "playPauseButton"
        Layout.preferredWidth: 52; Layout.preferredHeight: 52
        flat: true
        icon.source: root.playback.state === root.playback.Playing
                     ? Theme.icon("pause-fill") : Theme.icon("play-fill")
        icon.color: Theme.iconPrimary
        icon.width: 24; icon.height: 24
        scale: down ? 0.95 : hovered ? 1.05 : 1.0
        Accessible.name: root.playback.state === root.playback.Playing
                         ? qsTr("Pause") : qsTr("Play")
        onClicked: root.playback.togglePlayback()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        Behavior on scale { NumberAnimation { duration: playPauseButton.down ? 150 : 200; easing.type: Easing.OutCubic } }
        background: Rectangle {
            id: playButtonBody
            objectName: "playButtonBody"
            radius: width / 2
            color: playPauseButton.hovered ? Theme.hoverSurface : Theme.panel
            border.width: playPauseButton.activeFocus
                          && root.highlightKeyboardFocus ? 4 : 3
            border.color: playPauseButton.activeFocus
                          && root.highlightKeyboardFocus ? Theme.focus
                          : root.playback.state === root.playback.Playing
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
        onClicked: root.playback.next()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: Rectangle {
            color: root.highlightKeyboardFocus
                   ? (parent.down ? Theme.surfacePressed
                                  : parent.hovered ? Theme.hoverSurface
                                                   : "transparent")
                   : "transparent"
            border.width: root.highlightKeyboardFocus && parent.activeFocus ? 1 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }
    ToolButton {
        objectName: "modeButton"
        visible: root.showPlaybackMode
        flat: true
        icon.source: {
            switch (root.playback.mode) {
            case root.playback.Sequential: return Theme.icon("play-order-line")
            case root.playback.RepeatOne: return Theme.icon("repeat-one-line-alt")
            case root.playback.Shuffle: return Theme.icon("shuffle-arrows-line")
            default: return Theme.icon("repeat-list-line")
            }
        }
        icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20
        Accessible.name: root.playbackModeName()
        onClicked: root.playback.cycleMode()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: null
    }
}
