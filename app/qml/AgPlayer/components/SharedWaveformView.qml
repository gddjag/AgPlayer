import QtQuick
import AgPlayer

Item {
    id: root
    objectName: "sharedWaveformView"
    property var waveformSession: null
    property var playback: PlaybackController
    property real opacityScale: 1.0
    property bool trackColorized: true
    readonly property string effectiveTrackId:
        waveformSession && waveformSession.trackId
        ? String(waveformSession.trackId)
        : playback ? String(playback.currentTrackId || "") : ""
    readonly property color trackBaseColor:
        waveformSession && waveformSession.trackPalette
        ? waveformSession.trackPalette.cool : colorForTrack(effectiveTrackId, 0)
    readonly property color trackProgressColor:
        waveformSession && waveformSession.trackPalette
        ? waveformSession.trackPalette.warm : colorForTrack(effectiveTrackId, 1)
    readonly property color trackMiddleColor:
        waveformSession && waveformSession.trackPalette
        ? waveformSession.trackPalette.highlight : colorForTrack(effectiveTrackId, 2)
    readonly property real effectiveDurationMs:
        waveformSession && Number(waveformSession.durationMs) > 0
        ? Number(waveformSession.durationMs)
        : playback && playback.durationMs > 0 ? playback.durationMs : 0

    function formatTime(ms) {
        var total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        return Math.floor(total / 60) + ":" + String(total % 60).padStart(2, "0")
    }

    function colorForTrack(trackId, salt) {
        var text = String(trackId || "AgPlayer")
        var hash = 2166136261
        for (var index = 0; index < text.length; ++index)
            hash = Math.imul(hash ^ text.charCodeAt(index), 16777619)
        hash = (hash + Math.imul(Number(salt || 0), -1640531527)) >>> 0
        return Qt.hsla((hash % 360) / 360, 0.76, 0.68, 1.0)
    }

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: waveform.verticalCenter
        width: 38
        horizontalAlignment: Text.AlignHCenter
        text: root.formatTime(root.playback ? root.playback.positionMs : 0)
        color: Qt.rgba(0.86, 0.91, 0.96, 0.72 * root.opacityScale)
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
        visualMode: SettingsController.waveformMode
        baseColor: root.trackColorized ? root.trackBaseColor
                                      : SettingsController.waveformMode === 0
                                        ? SettingsController.waveformSolidBaseColor
                                        : SettingsController.waveformRgbBaseColor
        progressColor: root.trackColorized ? root.trackProgressColor
                                          : SettingsController.waveformSolidProgressColor
        gradientStartColor: root.trackColorized ? root.trackBaseColor
                                               : SettingsController.waveformRgbStartColor
        gradientMiddleColor: root.trackColorized ? root.trackMiddleColor
                                                : SettingsController.waveformRgbMiddleColor
        gradientEndColor: root.trackColorized ? root.trackProgressColor
                                             : SettingsController.waveformRgbEndColor
        rgbProgress: SettingsController.waveformMode === 1
                     && SettingsController.waveformRgbProgress
        amplitudeScale: SettingsController.waveformHeight
        density: SettingsController.waveformDensity
        lineWidth: SettingsController.waveformThickness
        opacity: 0.58 * root.opacityScale
        Behavior on baseColor { ColorAnimation { duration: 520 } }
        Behavior on progressColor { ColorAnimation { duration: 520 } }
        Behavior on gradientStartColor { ColorAnimation { duration: 520 } }
        Behavior on gradientMiddleColor { ColorAnimation { duration: 520 } }
        Behavior on gradientEndColor { ColorAnimation { duration: 520 } }
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
        color: Qt.rgba(1, 1, 1, 0.82 * root.opacityScale)
    }

    Text {
        anchors.right: parent.right
        anchors.verticalCenter: waveform.verticalCenter
        width: 38
        horizontalAlignment: Text.AlignHCenter
        text: root.formatTime(root.effectiveDurationMs)
        color: Qt.rgba(0.86, 0.91, 0.96, 0.72 * root.opacityScale)
        font.pixelSize: 10
    }
}
