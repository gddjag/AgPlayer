import QtQuick
import AgPlayer

QtObject {
    id: root
    objectName: "sharedWaveformSession"

    property var layers: ({})
    property real durationMs: 0
    property real generation: 0
    property int libraryRevision: 0
    readonly property string trackId:
        String(PlaybackController.currentTrackId || "")
    readonly property var trackPalette: paletteForTrack(trackId)
    readonly property double trackColorHash: Number(trackPalette.hash)
    readonly property color paletteCoolColor: trackPalette.cool
    readonly property color paletteWarmColor: trackPalette.warm
    readonly property color paletteHighlightColor: trackPalette.highlight
    readonly property color paletteAmbientColor: trackPalette.ambient

    function paletteForTrack(candidateTrackId) {
        var text = String(candidateTrackId || "AgPlayer")
        var hash = 2166136261
        for (var index = 0; index < text.length; ++index)
            hash = Math.imul(hash ^ text.charCodeAt(index), 16777619)
        hash = hash >>> 0
        var jitter = ((hash % 61) - 30) / 360.0
        return {
            hash: hash,
            cool: Qt.hsla((0.52 + jitter + 1.0) % 1.0, 0.78, 0.66, 1.0), // theme-color-allow: immersive media visual contract
            warm: Qt.hsla((0.96 + jitter * 0.55 + 1.0) % 1.0, 0.82, 0.65, 1.0), // theme-color-allow: immersive media visual contract
            highlight: Qt.hsla((0.11 + jitter * 0.25 + 1.0) % 1.0, 0.76, 0.78, 1.0), // theme-color-allow: immersive media visual contract
            ambient: Qt.hsla((0.60 + jitter * 0.80 + 1.0) % 1.0, 0.62, 0.48, 1.0) // theme-color-allow: immersive media visual contract
        }
    }

    function publishVisualTiming() {
        AudioVisualFeatureController.setWaveformTiming(
                    trackId,
                    Number(layers._bpm) || 0,
                    Math.max(0, Number(layers._durationMs) || durationMs || 0),
                    layers.mix || [])
    }

    function currentRow() {
        return LibraryModel.indexForTrackId(PlaybackController.currentTrackId)
    }

    function currentPath() {
        var row = currentRow()
        return row < 0 ? "" : LibraryModel.data(
                    LibraryModel.index(row, 0), LibraryModel.PathRole)
    }

    function loadWaveform() {
        var row = currentRow()
        var path = currentPath()
        var neighbors = []
        if (row > 0)
            neighbors.push(LibraryModel.data(
                               LibraryModel.index(row - 1, 0),
                               LibraryModel.PathRole))
        if (row >= 0 && row + 1 < LibraryModel.count)
            neighbors.push(LibraryModel.data(
                               LibraryModel.index(row + 1, 0),
                               LibraryModel.PathRole))
        layers = ({})
        durationMs = 0
        publishVisualTiming()
        if (!active)
            return
        if (!path || path.length === 0)
            return
        generation = WaveformProvider.loadForTrack(
                    PlaybackController.currentTrackId, path, true)
        WaveformProvider.prefetchTracks(neighbors)
    }

    Component.onCompleted: loadWaveform()

    property Connections playbackConnection: Connections {
        target: PlaybackController
        function onCurrentTrackIdChanged() { root.loadWaveform() }
    }

    property Connections libraryConnection: Connections {
        target: LibraryModel
        function onDataChanged() { ++root.libraryRevision }
        function onModelReset() { ++root.libraryRevision; root.loadWaveform() }
    }

    property Connections waveformConnection: Connections {
        target: WaveformProvider
        function onWaveformReady(path, resultLayers) {
            var responseTrack = String(resultLayers._trackId || "")
            var responseGeneration = Number(resultLayers._generation || 0)
            var sameTrack = responseTrack.length === 0
                    || responseTrack === String(PlaybackController.currentTrackId)
            var sameGeneration = responseGeneration === 0
                    || responseGeneration === Number(WaveformProvider.activeGeneration)
            if (sameTrack && sameGeneration && path === root.currentPath()) {
                root.layers = resultLayers
                root.durationMs = Math.max(
                            0, Number(resultLayers._durationMs) || 0)
                root.publishVisualTiming()
            }
        }
    }

    property Connections settingsConnection: Connections {
        target: SettingsController
        function onWaveformPeakAlgorithmChanged() { root.loadWaveform() }
    }

    onActiveChanged: {
        if (active) {
            loadWaveform()
        } else {
            frequencyReady = false
        }
    }

}
