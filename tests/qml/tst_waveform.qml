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

    function test_z_cancelled_drag_does_not_emit_stale_seek() {
        mousePress(waveform, 100, 20)
        waveform.enabled = false
        wait(0)
        mouseRelease(waveform, 600, 20)
        compare(seekSpy.count, 0)
    }
}
