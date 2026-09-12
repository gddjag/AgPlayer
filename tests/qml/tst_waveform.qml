import QtQuick
import QtTest
import AgPlayer 1.0

TestCase {
    id: testCase
    name: "WaveformInteraction"
    when: windowShown
    visible: true
    width: 800
    height: 100

    function test_partialWaveformDoesNotResetVisualBeatTiming() {
        const component = Qt.createComponent(
                    "../../app/qml/AgPlayer/components/WaveformSession.qml")
        compare(component.status, Component.Ready, component.errorString())
        const session = component.createObject(testCase, { active: false })
        verify(session !== null)
        AudioVisualFeatureController.setWaveformTiming("playing", 120, 120000, [0.2, 0.6])
        compare(AudioVisualFeatureController.beatReliable, true)
        session.layers = { _complete: false, _durationMs: 120000, mix: [0.2, 0] }
        session.publishVisualTiming()
        compare(AudioVisualFeatureController.beatReliable, true)
        session.layers = { _complete: true, _durationMs: 120000, _bpm: 0, mix: [0.2, 0.3] }
        session.publishVisualTiming()
        compare(AudioVisualFeatureController.beatReliable, false)
        session.destroy()
    }

    WaveformItem {
        id: waveform
        width: 800
        height: 40
        duration: 100000
        peaks: [0.25, 0.5, 0.75, 1.0]
    }

    SignalSpy {
        id: seekSpy
        target: waveform
        signalName: "seekRequested"
    }

    function init() {
        seekSpy.clear()
        waveform.enabled = true
        waveform.visible = true
        waveform.duration = 100000
        waveform.position = 0
        waveform.visualMode = 0
        waveform.layers = ({ mix: [0.25, 0.5, 0.75, 1.0] })
    }

    function test_drag_emits_one_committed_seek() {
        mousePress(waveform, 100, 20)
        mouseMove(waveform, 600, 20)
        compare(waveform.hoverPosition, 75000)
        compare(seekSpy.count, 0)
        mouseRelease(waveform, 600, 20)
        compare(seekSpy.count, 1)
        compare(seekSpy.signalArguments[0][0], 75000)
    }

    function test_click_emits_one_seek() {
        verify(waveform.window !== null)
        verify(waveform.visible)
        compare(testCase.childAt(200, 20), waveform)
        mouseClick(waveform, 200, 20)
        compare(seekSpy.count, 1)
        compare(seekSpy.signalArguments[0][0], 25000)
    }

    function test_clamps_pointer_outside_and_zero_duration() {
        compare(waveform.timeForX(-20), 0)
        compare(waveform.timeForX(900), 100000)
        waveform.duration = 0
        compare(waveform.timeForX(400), 0)
    }

    function test_cursor_position_maps_to_continuous_pixel_progress() {
        waveform.position = 0
        waveform.cursorPosition = 100
        var firstPixel = waveform.waveformCursorX

        waveform.cursorPosition = 110
        verify(waveform.waveformCursorX > firstPixel,
               "cursor progress must advance within one peak bucket")
        verify(waveform.waveformCursorX < 1,
               "a 110 ms cursor must remain below the first pixel at this scale")
    }

    function test_z_cancelled_drag_does_not_emit_stale_seek() {
        mousePress(waveform, 100, 20)
        waveform.enabled = false
        wait(0)
        mouseRelease(waveform, 600, 20)
        compare(seekSpy.count, 0)
    }
}
