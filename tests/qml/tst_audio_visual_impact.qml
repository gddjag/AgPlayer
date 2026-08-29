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
    }

    SharedWaveformView {
        id: sharedWaveform
        width: parent.width
        height: 72
        waveformSession: waveformSession
    }

    WaveformSession {
        id: paletteSession
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

    function test_shared_view_uses_one_frequency_color_waveform() {
        compare(waveformItemCount(sharedWaveform), 1)

        var item = findChild(sharedWaveform, "immersiveWaveform")
        verify(item)
        compare(item.layers, waveformSession.layers)
        compare(item.duration, waveformSession.durationMs)
        compare(item.visualMode, 3)
        compare(String(item.frequencyLowColor),
                SettingsController.waveformFrequencyLowColor)
        compare(String(item.frequencyMidColor),
                SettingsController.waveformFrequencyMidColor)
        compare(String(item.frequencyHighColor),
                SettingsController.waveformFrequencyHighColor)
    }

    function test_track_change_does_not_randomize_frequency_colors() {
        var item = findChild(sharedWaveform, "immersiveWaveform")
        verify(item)
        var low = String(item.frequencyLowColor)
        var mid = String(item.frequencyMidColor)
        var high = String(item.frequencyHighColor)
        waveformSession.trackId = "track-a"
        waveformSession.trackId = "track-b"
        compare(String(item.frequencyLowColor), low)
        compare(String(item.frequencyMidColor), mid)
        compare(String(item.frequencyHighColor), high)
    }

    function test_waveform_session_exposes_reusable_stable_track_palette() {
        var first = paletteSession.paletteForTrack("track-a")
        var same = paletteSession.paletteForTrack("track-a")
        var other = paletteSession.paletteForTrack("track-b")
        compare(first.hash, same.hash)
        compare(String(first.cool), String(same.cool))
        compare(String(first.warm), String(same.warm))
        verify(first.hash !== other.hash)
        verify(String(first.cool) !== String(other.cool))
    }
}
