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
    property string waveformPlacement: "beforePrevious"
    property bool showPlaybackMode: true
    property bool showCueButton: false
    property bool highlightKeyboardFocus: false
    readonly property real playButtonCenterX:
        playPauseButton.x + playPauseButton.width / 2
    spacing: compact ? 4 : dense ? 8 : 16

    function keyboardFocused(control) {
        return control.activeFocus
            && (control.focusReason === Qt.TabFocusReason
                || control.focusReason === Qt.BacktabFocusReason)
    }

    function playbackModeName() {
        if (!root.playback)
            return qsTr("播放模式")
        switch (root.playback.mode) {
        case PlaybackController.Sequential: return qsTr("顺序播放")
        case PlaybackController.Shuffle: return qsTr("随机播放")
        case PlaybackController.RepeatOne: return qsTr("单曲循环")
        default: return qsTr("列表循环")
        }
    }

    ToolButton {
        objectName: root.waveformPlacement === "afterMode"
                    ? "leadingEqualizerButton" : "equalizerButton"
        visible: root.showEqualizer
                 && root.waveformPlacement === "beforePrevious"
        Layout.preferredWidth: root.compact ? 32 : 40
        Layout.preferredHeight: root.compact ? 32 : 40
        flat: true
        enabled: root.playback !== null
        icon.source: Theme.icon("equalizer-line")
        icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20
        contentItem.rotation: 90
        Accessible.name: qsTr("十八段图形均衡器")
        onClicked: root.openEqualizerRequested()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: Rectangle {
            color: parent.down ? Theme.surfacePressed
                : parent.hovered ? Theme.surfaceHover : "transparent"
            border.width: root.keyboardFocused(parent) ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
            Behavior on color { ColorAnimation { duration: 100 } }
        }
    }
    Component {
        id: waveformModeAction
        ToolButton {
            objectName: "waveformModeButton"
            flat: true
            enabled: root.playback !== null
            icon.source: Theme.icon("waveform-switch")
            icon.color: Theme.iconPrimary
            icon.width: 20; icon.height: 20
            Accessible.name: qsTr("切换波形样式")
            onClicked: SettingsController.cycleWaveformMode()
            ToolTip.text: Accessible.name; ToolTip.visible: hovered
            background: Rectangle {
                color: parent.down ? Theme.surfacePressed
                    : parent.hovered ? Theme.surfaceHover : "transparent"
                border.width: root.keyboardFocused(parent) ? 2 : 0
                border.color: Theme.focus
                radius: Theme.radiusSm
                Behavior on color { ColorAnimation { duration: 100 } }
            }
        }
    }
    Loader {
        active: root.showWaveformMode
                && root.waveformPlacement === "beforePrevious"
        visible: active
        Layout.preferredWidth: root.compact ? 32 : 40
        Layout.preferredHeight: root.compact ? 32 : 40
        sourceComponent: waveformModeAction
    }

    function cancelCueHold() {
        if (!cueButton.cueHoldActive)
            return
        cueButton.cueHoldActive = false
        if (root.playback && root.playback.cancelCue !== undefined)
            root.playback.cancelCue()
    }
    ToolButton {
        objectName: "previousButton"
        Layout.preferredWidth: root.compact ? 32 : 40
        Layout.preferredHeight: root.compact ? 32 : 40
        flat: true
        enabled: root.playback !== null
        icon.source: Theme.icon("skip-back-fill")
        icon.color: Theme.iconPrimary
        icon.width: 24; icon.height: 24
        Accessible.name: qsTr("上一首")
        onClicked: if (root.playback) root.playback.previous()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: Rectangle {
            color: root.highlightKeyboardFocus
                   ? (parent.down ? Theme.surfacePressed
                                  : parent.hovered ? Theme.hoverSurface
                                                   : "transparent")
                   : "transparent"
            border.width: root.keyboardFocused(parent) ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }
    ThemedIconButton {
        id: cueButton
        objectName: "rollingCueButton"
        visible: root.showCueButton
        Layout.preferredWidth: root.compact ? 46 : 52
        Layout.preferredHeight: root.compact ? 46 : 52
        flat: true
        enabled: root.playback !== null
        focusPolicy: Qt.StrongFocus
        property bool cueHoldActive: false
        Accessible.name: qsTr("CUE 预听")
        contentItem: Text {
            text: "CUE"
            color: Theme.warning
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        onPressed: {
            if (!root.playback || root.playback.cuePress === undefined)
                return
            cueHoldActive = true
            root.playback.cuePress()
        }
        onReleased: {
            if (!cueHoldActive)
                return
            cueHoldActive = false
            if (root.playback && root.playback.cueRelease !== undefined)
                root.playback.cueRelease()
        }
        onCanceled: root.cancelCueHold()
        Keys.onEscapePressed: function(event) {
            root.cancelCueHold()
            event.accepted = true
        }
        background: Rectangle {
            radius: width / 2
            color: cueButton.down || (root.playback
                                      && root.playback.cueAuditioning)
                   ? Qt.lighter(Theme.warning, 1.65)
                   : cueButton.hovered ? Theme.hoverSurface : Theme.panel
            border.width: root.keyboardFocused(cueButton) ? 4 : 3
            border.color: root.keyboardFocused(cueButton)
                          ? Theme.focus : Theme.warning
        }
    }
    ToolButton {
        id: playPauseButton
        objectName: "playPauseButton"
        Layout.preferredWidth: root.compact ? 46 : 52
        Layout.preferredHeight: root.compact ? 46 : 52
        flat: true
        icon.source: root.playback
                     && root.playback.state === PlaybackController.Playing
                     ? Theme.icon("pause-fill") : Theme.icon("play-fill")
        icon.color: Theme.iconPrimary
        icon.width: 24; icon.height: 24
        scale: down ? 0.95 : hovered ? 1.05 : 1.0
        enabled: root.playback !== null
        Accessible.name: root.playback
                         && root.playback.state === PlaybackController.Playing
                          ? qsTr("暂停") : qsTr("播放")
        onClicked: if (root.playback) root.playback.togglePlayback()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        Behavior on scale { NumberAnimation { duration: playPauseButton.down ? 150 : 200; easing.type: Easing.OutCubic } }
        background: Rectangle {
            id: playButtonBody
            objectName: "playButtonBody"
            radius: width / 2
            color: playPauseButton.hovered ? Theme.hoverSurface : Theme.panel
            border.width: root.keyboardFocused(playPauseButton) ? 4 : 3
            border.color: root.keyboardFocused(playPauseButton) ? Theme.focus
                          : root.showCueButton ? Theme.success
                          : root.playback
                            && root.playback.state === PlaybackController.Playing
                            ? Theme.playRingPlaying : Theme.playRingPaused
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }
    ToolButton {
        objectName: "nextButton"
        Layout.preferredWidth: root.compact ? 32 : 40
        Layout.preferredHeight: root.compact ? 32 : 40
        flat: true
        enabled: root.playback !== null
        icon.source: Theme.icon("skip-forward-fill")
        icon.color: Theme.iconPrimary
        icon.width: 24; icon.height: 24
        Accessible.name: qsTr("下一首")
        onClicked: if (root.playback) root.playback.next()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: Rectangle {
            color: root.highlightKeyboardFocus
                   ? (parent.down ? Theme.surfacePressed
                                  : parent.hovered ? Theme.hoverSurface
                                                   : "transparent")
                   : "transparent"
            border.width: root.keyboardFocused(parent) ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
        }
    }
    ToolButton {
        objectName: "modeButton"
        Layout.preferredWidth: root.compact ? 32 : 40
        Layout.preferredHeight: root.compact ? 32 : 40
        visible: root.showPlaybackMode
        flat: true
        enabled: root.playback !== null
        icon.source: {
            if (!root.playback)
                return Theme.icon("play-order-line")
            switch (root.playback.mode) {
            case PlaybackController.Sequential: return Theme.icon("play-order-line")
            case PlaybackController.RepeatOne: return Theme.icon("repeat-one-line-alt")
            case PlaybackController.Shuffle: return Theme.icon("shuffle-arrows-line")
            default: return Theme.icon("repeat-list-line")
            }
        }
        icon.color: Theme.iconPrimary
        icon.width: 20; icon.height: 20
        Accessible.name: root.playbackModeName()
        onClicked: if (root.playback) root.playback.cycleMode()
        ToolTip.text: Accessible.name; ToolTip.visible: hovered
        background: Rectangle {
            color: parent.down ? Theme.surfacePressed
                : parent.hovered ? Theme.surfaceHover : "transparent"
            border.width: root.keyboardFocused(parent) ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
            Behavior on color { ColorAnimation { duration: 100 } }
        }
    }
    Loader {
        active: root.showWaveformMode
                && root.waveformPlacement === "afterMode"
        visible: active
        Layout.preferredWidth: root.compact ? 32 : 40
        Layout.preferredHeight: root.compact ? 32 : 40
        sourceComponent: waveformModeAction
    }
    ToolButton {
        objectName: "equalizerButton"
        visible: root.showEqualizer
                 && root.waveformPlacement === "afterMode"
        Layout.preferredWidth: root.compact ? 32 : 40
        Layout.preferredHeight: root.compact ? 32 : 40
        flat: true
        enabled: root.playback !== null
        icon.source: Theme.icon("equalizer-line")
        icon.color: Theme.iconPrimary
        icon.width: 20
        icon.height: 20
        contentItem.rotation: 90
        Accessible.name: qsTr("十八段图形均衡器")
        onClicked: root.openEqualizerRequested()
        ToolTip.text: Accessible.name
        ToolTip.visible: hovered
        background: Rectangle {
            color: parent.down ? Theme.surfacePressed
                : parent.hovered ? Theme.surfaceHover : "transparent"
            border.width: root.keyboardFocused(parent) ? 2 : 0
            border.color: Theme.focus
            radius: Theme.radiusSm
            Behavior on color { ColorAnimation { duration: 100 } }
        }
    }
}
