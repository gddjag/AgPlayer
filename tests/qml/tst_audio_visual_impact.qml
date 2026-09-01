import QtQuick
import QtTest
import AgPlayer 1.0
import "../../app/qml/AgPlayer/components"

TestCase {
    id: testCase
    name: "AudioVisualImpact"
    when: windowShown
    visible: true
    width: 800
    height: 120

    QtObject {
        id: waveformSession
        property var layers: ({
            mix: [0.2, 0.6, 0.4],
            bass: [0.8, 0.2, 0.1],
            mid: [0.1, 0.8, 0.2],
            high: [0.1, 0.2, 0.8],
            _durationMs: 120000,
            _bpm: 120
        })
        property real durationMs: 120000
        property string trackId: "track-a"
        property bool frequencyReady: false
    }

    SharedWaveformView {
        id: sharedWaveform
        width: parent.width
        height: 72
        waveformSession: waveformSession
    }

    function waveformItemCount(item) {
        var count = String(item).indexOf("WaveformItem") >= 0 ? 1 : 0
        for (var index = 0; index < item.children.length; ++index)
            count += waveformItemCount(item.children[index])
        return count
    }

    function init() {
        AudioVisualFeatureController.setActive(false)
        AudioVisualFeatureController.setWaveformTiming("", 0, 0, [])
        AudioVisualFeatureController.setActive(true)
    }

    function cleanup() {
        AudioVisualFeatureController.setActive(false)
        AudioVisualFeatureController.setWaveformTiming("", 0, 0, [])
    }

    function test_reliable_120_bpm_emits_once_every_eight_beats() {
        var startRevision = AudioVisualFeatureController.impactRevision
        AudioVisualFeatureController.setWaveformTiming(
                    "track-a", 120, 120000, [0.2, 0.6, 0.4])
        compare(AudioVisualFeatureController.beatReliable, true)

        AudioVisualFeatureController.processPlaybackPosition(0)
        AudioVisualFeatureController.processPlaybackPosition(3999)
        compare(AudioVisualFeatureController.impactRevision, startRevision)
        AudioVisualFeatureController.processPlaybackPosition(4000)
        compare(AudioVisualFeatureController.impactRevision, startRevision + 1)
        AudioVisualFeatureController.processPlaybackPosition(4017)
        compare(AudioVisualFeatureController.impactRevision, startRevision + 1)
        AudioVisualFeatureController.processPlaybackPosition(7999)
        AudioVisualFeatureController.processPlaybackPosition(8000)
        compare(AudioVisualFeatureController.impactRevision, startRevision + 2)
        verify(AudioVisualFeatureController.impactStrength > 0)
    }

    function test_seek_and_track_change_do_not_emit_duplicate_impacts() {
        AudioVisualFeatureController.setWaveformTiming(
                    "track-a", 120, 120000, [0.2, 0.6, 0.4])
        AudioVisualFeatureController.processPlaybackPosition(0)
        AudioVisualFeatureController.processPlaybackPosition(3999)
        AudioVisualFeatureController.processPlaybackPosition(4000)
        var revision = AudioVisualFeatureController.impactRevision

        AudioVisualFeatureController.processPlaybackPosition(20000)
        compare(AudioVisualFeatureController.impactRevision, revision)
        AudioVisualFeatureController.processPlaybackPosition(4000)
        compare(AudioVisualFeatureController.impactRevision, revision)

        AudioVisualFeatureController.setWaveformTiming(
                    "track-b", 120, 120000, [0.4, 0.3])
        AudioVisualFeatureController.processPlaybackPosition(4000)
        compare(AudioVisualFeatureController.impactRevision, revision)
    }

    function test_shared_view_uses_one_spectral_centroid_waveform() {
        compare(waveformItemCount(sharedWaveform), 1)

        var item = findChild(sharedWaveform, "immersiveWaveform")
        var frequencySettings = SettingsController.frequencyColorWaveform
        verify(item)
        compare(item.layers, waveformSession.layers)
        compare(item.duration, waveformSession.durationMs)
        compare(item.visualMode, 3)
        compare(item.spectralPalette.length, 8)
        compare(String(item.spectralPalette[0]),
                String(frequencySettings.palette[0]))
        compare(String(item.spectralPalette[7]),
                String(frequencySettings.palette[7]))
        compare(item.spectralUnplayedOpacity,
                frequencySettings.unplayedOpacity)
        compare(findChild(sharedWaveform,
                          "immersiveWaveformPlaybackGuide"), null)
        compare(findChild(sharedWaveform,
                          "immersiveWaveformPlaybackFocusDot"), null)
    }

    function test_track_change_does_not_randomize_spectral_palette() {
        var item = findChild(sharedWaveform, "immersiveWaveform")
        verify(item)
        var first = String(item.spectralPalette[0])
        var last = String(item.spectralPalette[7])
        waveformSession.trackId = "track-a"
        waveformSession.trackId = "track-b"
        compare(String(item.spectralPalette[0]), first)
        compare(String(item.spectralPalette[7]), last)
    }

}
