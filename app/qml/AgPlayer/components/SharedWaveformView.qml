import QtQuick
import AgPlayer

Item {
    id: root
    objectName: "sharedWaveformView"
    property var waveformSession: null
    property var playback: PlaybackController
    property real opacityScale: 1.0
    readonly property var frequencyWaveformSettings:
        SettingsController.frequencyColorWaveform
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
        color: "#DBE8F5" // theme-color-allow: immersive waveform time label
        opacity: 0.72 * root.opacityScale
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
        // Duration must be established before the initial playback position:
        // WaveformItem intentionally clamps positions to its known duration.
        duration: root.effectiveDurationMs
        position: root.playback ? root.playback.positionMs : 0
        cursorPosition: root.playback ? root.playback.positionMs : 0
        // Immersive mode intentionally owns one waveform presentation. It
        // still consumes the shared cached mix/bass/mid/high layers, but its
        // colours always carry frequency meaning regardless of the normal
        // player waveform preference.
        visualMode: 3
        baseColor: SettingsController.waveformMode === 0
                   ? SettingsController.waveformSolidBaseColor
                   : SettingsController.waveformRgbBaseColor
        spectralPalette: root.frequencyWaveformSettings.palette
        spectralUnplayedOpacity: root.frequencyWaveformSettings.unplayedOpacity
        amplitudeScale: SettingsController.waveformHeight
        density: SettingsController.waveformDensity
        lineWidth: SettingsController.waveformThickness
        opacity: 0.58 * root.opacityScale
        onSeekRequested: function(positionMs) {
            if (root.playback)
                root.playback.seek(positionMs)
        }
    }

    Text {
        anchors.right: parent.right
        anchors.verticalCenter: waveform.verticalCenter
        width: 38
        horizontalAlignment: Text.AlignHCenter
        text: root.formatTime(root.effectiveDurationMs)
        color: "#DBE8F5" // theme-color-allow: immersive waveform time label
        opacity: 0.72 * root.opacityScale
        font.pixelSize: 10
    }
}
