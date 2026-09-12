import QtQuick
import AgPlayer as Runtime

QtObject {
    id: root
    objectName: "sharedWaveformSession"

    property var layers: ({})
    property real durationMs: 0
    property real generation: 0
    property bool active: true
    property bool frequencyReady: false
    property int libraryRevision: 0
    readonly property string trackId:
        String(Runtime.PlaybackController.currentTrackId || "")
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
        // Partial display snapshots are not new beat-analysis results. Publishing
        // them would reset spectrum/transient history on every preview update.
        if (layers._complete === false)
            return
        Runtime.PlaybackController.applyBeatGridWaveform(
                    trackId,
                    Number(layers._bpm) || 0,
                    Math.max(0, Number(layers._durationMs) || durationMs || 0),
                    layers.mix || [])
        Runtime.AudioVisualFeatureController.setWaveformTiming(
                    trackId,
                    Number(layers._bpm) || 0,
                    Math.max(0, Number(layers._durationMs) || durationMs || 0),
                    layers.mix || [])
    }

    function currentRow() {
        return Runtime.LibraryModel.indexForTrackId(
                    Runtime.PlaybackController.currentTrackId)
    }

    function currentPath() {
        var row = currentRow()
        return row < 0 ? "" : Runtime.LibraryModel.data(
                    Runtime.LibraryModel.index(row, 0), Runtime.LibraryModel.PathRole)
    }

    function loadWaveform() {
        var row = currentRow()
        var path = currentPath()
        var neighbors = []
        if (row > 0)
            neighbors.push(Runtime.LibraryModel.data(
                               Runtime.LibraryModel.index(row - 1, 0),
                               Runtime.LibraryModel.PathRole))
        if (row >= 0 && row + 1 < Runtime.LibraryModel.count)
            neighbors.push(Runtime.LibraryModel.data(
                               Runtime.LibraryModel.index(row + 1, 0),
                               Runtime.LibraryModel.PathRole))
        layers = ({})
        durationMs = 0
        publishVisualTiming()
        if (!active)
            return
        if (!path || path.length === 0)
            return
        generation = Runtime.WaveformProvider.loadForTrack(
                    Runtime.PlaybackController.currentTrackId, path, true)
        Runtime.WaveformProvider.prefetchTracks(neighbors)
    }

    Component.onCompleted: {
        Runtime.PlaybackController.setBeatGridAutoPositionEnabled(
                    Runtime.SettingsController.playerShellMode === 2)
        loadWaveform()
    }

    property Connections playbackConnection: Connections {
        target: Runtime.PlaybackController
        function onCurrentTrackIdChanged() { root.loadWaveform() }
    }

    property Connections libraryConnection: Connections {
        target: Runtime.LibraryModel
        function onDataChanged() { ++root.libraryRevision }
        function onModelReset() { ++root.libraryRevision; root.loadWaveform() }
    }

    property Connections waveformConnection: Connections {
        target: Runtime.WaveformProvider
        function onActiveGenerationChanged() {
            root.frequencyReady = false
        }
        function onWaveformReady(path, resultLayers) {
            var responseTrack = String(resultLayers._trackId || "")
            var responseGeneration = Number(resultLayers._generation || 0)
            var sameTrack = responseTrack.length === 0
                    || responseTrack === String(Runtime.PlaybackController.currentTrackId)
            var sameGeneration = responseGeneration === 0
                    || responseGeneration === Number(Runtime.WaveformProvider.activeGeneration)
            if (root.active && sameTrack && sameGeneration
                    && path === root.currentPath()) {
                root.layers = resultLayers
                root.frequencyReady = Boolean(resultLayers._frequencyReady)
                root.durationMs = Math.max(
                            0, Number(resultLayers._durationMs) || 0)
                root.publishVisualTiming()
            }
        }
    }

    property Connections settingsConnection: Connections {
        target: Runtime.SettingsController
        function onPlayerShellModeChanged() {
            Runtime.PlaybackController.setBeatGridAutoPositionEnabled(
                        Runtime.SettingsController.playerShellMode === 2)
        }
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
