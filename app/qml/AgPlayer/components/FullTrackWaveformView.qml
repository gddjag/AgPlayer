import QtQuick
import AgPlayer

Item {
    id: root

    property string waveformObjectName: "fullTrackWaveform"
    property string playedClipObjectName: "fullTrackWaveformPlayedClip"
    property string playedWaveformObjectName: "fullTrackPlayedWaveform"
    property string interactionObjectName: "fullTrackWaveformInteraction"
    property string hoverGuideObjectName: "fullTrackWaveformHoverGuide"
    property string hoverCapsuleObjectName: "fullTrackWaveformHoverTimeCapsule"
    property string playbackGuideObjectName: "fullTrackWaveformPlaybackGuide"

    property var layers: ({})
    property var peaks: []
    property real duration: 0
    property real position: 0
    property real analysisProgress: 1
    property int visualMode: SettingsController.waveformMode
    property color baseColor: visualMode === 0
                              ? SettingsController.waveformSolidBaseColor
                              : SettingsController.waveformRgbBaseColor
    property color progressColor: SettingsController.waveformSolidProgressColor
    property color gradientStartColor: visualMode === 2
                                       ? (SettingsController.spectrumColorMode === 0
                                          ? SettingsController.spectrumSolidColor
                                          : SettingsController.spectrumRgbStartColor)
                                       : SettingsController.waveformRgbStartColor
    property color gradientMiddleColor: visualMode === 2
                                        ? (SettingsController.spectrumColorMode === 0
                                           ? SettingsController.spectrumSolidColor
                                           : SettingsController.spectrumRgbMiddleColor)
                                        : SettingsController.waveformRgbMiddleColor
    property color gradientEndColor: visualMode === 2
                                     ? (SettingsController.spectrumColorMode === 0
                                        ? SettingsController.spectrumSolidColor
                                        : SettingsController.spectrumRgbEndColor)
                                     : SettingsController.waveformRgbEndColor
    readonly property var frequencySettings:
        SettingsController.frequencyColorWaveform
    property real amplitudeScale: visualMode === 2
                                  ? 1.0 : SettingsController.waveformHeight
    property real density: visualMode === 2
                           ? 1.0 : SettingsController.waveformDensity
    property real lineWidth: visualMode === 2
                             ? 3.0 : SettingsController.waveformThickness
    property bool rgbProgress: visualMode === 1
                               && SettingsController.waveformRgbProgress
    property bool hoverPreviewEnabled: SettingsController.waveformHoverTimePreview
    property bool playbackGuideEnabled: SettingsController.waveformPlaybackGuide
    property color playbackGuideColor: Theme.playbackGuide

    readonly property real hoverPosition: waveform.hoverPosition
    readonly property real waveformCursorX: waveform.waveformCursorX
    property alias waveformItem: waveform
    property alias playedWaveformItem: playedWaveform

    signal seekRequested(real positionMs)

    function formatTime(ms) {
        var total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        return Math.floor(total / 60) + ":"
                + String(total % 60).padStart(2, "0")
    }

    function timeForX(x) { return waveform.timeForX(x) }
    function pixelForTime(positionMs) { return waveform.pixelForTime(positionMs) }
    function setHoverPositionForInteraction(positionMs) {
        waveform.setHoverPositionForInteraction(positionMs)
    }

    clip: true

    WaveformItem {
        id: waveform
        objectName: root.waveformObjectName
        anchors.fill: parent
        pointerInteractionEnabled: false
        layers: root.layers
        peaks: root.peaks
        position: 0
        cursorPosition: Math.max(0, Math.min(root.duration, root.position))
        duration: root.duration
        analysisProgress: root.analysisProgress
        visualMode: root.visualMode
        baseColor: root.baseColor
        progressColor: root.progressColor
        gradientStartColor: root.gradientStartColor
        gradientMiddleColor: root.gradientMiddleColor
        gradientEndColor: root.gradientEndColor
        lowColor: root.frequencySettings.lowColor
        midColor: root.frequencySettings.midColor
        highColor: root.frequencySettings.highColor
        frequencyUnplayedOpacity: Theme.nonImmersiveSpectralUnplayedOpacity
        rgbProgress: root.rgbProgress
        amplitudeScale: root.amplitudeScale
        density: root.density
        lineWidth: root.lineWidth
    }

    Item {
        id: playedClip
        objectName: root.playedClipObjectName
        width: Math.max(0, Math.min(root.width, waveform.waveformCursorX))
        height: root.height
        clip: true
        enabled: false

        WaveformItem {
            id: playedWaveform
            objectName: root.playedWaveformObjectName
            enabled: false
            width: root.width
            height: root.height
            layers: root.layers
            peaks: root.peaks
            duration: root.duration
            position: root.duration
            visualMode: root.visualMode
            baseColor: root.baseColor
            progressColor: root.progressColor
            gradientStartColor: root.gradientStartColor
            gradientMiddleColor: root.gradientMiddleColor
            gradientEndColor: root.gradientEndColor
            lowColor: root.frequencySettings.lowColor
            midColor: root.frequencySettings.midColor
            highColor: root.frequencySettings.highColor
            frequencyUnplayedOpacity: Theme.nonImmersiveSpectralUnplayedOpacity
            rgbProgress: root.rgbProgress
            amplitudeScale: root.amplitudeScale
            density: root.density
            lineWidth: root.lineWidth
        }
    }

    MouseArea {
        id: interaction
        objectName: root.interactionObjectName
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.ArrowCursor
        z: 5

        function updatePreviewAt(x) {
            waveform.setHoverPositionForInteraction(waveform.timeForX(x))
        }
        onPositionChanged: mouse => updatePreviewAt(mouse.x)
        onEntered: updatePreviewAt(mouseX)
        onPressed: mouse => {
            updatePreviewAt(mouse.x)
            root.seekRequested(waveform.timeForX(mouse.x))
        }
        onExited: waveform.setHoverPositionForInteraction(-1)
    }

    Rectangle {
        objectName: root.hoverGuideObjectName
        visible: root.hoverPreviewEnabled && waveform.hoverPosition >= 0
        x: Math.max(0, Math.min(root.width - width,
                               waveform.pixelForTime(waveform.hoverPosition)))
        width: 1
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: "#54ff84" // theme-color-allow: waveform hover guide
        opacity: 0.96
        z: 6
    }

    Rectangle {
        objectName: root.hoverCapsuleObjectName
        visible: root.hoverPreviewEnabled && waveform.hoverPosition >= 0
        x: Math.max(0, Math.min(root.width - width,
                               waveform.pixelForTime(waveform.hoverPosition)
                               - width / 2))
        y: 2
        width: hoverTime.implicitWidth + 12
        height: hoverTime.implicitHeight + 6
        radius: height / 2
        color: Theme.panel
        border.color: "#54ff84" // theme-color-allow: waveform hover guide
        z: 7

        Text {
            id: hoverTime
            anchors.centerIn: parent
            text: root.formatTime(waveform.hoverPosition)
            color: Theme.primaryText
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
        }
    }

    Rectangle {
        objectName: root.playbackGuideObjectName
        visible: root.playbackGuideEnabled
        x: Math.max(0, Math.min(root.width, waveform.waveformCursorX))
        width: 1
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: root.playbackGuideColor
        z: 10
    }
}
