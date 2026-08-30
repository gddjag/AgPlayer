import QtQuick
import AgPlayer

Item {
    id: root
    objectName: "sharedWaveformView"
    property var waveformSession: null
    property var playback: PlaybackController
    property real opacityScale: 1.0
    readonly property real effectiveDurationMs:
        waveformSession && Number(waveformSession.durationMs) > 0
        ? Number(waveformSession.durationMs)
        : playback && playback.durationMs > 0 ? playback.durationMs : 0

    function formatTime(ms) {
        var total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0")
    }

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: waveform.verticalCenter
        width: 38
        horizontalAlignment: Text.AlignHCenter
        text: root.formatTime(root.playback ? root.playback.positionMs : 0)
        color: Qt.rgba(0.86, 0.91, 0.96, 0.72 * root.opacityScale) // theme-color-allow: immersive media visual contract
        font.pixelSize: 10
    }

    WaveformItem {
        id: waveform
        objectName: "immersiveWaveform"
        anchors.left: parent.left
        anchors.leftMargin: 48
        anchors.right: parent.right
        anchors.rightMargin: 48
        anchors.verticalCenter: parent.verticalCenter
        height: Math.min(44, parent.height)
        layers: root.waveformSession ? root.waveformSession.layers : ({})
        cursorPosition: root.playback ? root.playback.positionMs : 0
        duration: root.effectiveDurationMs
        // Immersive mode intentionally owns one waveform presentation. It
        // still consumes the shared cached mix/bass/mid/high layers, but its
        // colours always carry frequency meaning regardless of the normal
        // player waveform preference.
        visualMode: 3
        frequencyLowColor: SettingsController.waveformFrequencyLowColor
        frequencyMidColor: SettingsController.waveformFrequencyMidColor
        frequencyHighColor: SettingsController.waveformFrequencyHighColor
        amplitudeScale: SettingsController.waveformHeight
        density: SettingsController.waveformDensity
        lineWidth: SettingsController.waveformThickness
        opacity: 0.58 * root.opacityScale
        Behavior on frequencyLowColor { ColorAnimation { duration: 220 } }
        Behavior on frequencyMidColor { ColorAnimation { duration: 220 } }
        Behavior on frequencyHighColor { ColorAnimation { duration: 220 } }
        onSeekRequested: function(positionMs) {
            if (root.playback)
                root.playback.seek(positionMs)
        }
    }

    Rectangle {
        visible: SettingsController.waveformPlaybackGuide
        x: waveform.x + waveform.waveformCursorX
        anchors.verticalCenter: waveform.verticalCenter
        width: 1
        height: waveform.height - 8
        color: Qt.rgba(1, 1, 1, 0.82 * root.opacityScale) // theme-color-allow: immersive media visual contract
    }

    Text {
        anchors.right: parent.right
        anchors.verticalCenter: waveform.verticalCenter
        width: 38
        horizontalAlignment: Text.AlignHCenter
        text: root.formatTime(root.effectiveDurationMs)
        color: Qt.rgba(0.86, 0.91, 0.96, 0.72 * root.opacityScale) // theme-color-allow: immersive media visual contract
        font.pixelSize: 10
    }
}
