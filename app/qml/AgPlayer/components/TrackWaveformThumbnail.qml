import QtQuick
import AgPlayer

Item {
    id: root
    objectName: "trackWaveformThumbnail"

    property string trackId: ""
    property string sourcePath: ""
    property int delegateGeneration: 0
    property string mode: "Spectral"
    property var provider: TrackWaveformThumbnailProvider
    property var waveformPeaks: ""
    property var bassEnergy: ""
    property var midEnergy: ""
    property var highEnergy: ""
    property real brightness: SettingsController.trackWaveformBrightness

    property string requestedTrackId: ""
    property int requestedGeneration: 0
    property bool requestScheduled: false

    function cancelRequest() {
        if (requestedTrackId.length > 0 && provider)
            provider.cancel(requestedTrackId, requestedGeneration)
        requestedTrackId = ""
        requestedGeneration = 0
    }

    function requestEligible() {
        return enabled && provider && trackId.length > 0
                && sourcePath.length > 0 && parent
                && (typeof parent.active !== "boolean" || parent.active)
    }

    function performRequest() {
        requestScheduled = false
        cancelRequest()
        waveformPeaks = ""
        bassEnergy = ""
        midEnergy = ""
        highEnergy = ""
        if (!requestEligible())
            return
        requestedTrackId = trackId
        requestedGeneration = delegateGeneration
        provider.request(requestedTrackId, sourcePath, requestedGeneration, true)
    }

    function scheduleRequest() {
        if (requestScheduled)
            return
        requestScheduled = true
        requestTimer.start()
    }

    // The timer is owned by the wrapper, so delegate/Loader destruction also
    // discards the deferred callback. Qt.callLater can otherwise retain a QML
    // method after its context is gone during a rapid library model reset.
    Timer {
        id: requestTimer
        interval: 0
        repeat: false
        onTriggered: root.performRequest()
    }

    onTrackIdChanged: scheduleRequest()
    onSourcePathChanged: scheduleRequest()
    onDelegateGenerationChanged: scheduleRequest()
    onEnabledChanged: scheduleRequest()
    Component.onCompleted: scheduleRequest()
    Component.onDestruction: cancelRequest()

    Connections {
        target: root.provider
        function onThumbnailReady(readyTrackId, readyGeneration, peaks,
                                  bass, mid, high) {
            if (readyTrackId === root.requestedTrackId
                    && readyGeneration === root.requestedGeneration) {
                root.waveformPeaks = peaks
                root.bassEnergy = bass
                root.midEnergy = mid
                root.highEnergy = high
            }
        }
        function onSourceCacheInvalidated(invalidatedSourcePath) {
            if (invalidatedSourcePath === root.sourcePath
                    && root.requestEligible())
                root.scheduleRequest()
        }
    }

    Loader {
        id: thumbnailItemLoader
        objectName: "trackWaveformThumbnailItemLoader"
        anchors.fill: parent
        active: root.enabled
        opacity: root.brightness
        sourceComponent: Component {
            TrackWaveformThumbnailItem {
                objectName: "trackWaveformThumbnailItem"
                peaks: root.waveformPeaks
                waveformColor: Theme.listWaveformMono
                bass: root.mode === "Mono" ? "" : (root.bassEnergy || "")
                mid: root.mode === "Mono" ? "" : (root.midEnergy || "")
                high: root.mode === "Mono" ? "" : (root.highEnergy || "")
                lowColor: SettingsController.frequencyColorWaveform.lowColor
                midColor: SettingsController.frequencyColorWaveform.midColor
                highColor: SettingsController.frequencyColorWaveform.highColor
            }
        }
    }
}
