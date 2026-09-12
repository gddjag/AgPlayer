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

        // A BPM grid supplies timing, not audible energy. Feed spectrum just as
        // the live playback controller does before expecting a visible impact.
        var spectrum = []
        for (var bin = 0; bin < 128; ++bin) spectrum.push(0.4)
        spectrumTestDriver.feedSpectrum(spectrum)

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
        for (var silentBin = 0; silentBin < 128; ++silentBin) spectrum[silentBin] = 0
        spectrumTestDriver.feedSpectrum(spectrum)
        AudioVisualFeatureController.processPlaybackPosition(11999)
        AudioVisualFeatureController.processPlaybackPosition(12000)
        compare(AudioVisualFeatureController.impactRevision, startRevision + 3)
        compare(AudioVisualFeatureController.impactStrength, 0)
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

    function test_shared_view_uses_one_three_band_frequency_waveform() {
        compare(waveformItemCount(sharedWaveform), 1)

        var item = findChild(sharedWaveform, "immersiveWaveform")
        var frequencySettings = SettingsController.frequencyColorWaveform
        verify(item)
        compare(item.layers, waveformSession.layers)
        compare(item.duration, waveformSession.durationMs)
        compare(item.visualMode, 3)
        compare(String(item.lowColor), String(frequencySettings.lowColor))
        compare(String(item.midColor), String(frequencySettings.midColor))
        compare(String(item.highColor), String(frequencySettings.highColor))
        compare(item.frequencyUnplayedOpacity,
                frequencySettings.unplayedOpacity)
        compare(findChild(sharedWaveform,
                          "immersiveWaveformPlaybackGuide"), null)
        compare(findChild(sharedWaveform,
                          "immersiveWaveformPlaybackFocusDot"), null)
    }

    function test_track_change_does_not_randomize_frequency_colors() {
        var item = findChild(sharedWaveform, "immersiveWaveform")
        verify(item)
        var low = String(item.lowColor)
        var high = String(item.highColor)
        waveformSession.trackId = "track-a"
        waveformSession.trackId = "track-b"
        compare(String(item.lowColor), low)
        compare(String(item.highColor), high)
    }

}
