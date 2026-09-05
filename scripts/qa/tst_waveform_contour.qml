import QtQuick
import QtQuick.Window
import QtTest
import AgPlayer 1.0

// The existing qml_waveform_test host supplies the real provider and renderer.
// config.json follows capture-waveform-quality.ps1's input convention.
TestCase {
    id: test
    name: "WaveformContourParity"
    when: windowShown
    visible: true
    width: 1200
    height: 480
    property var config
    property var receivedLayers
    property var modes: [0, 1, 3]

    Timer {
        interval: 100
        running: true
        onTriggered: {
            const host = test.Window.window
            if (host) { host.hide(); host.show() }
        }
    }
    Rectangle {
        id: canvas
        anchors.fill: parent
        color: "#141619"
        Repeater {
            id: rows
            model: test.modes
            delegate: Item {
                required property int modelData
                required property int index
                x: 20
                y: index * 160
                width: 1160
                height: 160
                property alias waveform: waveform
                Text {
                    y: 10
                    color: "#c8cbd0"
                    font.family: "Segoe UI"
                    text: "Mode " + parent.modelData + " - identical mix / viewport / density / scale"
                }
                WaveformItem {
                    id: waveform
                    y: 36
                    width: 1160
                    height: 120
                    visualMode: parent.modelData
                    density: 1
                    lineWidth: 2
                    amplitudeScale: 1
                    waveformColor: "#e6e6e6"
                    baseColor: "#e6e6e6"
                    progressColor: "#e6e6e6"
                    gradientStartColor: "#00d4ff"
                    gradientMiddleColor: "#9c64ed"
                    gradientEndColor: "#ef579f"
                    lowColor: "#ff0000"
                    midColor: "#00ff00"
                    highColor: "#0000ff"
                    frequencyUnplayedOpacity: 1
                    pointerInteractionEnabled: false
                }
            }
        }
    }
    Connections {
        target: WaveformProvider
        function onWaveformReady(path, layers) {
            if (test.config && path === test.config.audioPath)
                test.receivedLayers = layers
        }
    }

    function initTestCase() {
        failOnWarning(/.?/)
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
        WaveformProvider.loadForTrack("contour-parity", config.audioPath, true)
        tryVerify(function() {
            return receivedLayers && receivedLayers._frequencyReady
                    && receivedLayers._complete !== false
        }, 120000)
        verify(receivedLayers.mix.length > 0)
        compare(rows.count, 3)
    }

    function test_contour_data() {
        return [
            { tag: "full", start: 0, end: 24000 },
            { tag: "detail", start: 10000, end: 14000 }
        ]
    }

    function test_contour(data) {
        for (let row = 0; row < 3; ++row) {
            const waveform = rows.itemAt(row).waveform
            waveform.layers = receivedLayers
            waveform.duration = receivedLayers._durationMs
            waveform.position = 12000
            waveform.cursorPosition = 12000
            waveform.setVisibleRange(data.start, data.end)
        }
        waitForRendering(canvas)
        wait(100)
        const capture = grabImage(canvas)
        const imagePath = config.imagePath.replace(/\.png$/, "-" + data.tag + ".png")
        capture.save(imagePath)
        compare(capture.width, 1200)
        compare(capture.height, 480)

        let mismatchedColumns = 0
        let maxEdgeDelta = 0
        let visibleColumns = 0
        let mismatchedOccupancyPixels = 0
        let firstMismatch = ""
        // Use the uniform canvas color as the exact RGB background. Text sits
        // outside these row bounds. Compare every raster column, including gaps.
        for (let x = 0; x < 1160; ++x) {
            const edges = []
            const masks = []
            for (let row = 0; row < 3; ++row) {
                let top = -1
                let bottom = -1
                const mask = []
                for (let y = 0; y < 120; ++y) {
                    const imageY = row * 160 + 36 + y
                    const occupied = capture.red(x + 20, imageY) !== 20
                            || capture.green(x + 20, imageY) !== 22
                            || capture.blue(x + 20, imageY) !== 25
                    mask.push(occupied)
                    if (occupied) {
                        if (top < 0) top = y
                        bottom = y
                    }
                }
                edges.push({ top: top, bottom: bottom })
                masks.push(mask)
            }
            if (edges[0].top >= 0) visibleColumns++
            for (let row = 1; row < 3; ++row) {
                if (edges[row].top !== edges[0].top || edges[row].bottom !== edges[0].bottom) {
                    mismatchedColumns++
                    maxEdgeDelta = Math.max(maxEdgeDelta,
                        Math.abs(edges[row].top - edges[0].top),
                        Math.abs(edges[row].bottom - edges[0].bottom))
                    if (!firstMismatch) firstMismatch = "x=" + x + " mode=" + modes[row]
                            + " reference=" + JSON.stringify(edges[0]) + " actual=" + JSON.stringify(edges[row])
                }
                for (let y = 0; y < 120; ++y) {
                    if (masks[row][y] !== masks[0][y]) mismatchedOccupancyPixels++
                }
            }
        }
        console.log("WAVEFORM_CONTOUR_METRICS " + JSON.stringify({
            viewport: data.tag, startMs: data.start, endMs: data.end,
            modes: modes, width: 1160, height: 120, density: 1, lineWidth: 2,
            visibleColumns: visibleColumns, columnComparisons: 2320,
            mismatchedColumns: mismatchedColumns, maxEdgeDelta: maxEdgeDelta,
            mismatchedOccupancyPixels: mismatchedOccupancyPixels,
            imagePath: imagePath, firstMismatch: firstMismatch
        }))
        verify(visibleColumns > 100, "Empty or invalid render cannot establish contour parity")
        compare(mismatchedColumns, 0, firstMismatch)
        compare(mismatchedOccupancyPixels, 0, "Waveform texture/occupied pixels must also match")
    }
}
