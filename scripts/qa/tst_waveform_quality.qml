import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer 1.0

// Runs in the existing qml_waveform_test host. Input configuration sits next to
// its -o text log, so no production QA hooks or additional binary are required.
TestCase {
    id: test
    name: "WaveformQualityCapture"
    when: windowShown
    visible: true
    width: 1200
    height: 340
    property var config
    property var receivedLayers
    property double loadStarted: 0
    property int firstVisibleMs: -1
    property double lastTick: 0
    property int maxTickGapMs: 0
    property int timerSamples: 0

    // Windows STARTUPINFO hides the first ShowWindow call when the helper is
    // launched with -WindowStyle Hidden. Expose only this test's render surface
    // after startup; otherwise QuickTest's windowShown condition never fires.
    Timer {
        interval: 100
        running: true
        onTriggered: {
            const hostWindow = test.Window.window
            if (hostWindow) {
                hostWindow.hide()
                hostWindow.show()
            }
        }
    }

    Rectangle {
        id: canvas
        anchors.fill: parent
        color: "#141619"
        Text { x: 20; y: 8; color: "#c8cbd0"; font.family: "Segoe UI"; text: "Full track - fixed cursor 50%" }
        WaveformItem {
            id: overview
            x: 20; y: 38; width: 1160; height: 120
            visualMode: 3
            lowColor: "#ff0000"; midColor: "#00ff00"; highColor: "#0000ff"
            frequencyUnplayedOpacity: 0.7
            pointerInteractionEnabled: false
        }
        Text { x: 20; y: 170; color: "#c8cbd0"; font.family: "Segoe UI"; text: "Middle 4 seconds - same source and cursor" }
        WaveformItem {
            id: detail
            x: 20; y: 202; width: 1160; height: 120
            visualMode: 3
            lowColor: "#ff0000"; midColor: "#00ff00"; highColor: "#0000ff"
            frequencyUnplayedOpacity: 0.7
            pointerInteractionEnabled: false
        }
    }
    Timer {
        id: heartbeat
        interval: 16; repeat: true
        onTriggered: {
            const now = Date.now()
            if (test.lastTick) test.maxTickGapMs = Math.max(test.maxTickGapMs, now - test.lastTick)
            test.lastTick = now
            test.timerSamples++
        }
    }
    Connections {
        target: WaveformProvider
        function onWaveformReady(path, layers) {
            if (path === test.config.audioPath) {
                test.receivedLayers = layers
                if (test.firstVisibleMs < 0 && layers.mix && layers.mix.some(function(value) { return value > 0 }))
                    test.firstVisibleMs = Date.now() - test.loadStarted
            }
        }
    }

    function test_capture() {
        const args = Qt.application.arguments
        const outputArg = args[args.indexOf("-o") + 1]
        const outputFile = outputArg.substring(0, outputArg.lastIndexOf(","))
        const outputDir = outputFile.substring(0, outputFile.lastIndexOf("/"))
        const request = new XMLHttpRequest()
        request.open("GET", "file:///" + outputDir + "/config.json", false)
        request.send()
        config = JSON.parse(request.responseText)
        SettingsController.cacheDirectory = config.cachePath
        SettingsController.waveformPeakAlgorithm = 0
        lastTick = Date.now()
        heartbeat.start()
        const started = Date.now()
        loadStarted = started
        WaveformProvider.loadForTrack("quality-capture", config.audioPath, true)
        tryVerify(function() {
            return receivedLayers !== undefined && receivedLayers._frequencyReady === true
                    && receivedLayers._complete !== false
        }, 120000)
        const readyMs = Date.now() - started
        verify(receivedLayers.mix.length > 0)
        verify(receivedLayers.bass.length > 0)
        const duration = receivedLayers._durationMs
        verify(duration > 0)
        overview.layers = receivedLayers
        detail.layers = receivedLayers
        overview.duration = duration
        detail.duration = duration
        overview.position = duration / 2
        detail.position = duration / 2
        overview.cursorPosition = duration / 2
        detail.cursorPosition = duration / 2
        detail.setVisibleRange(Math.max(0, duration / 2 - 2000), Math.min(duration, duration / 2 + 2000))
        // The same scenegraph frame renders both items; waiting for a second
        // item frame would spuriously time out once that frame is already done.
        waitForRendering(canvas)
        const initialRenderMs = Date.now() - started
        heartbeat.stop()
        wait(100)
        grabImage(canvas).save(config.imagePath)
        console.log("WAVEFORM_QA_METRICS " + JSON.stringify({
            providerReadyMs: readyMs,
            firstVisibleMs: firstVisibleMs,
            fullReadyMs: readyMs,
            initialRenderMs: initialRenderMs,
            guiTimerScope: "load through initial layer assignment and render; baseline measured load only",
            maxGuiTimerGapMs: maxTickGapMs,
            guiTimerSamples: timerSamples,
            durationMs: duration,
            peakCount: receivedLayers.mix.length,
            frequencyPeakCount: receivedLayers.bass.length,
            layerKeys: Object.keys(receivedLayers),
            cursorMs: duration / 2,
            detailStartMs: detail.visibleStartMs,
            detailEndMs: detail.visibleEndMs,
            width: 1200, height: 340
        }))
    }
}
