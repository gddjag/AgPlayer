import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "MiniPlayerState"
    when: windowShown

    property var mainPlayer: null
    property var miniPlayer: null

    // Fake playback controller that mirrors the production PlaybackController API
    // surface (properties + Q_INVOKABLE functions) so MiniPlayerControls bindings
    // resolve without touching real audio. Only positionMs, durationMs, playing
    // and pauseCalls are assertion-relevant; the rest exist to keep bindings
    // warning-free under test.
    QtObject {
        id: playbackFake
        property int positionMs: 0
        property int durationMs: 0
        property int pauseCalls: 0
        property bool playing: true

        // Mirror of PlaybackController API consumed by MiniPlayerControls.
        property int state: PlaybackController.Playing
        property int mode: PlaybackController.Sequential
        property real volume: 1.0
        property bool muted: false
        property int trackIndex: -1
        property int trackCount: 0
        property string currentTrackId: ""
        property string errorMessage: ""

        function publishPlaying(position, duration) {
            positionMs = position
            durationMs = duration
            playing = true
            state = PlaybackController.Playing
        }
        function togglePlayback() {
            if (playing) pauseCalls += 1
            playing = !playing
            state = playing ? PlaybackController.Playing : PlaybackController.Paused
        }
        function play() {}
        function pause() {}
        function seek(position) {}
        function next() {}
        function previous() {}
        function setVolume(v) { volume = v }
        function toggleMuted() { muted = !muted }
        function cycleMode() {}
        function playRow(row) {}
        function toggleFavorite() {}
        function toggleFavoriteRow(row) {}
    }

    // Fake WindowController recording real actions instead of mutating real windows.
    QtObject {
        id: windowController
        property bool alwaysOnTop: false
        property bool mainVisible: false
        property bool miniVisible: true
        function setAlwaysOnTop(value) { alwaysOnTop = value }
        function showMain() { mainVisible = true; miniVisible = false }
        function showMini() { miniVisible = true; mainVisible = false }
        function requestClose() {}
    }

    function initTestCase() {
        verify(typeof testMainWindow !== "undefined", "testMainWindow context property should exist")
        verify(typeof testMiniWindow !== "undefined", "testMiniWindow context property should exist")
        mainPlayer = testMainWindow
        miniPlayer = testMiniWindow
        verify(mainPlayer, "Failed to create Main window")
        verify(miniPlayer, "Failed to create Mini player window")

        // Override playback/windows so both windows share the same fake state
        // source without spinning up a real ag_player core or audio device.
        mainPlayer.playback = playbackFake
        miniPlayer.playback = playbackFake
        miniPlayer.windows = windowController
        miniPlayer.visible = true
        wait(50)
    }

    function cleanupTestCase() {
        mainPlayer = null
        miniPlayer = null
    }

    function test_mini_and_main_share_state() {
        playbackFake.publishPlaying(25000, 286000)
        compare(mainPlayer.positionMs, 25000, "main window should mirror shared playback position")
        compare(miniPlayer.positionMs, 25000, "mini window should mirror shared playback position")
        mouseClick(miniPlayer.playPauseButton)
        compare(playbackFake.pauseCalls, 1, "clicking mini playPause should call playback.togglePlayback once")
    }

    function test_pin_and_restore_are_real_actions() {
        mouseClick(miniPlayer.pinButton)
        compare(windowController.alwaysOnTop, true, "pin button should call windows.setAlwaysOnTop(true)")
        mouseClick(miniPlayer.restoreButton)
        compare(windowController.mainVisible, true, "restore button should call windows.showMain() -> mainVisible=true")
        compare(windowController.miniVisible, false, "restore button should call windows.showMain() -> miniVisible=false")
    }
}
