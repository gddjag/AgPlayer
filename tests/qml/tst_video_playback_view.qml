import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "VideoPlaybackView"
    when: windowShown
    visible: true
    width: 960
    height: 540

    property var playback: null
    property var videoState: null
    property var view: null

    Component {
        id: playbackComponent
        QtObject {
            enum PlaybackState { Stopped, Playing }
            property int state: 1
            property int positionMs: 18000
            property int durationMs: 125000
            property real speedRatio: 1.0
            property bool muted: false
            property real volume: 0.32
            property int previousCalls: 0
            property int toggleCalls: 0
            property int nextCalls: 0
            property int seekCalls: 0
            property int speedCalls: 0
            property int toggleMutedCalls: 0
            property int setVolumeCalls: 0
            property int lastSeekMs: -1
            property real lastSpeedRatio: 0
            property real lastVolume: -1
            function previous() { ++previousCalls }
            function togglePlayback() { ++toggleCalls }
            function next() { ++nextCalls }
            function seek(value) { ++seekCalls; lastSeekMs = Math.round(value) }
            function toggleMuted() {
                ++toggleMutedCalls
                muted = !muted
            }
            function setVolume(value) {
                ++setVolumeCalls
                lastVolume = value
                volume = value
            }
            function setSpeedRatio(value) {
                ++speedCalls
                lastSpeedRatio = value
                speedRatio = value
            }
        }
    }

    Component {
        id: videoStateComponent
        QtObject {
            property bool visible: true
            property bool loading: false
            property string errorMessage: ""
        }
    }

    Component {
        id: viewComponent
        VideoPlaybackView {
            width: 960
            height: 540
        }
    }

    Component {
        id: defaultVolumeControlComponent
        PlayerVolumeControl {
            expandedForQa: true
        }
    }

    function init() {
        failOnWarning(/.?/)
        playback = playbackComponent.createObject(testCase)
        videoState = videoStateComponent.createObject(testCase)
        view = viewComponent.createObject(testCase, {
            "playback": playback,
            "videoPlayback": videoState
        })
        verify(playback && videoState && view)
    }

    function cleanup() {
        if (view)
            view.destroy()
        if (videoState)
            videoState.destroy()
        if (playback)
            playback.destroy()
        view = null
        videoState = null
        playback = null
    }

    function test_surface_exposes_only_basic_video_controls() {
        var surface = findChild(view, "videoSurface")
        var frame = findChild(view, "videoFrameItem")
        var bar = findChild(view, "videoTransportBar")
        verify(surface && frame && bar)
        compare(surface.color.toString(), "#000000")

        var expectedControls = [
            "previousButton", "playPauseButton", "nextButton",
            "videoSeekSlider", "videoCurrentTimeLabel", "videoTotalTimeLabel",
            "mainVolumeControl", "muteButton", "volumeSlider",
            "videoSpeedControl", "videoFullscreenButton", "videoReturnButton"
        ]
        for (var index = 0; index < expectedControls.length; ++index)
            verify(findChild(bar, expectedControls[index]),
                   "missing basic control " + expectedControls[index])

        compare(findChild(bar, "equalizerButton").visible, false)
        var waveformMode = findChild(bar, "waveformModeButton")
        var playbackMode = findChild(bar, "modeButton")
        verify(!waveformMode || !waveformMode.visible)
        verify(!playbackMode || !playbackMode.visible)
        var forbidden = [
            "videoNavigation", "videoLibrary", "videoQuality",
            "videoFilter", "videoSubtitle", "videoUrl", "openVideoButton"
        ]
        for (var forbiddenIndex = 0; forbiddenIndex < forbidden.length;
             ++forbiddenIndex)
            compare(findChild(view, forbidden[forbiddenIndex]), null)
    }

    function test_small_and_large_layouts_keep_actions_accessible() {
        var bar = findChild(view, "videoTransportBar")
        var actionNames = [
            "previousButton", "playPauseButton", "nextButton",
            "videoSeekSlider", "muteButton", "volumeSlider",
            "videoSpeedControl",
            "videoFullscreenButton", "videoReturnButton"
        ]
        for (var widthIndex = 0; widthIndex < 2; ++widthIndex) {
            view.width = widthIndex === 0 ? 612 : 1180
            wait(0)
            compare(bar.width, view.width)
            var volume = findChild(bar, "mainVolumeControl")
            var volumeSlider = findChild(bar, "volumeSlider")
            volume.expandedForQa = true
            tryVerify(function() { return volumeSlider.width >= 48 }, 1000,
                      "volume must remain adjustable at width " + view.width)
            for (var actionIndex = 0; actionIndex < actionNames.length;
                 ++actionIndex) {
                var action = findChild(bar, actionNames[actionIndex])
                verify(action && action.visible,
                       actionNames[actionIndex] + " must remain visible")
                verify(action.focusPolicy !== Qt.NoFocus,
                       actionNames[actionIndex] + " must accept keyboard focus")
                verify(action.Accessible.name.length > 0,
                       actionNames[actionIndex] + " needs an accessible name")
                var point = action.mapToItem(bar, 0, 0)
                verify(point.x >= -1 && point.x + action.width <= bar.width + 1,
                       actionNames[actionIndex] + " must fit width " + view.width)
                if (actionNames[actionIndex] === "videoSpeedControl")
                    verify(!action.contentItem.truncated,
                           "speed label must not be elided at width " + view.width
                           + " (control=" + action.width
                           + ", content=" + action.contentItem.width
                           + ", painted=" + action.contentItem.paintedWidth + ")")
            }
            volume.expandedForQa = false
        }
    }

    function test_shared_transport_dispatches_to_the_existing_playback_authority() {
        findChild(view, "previousButton").clicked()
        findChild(view, "playPauseButton").clicked()
        findChild(view, "nextButton").clicked()
        compare(playback.previousCalls, 1)
        compare(playback.toggleCalls, 1)
        compare(playback.nextCalls, 1)

        var seek = findChild(view, "videoSeekSlider")
        seek.value = 42000
        seek.moved()
        compare(playback.seekCalls, 1)
        compare(playback.lastSeekMs, 42000)

        var speed = findChild(view, "videoSpeedControl")
        speed.activated(3)
        compare(playback.speedCalls, 1)
        compare(playback.lastSpeedRatio, 1.5)
    }

    function test_shared_volume_dispatches_to_the_injected_playback_authority() {
        var originalMuted = PlaybackController.muted
        var originalVolume = PlaybackController.volume
        try {
            var mute = findChild(view, "muteButton")
            var volume = findChild(view, "volumeSlider")
            mute.clicked()
            volume.value = 0.61
            volume.moved()

            compare(PlaybackController.muted, originalMuted)
            fuzzyCompare(PlaybackController.volume, originalVolume, 0.001)
            compare(playback.toggleMutedCalls, 1)
            compare(playback.muted, true)
            compare(playback.setVolumeCalls, 1)
            compare(playback.lastVolume, 0.61)
        } finally {
            if (PlaybackController.muted !== originalMuted)
                PlaybackController.toggleMuted()
            PlaybackController.setVolume(originalVolume)
        }
    }

    function test_default_audio_volume_control_keeps_global_bindings() {
        var control = defaultVolumeControlComponent.createObject(testCase)
        verify(control)
        compare(control.playback, PlaybackController)
        compare(findChild(control, "muteButton").Accessible.name,
                PlaybackController.muted ? qsTr("取消静音") : qsTr("静音"))
        fuzzyCompare(findChild(control, "volumeSlider").value,
                     PlaybackController.muted ? 0 : PlaybackController.volume,
                     0.001)
        control.destroy()
    }

    function test_loading_and_error_share_the_same_minimal_surface() {
        var status = findChild(view, "videoStatusText")
        var surface = findChild(view, "videoSurface")
        verify(status && surface)
        compare(view.videoPlayback, videoState)

        videoState.loading = true
        compare(videoState.loading, true)
        compare(view.videoPlayback.loading, true)
        compare(view.visible, true)
        compare(surface.visible, true)
        tryCompare(status, "visible", true)
        compare(status.parent, surface)
        compare(status.text, qsTr("正在加载视频画面…"))

        videoState.errorMessage = qsTr("无法解码视频画面")
        tryCompare(status, "text", qsTr("无法解码视频画面"))
        compare(status.parent, surface)

        videoState.loading = false
        videoState.errorMessage = ""
        tryCompare(status, "visible", false)
    }

    function test_status_state_and_frame_controller_sources_are_explicit() {
        var frame = findChild(view, "videoFrameItem")
        compare(view.videoPlayback, videoState,
                "the injectable object is the loading/error state source")
        compare(view.frameController, VideoPlaybackController,
                "production frames keep the typed controller authority")
        compare(frame.controller, view.frameController)
    }

    function test_fullscreen_and_return_are_forwarded_as_view_requests() {
        var fullscreenSpy = signalSpyComponent.createObject(testCase, {
            "target": view,
            "signalName": "fullscreenRequested"
        })
        var returnSpy = signalSpyComponent.createObject(testCase, {
            "target": view,
            "signalName": "returnRequested"
        })
        verify(fullscreenSpy.valid && returnSpy.valid)

        findChild(view, "videoFullscreenButton").clicked()
        findChild(view, "videoReturnButton").clicked()
        compare(fullscreenSpy.count, 1)
        compare(returnSpy.count, 1)
        fullscreenSpy.destroy()
        returnSpy.destroy()
    }

    Component {
        id: signalSpyComponent
        SignalSpy {}
    }
}
