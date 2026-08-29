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
        property var layers: ({ mix: [0.2, 0.6, 0.4], _durationMs: 120000, _bpm: 120 })
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

    function test_shared_view_uses_one_waveform_and_stable_track_colors() {
        compare(sharedWaveform.trackColorized, true)
        compare(waveformItemCount(sharedWaveform), 1)
        var first = sharedWaveform.colorForTrack("track-a", 0)
        compare(first, sharedWaveform.colorForTrack("track-a", 0))
        verify(first !== sharedWaveform.colorForTrack("track-b", 0))

        var item = findChild(sharedWaveform, "immersiveWaveform")
        verify(item)
        compare(item.layers, waveformSession.layers)
        compare(item.duration, waveformSession.durationMs)
        verify(item.baseColor !== undefined)
    }

    function test_track_change_smoothly_transitions_waveform_color() {
        var item = findChild(sharedWaveform, "immersiveWaveform")
        verify(item)
        waveformSession.trackId = "track-a"
        wait(550)
        var target = sharedWaveform.colorForTrack("track-b", 0)
        waveformSession.trackId = "track-b"
        verify(String(item.baseColor) !== String(target))
        wait(550)
        compare(String(item.baseColor), String(target))
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
