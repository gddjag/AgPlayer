import QtCore
import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "ImmersiveGlowGpu"
    when: windowShown
    visible: true
    width: 640
    height: 360

    property var glowEffect: null
    property int frozenFrameCount: 0
    readonly property string captureDirectory:
        StandardPaths.writableLocation(StandardPaths.TempLocation)

    function capturePath(label) {
        var directory = captureDirectory.toString()
        if (directory.indexOf("file:///") === 0) {
            directory = Qt.platform.os === "windows"
                    ? directory.substring(8) : directory.substring(7)
        } else if (directory.indexOf("file://") === 0) {
            directory = directory.substring(7)
        }
        return decodeURIComponent(directory) + "/agplayer-glow-"
                + label + ".png"
    }

    Rectangle {
        id: stage
        anchors.fill: parent
        color: "#03050a"

        TerrainReactorItem {
            id: terrain
            anchors.fill: parent
            active: true
            hostExposed: true
            quality: TerrainReactorItem.High
            deterministicSeed: 0x5eed
            useSyntheticFeatures: true

            Component.onCompleted: setSyntheticFeatures(
                                       [0.92, 0.88, 0.45, 0.42,
                                        0.62, 0.70, 0.56, 0.38],
                                       0.72, 0.64, false, false)
        }
    }

    Component {
        id: glowComponent
        ImmersiveColumnGlow { }
    }

    function isSupportedAcceleratedBackend() {
        var api = testCase.GraphicsInfo.api
        return api === GraphicsInfo.Direct3D11
                || api === GraphicsInfo.OpenGL
                || api === GraphicsInfo.Vulkan
                || api === GraphicsInfo.Metal
    }

    function settleAndGrab(label) {
        verify(waitForRendering(stage, 3000),
               "viewport did not render for " + label)
        var image = grabImage(stage)
        compare(image.width, 640)
        compare(image.height, 360)
        image.save(capturePath(label))
        return image
    }

    function differenceStats(baseline, candidate) {
        compare(candidate.width, baseline.width)
        compare(candidate.height, baseline.height)
        var increased = 0
        var darkened = 0
        var totalIncrease = 0
        for (var y = 0; y < baseline.height; ++y) {
            for (var x = 0; x < baseline.width; ++x) {
                var beforeRed = baseline.red(x, y)
                var beforeGreen = baseline.green(x, y)
                var beforeBlue = baseline.blue(x, y)
                var afterRed = candidate.red(x, y)
                var afterGreen = candidate.green(x, y)
                var afterBlue = candidate.blue(x, y)
                if (afterRed < beforeRed || afterGreen < beforeGreen
                        || afterBlue < beforeBlue)
                    ++darkened
                var delta = afterRed + afterGreen + afterBlue
                        - beforeRed - beforeGreen - beforeBlue
                if (delta > 0) {
                    ++increased
                    totalIncrease += delta
                }
            }
        }
        return {
            "increased": increased,
            "darkened": darkened,
            "totalIncrease": totalIncrease
        }
    }

    function widerOnlyPixelCount(baseline, narrow, wide) {
        var count = 0
        for (var y = 0; y < baseline.height; ++y) {
            for (var x = 0; x < baseline.width; ++x) {
                var narrowDelta = narrow.red(x, y) + narrow.green(x, y)
                        + narrow.blue(x, y) - baseline.red(x, y)
                        - baseline.green(x, y) - baseline.blue(x, y)
                var wideDelta = wide.red(x, y) + wide.green(x, y)
                        + wide.blue(x, y) - baseline.red(x, y)
                        - baseline.green(x, y) - baseline.blue(x, y)
                if (wideDelta > 0 && narrowDelta <= 0)
                    ++count
            }
        }
        return count
    }

    function cleanup() {
        if (glowEffect) {
            glowEffect.destroy()
            glowEffect = null
        }
        terrain.active = false
    }

    function test_native_glow_is_positive_bounded_and_releases_terrain() {
        if (!isSupportedAcceleratedBackend()) {
            terrain.active = false
            skip("Immersive glow GPU evidence requires D3D11, OpenGL, Vulkan, or Metal")
        }

        tryCompare(terrain, "renderStatus", TerrainReactorItem.Ready, 5000)
        tryVerify(function() { return terrain.frameCount >= 5 }, 5000)
        terrain.active = false
        wait(120)
        frozenFrameCount = terrain.frameCount
        wait(80)
        compare(terrain.frameCount, frozenFrameCount,
                "terrain must stop before pixel comparisons")

        glowEffect = glowComponent.createObject(stage, {
            "sourceItem": terrain,
            "width": stage.width,
            "height": stage.height,
            "intensity": 1.0,
            "spill": 2.0,
            "radius": 2.0,
            "opacity": 0.0
        })
        verify(glowEffect)
        compare(glowEffect.sourceItem, terrain)
        compare(glowEffect.textureWidth, 160)
        compare(glowEffect.textureHeight, 90)
        verify(glowEffect.textureWidth <= 512
               && glowEffect.textureHeight <= 512)

        for (var repetition = 0; repetition < 3; ++repetition) {
            glowEffect.opacity = 0.0
            var baseline = settleAndGrab("baseline-" + repetition)
            glowEffect.opacity = 1.0
            var glowing = settleAndGrab("visible-" + repetition)
            var stats = differenceStats(baseline, glowing)
            compare(stats.darkened, 0,
                    "additive halo must not darken any terrain channel")
            verify(stats.increased > 0,
                   "native halo produced no positive pixel increment")
            verify(stats.totalIncrease > 0,
                   "native halo added no measurable light")
            compare(terrain.frameCount, frozenFrameCount,
                    "glow sampling must not restart terrain rendering")
        }

        glowEffect.opacity = 0.0
        var radiusBaseline = settleAndGrab("radius-baseline")
        glowEffect.opacity = 1.0
        glowEffect.radius = 0.2
        var narrow = settleAndGrab("radius-narrow")
        glowEffect.radius = 2.0
        var wide = settleAndGrab("radius-wide")
        var narrowStats = differenceStats(radiusBaseline, narrow)
        var wideStats = differenceStats(radiusBaseline, wide)
        compare(narrowStats.darkened, 0)
        compare(wideStats.darkened, 0)
        verify(!wide.equals(narrow),
               "columnLightRadius did not change the rendered halo")
        verify(widerOnlyPixelCount(radiusBaseline, narrow, wide) > 0,
               "larger radius did not reach any additional exterior pixel")

        glowEffect.intensity = 0.0
        var zeroIntensity = settleAndGrab("zero-intensity")
        verify(zeroIntensity.equals(radiusBaseline),
               "zero glowIntensity must remove every halo pixel")
        glowEffect.intensity = 1.0
        glowEffect.spill = 0.0
        var zeroSpill = settleAndGrab("zero-spill")
        verify(zeroSpill.equals(radiusBaseline),
               "zero columnLightSpill must remove every halo pixel")

        glowEffect.destroy()
        glowEffect = null
        verify(waitForRendering(stage, 3000),
               "viewport did not render after glow destruction")
        wait(80)
        compare(terrain.frameCount, frozenFrameCount,
                "destroying glow must leave the frozen terrain idle")
    }
}
