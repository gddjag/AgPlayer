import QtQuick
import QtTest
import AgPlayer

TestCase {
    name: "IntegratedWaveformPreview"
    when: windowShown

    function savePreview(item, suffix) {
        var prefix = visualFixtureOutput.replace(/\.png$/i, "")
        var image = grabImage(item)
        image.save(prefix + suffix + ".png")
    }

    function test_capture_before_after() {
        if (!visualFixtureOutput)
            skip("Set AGPLAYER_VISUAL_FIXTURE_OUTPUT to capture the comparison")
        var main = testMainWindow
        verify(main)
        verify(testAudioUrl.toString().length > 0, "A real audio fixture is required")
        var savedMode = SettingsController.playerShellMode
        var savedTheme = SettingsController.themeMode
        var savedWaveformMode = SettingsController.waveformMode
        var savedVolume = PlaybackController.volume
        var savedWidth = main.width
        var savedHeight = main.height
        var shell = null
        var defaultHeight = 0
        try {
            SettingsController.themeMode = 0
            SettingsController.waveformMode = 1
            SettingsController.playerShellMode = 1
            tryVerify(function() {
                shell = findChild(main, "integratedPlayerShell")
                return shell !== null
            })
            defaultHeight = shell.waveformHeight
            compare(defaultHeight, Qt.platform.os === "osx" ? 100 : 120,
                    "The native macOS default must already be reduced")
            var frame = findChild(shell, "integratedWaveformFrame")
            var waveform = findChild(shell, "integratedWaveform")
            verify(frame && waveform)

            main.showNormal()
            main.width = 1386
            main.height = 850
            tryCompare(main.contentItem, "width", 1386)
            tryCompare(main.contentItem, "height", 850)
            tryCompare(ImportController, "busy", false, 5000)
            PlaybackController.stop()
            nativeDropHelper.clearLibrary()
            main.importFiles([testAudioUrl])
            tryCompare(ImportController, "busy", false, 5000)
            tryCompare(LibraryModel, "count", 1, 5000)
            PlaybackController.setVolume(0)
            PlaybackController.playRow(0)
            tryVerify(function() { return PlaybackController.durationMs > 0 }, 5000)
            PlaybackController.pause()
            tryVerify(function() {
                return shell.waveformDurationMs > 0
                        && shell.waveformLayers.mix
                        && shell.waveformLayers.mix.length > 0
                        && shell.waveformLayers._complete !== false
            }, 30000, "Decode the real fixture through the shared WaveformProvider")
            PlaybackController.seek(Math.round(PlaybackController.durationMs * 0.35))
            wait(100)

            shell.waveformHeight = 120
            tryCompare(frame, "height", 128)
            tryCompare(waveform, "height", 100)
            savePreview(main.contentItem, "-before")
            savePreview(frame, "-waveform-before")

            // Non-macOS runs preview the same geometry; only macOS validates
            // the production default and renders the native Mac controls.
            shell.waveformHeight = Qt.platform.os === "osx" ? defaultHeight : 100
            tryCompare(frame, "height", 108)
            tryCompare(waveform, "height", 80)
            compare(main.contentItem.width, 1386)
            compare(main.contentItem.height, 850)
            savePreview(main.contentItem, "-after")
            savePreview(frame, "-waveform-after")
        } finally {
            PlaybackController.pause()
            PlaybackController.setVolume(savedVolume)
            if (shell)
                shell.waveformHeight = defaultHeight
            SettingsController.playerShellMode = savedMode
            SettingsController.themeMode = savedTheme
            SettingsController.waveformMode = savedWaveformMode
            main.width = savedWidth
            main.height = savedHeight
        }
    }
}
