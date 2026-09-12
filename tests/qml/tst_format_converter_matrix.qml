import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: testCase
    name: "FormatConverterMatrix"
    when: windowShown
    visible: true
    width: 1672
    height: 941

    FormatConvertPage {
        id: page
        anchors.fill: parent
    }

    function resetConverter() {
        FormatConverter.rejectPendingPlan()
        const preflight = findChild(page, "formatPreflightDialog")
        const errorDialog = findChild(page, "formatErrorDialog")
        if (preflight && preflight.visible)
            preflight.close()
        if (errorDialog && errorDialog.visible)
            errorDialog.close()
        tryVerify(function() { return !FormatConverter.busy }, 30000,
                  "converter did not become idle during reset")
        FormatConverter.clear()
        tryCompare(FormatConverter, "fileCount", 0, 3000)
        wait(20)
    }

    function init() {
        testCase.width = 1672
        testCase.height = 941
        SettingsController.preserveMetadata = true
        SettingsController.defaultOutputDirectory = ""
        const settings = findChild(page, "formatSettingsPanel")
        if (settings)
            settings.resetCapabilityParameters()
        resetConverter()
    }

    function cleanup() {
        nativeDropHelper.unlockFiles()
        resetConverter()
    }

    function cleanupTestCase() {
        resetConverter()
        wait(50)
    }

    function test_bitrateVbrClicksKeepReferenceQualityWithoutChangingVorbisScale() {
        const settings = findChild(page, "formatSettingsPanel")
        const convert = findChild(page, "convertAllButton")
        const preflight = findChild(page, "formatPreflightDialog")
        verify(settings && convert && preflight)
        const input = nativeDropHelper.copyForNativeDrop(testAudioUrl)
        verify(input.toString().length > 0)
        FormatConverter.addUrls([input])
        tryVerify(function() { return !FormatConverter.busy }, 5000,
                  "VBR fixture did not finish loading")
        tryCompare(FormatConverter, "fileCount", 1, 3000)

        const formats = ["mp3", "aac", "opus"]
        for (let index = 0; index < formats.length; ++index) {
            const key = formats[index]
            const formatButton = findChild(page,
                                "formatOutputFormatButton-" + key)
            verify(formatButton)
            mouseClick(formatButton, formatButton.width / 2,
                       formatButton.height / 2, Qt.LeftButton)
            tryCompare(FormatConverter, "selectedFormat", key, 1000)
            tryCompare(settings, "bitrateMode", "cbr", 1000)
            const modeRepeater = findChild(page,
                                           "formatBitrateModeRepeater")
            verify(modeRepeater, key)
            let vbrButton = null
            for (let modeIndex = 0; modeIndex < modeRepeater.count;
                 ++modeIndex) {
                const candidate = modeRepeater.itemAt(modeIndex)
                if (candidate
                        && candidate.objectName
                                === "formatBitrateModeButton-vbr") {
                    vbrButton = candidate
                    break
                }
            }
            verify(vbrButton, key)
            vbrButton.forceActiveFocus()
            tryVerify(function() { return vbrButton.activeFocus }, 1000,
                      key + " VBR button did not accept keyboard focus")
            keyClick(Qt.Key_Space)
            tryCompare(settings, "bitrateMode", "vbr", 1000)
            compare(settings.quality, 75)
            mouseClick(convert, convert.width / 2, convert.height / 2,
                       Qt.LeftButton)
            tryVerify(function() { return preflight.visible }, 3000,
                      key + " VBR preflight did not open")
            const profile = FormatConverter.pendingPlan.tasks[0].resolvedProfile
            compare(profile.quality, 75, key)
            FormatConverter.rejectPendingPlan()
            preflight.close()
            tryVerify(function() { return !preflight.visible }, 3000,
                      key + " VBR preflight did not close")
        }

        const oggButton = findChild(page, "formatOutputFormatButton-ogg")
        verify(oggButton)
        mouseClick(oggButton, oggButton.width / 2,
                   oggButton.height / 2, Qt.LeftButton)
        tryCompare(FormatConverter, "selectedFormat", "ogg", 1000)
        tryCompare(settings, "quality", 8, 1000)
    }

    function test_availableFormatButtonsAndEveryVisibleModeProduceReopenableOutput() {
        const capabilities = FormatConverter.outputCapabilities
        const convert = findChild(page, "convertAllButton")
        const preflight = findChild(page, "formatPreflightDialog")
        const errorDialog = findChild(page, "formatErrorDialog")
        const settings = findChild(page, "formatSettingsPanel")
        const modeRow = findChild(page, "formatBitrateModeRow")
        verify(convert && preflight && errorDialog && settings && modeRow)
        let availableCount = 0
        for (let index = 0; index < capabilities.length; ++index) {
            const capability = capabilities[index]
            const formatButton = findChild(
                        page, "formatOutputFormatButton-" + capability.key)
            verify(formatButton)
            if (!capability.available) {
                verify(!formatButton.enabled)
                verify(String(capability.reason || "").length > 0)
                continue
            }
            const modes = capability.bitrateModes.length > 0
                        ? capability.bitrateModes : [{ key: "" }]
            for (let modeIndex = 0; modeIndex < modes.length; ++modeIndex) {
                const mode = modes[modeIndex]
                mouseClick(formatButton, formatButton.width / 2,
                           formatButton.height / 2, Qt.LeftButton)
                tryCompare(FormatConverter, "selectedFormat",
                           capability.key, 1000)
                wait(20)
                if (mode.key.length > 0) {
                    const modeButton = findChild(
                                page, "formatBitrateModeButton-" + mode.key)
                    verify(modeButton, capability.key + "/" + mode.key)
                    tryVerify(function() { return modeButton.visible }, 1000)
                    mouseClick(modeButton, modeButton.width / 2,
                               modeButton.height / 2, Qt.LeftButton)
                    tryCompare(settings, "bitrateMode", mode.key, 1000)
                } else {
                    compare(settings.bitrateMode, "")
                    verify(!modeRow.visible)
                }
                verify(settings.sampleRate === 0
                       || capability.sampleRates.indexOf(settings.sampleRate) >= 0,
                       capability.key + "/" + mode.key
                       + " invalid UI sample rate " + settings.sampleRate)
                // Let the capability-change callLater reset finish before the
                // same pointer sequence activates the next conversion.
                wait(50)

                const input = nativeDropHelper.copyForNativeDrop(testAudioUrl)
                verify(input.toString().length > 0)
                FormatConverter.addUrls([input])
                tryVerify(function() { return !FormatConverter.busy }, 5000,
                          capability.key + "/" + mode.key
                          + " fixture did not finish loading")
                tryCompare(FormatConverter, "fileCount", 1, 3000)
                verify(settings.sampleRate === 0
                       || capability.sampleRates.indexOf(settings.sampleRate) >= 0,
                       capability.key + "/" + mode.key
                       + " invalid post-load sample rate " + settings.sampleRate)
                tryCompare(convert, "enabled", true, 3000)
                mouseClick(convert, convert.width / 2, convert.height / 2,
                           Qt.LeftButton)
                wait(100)
                if ((FormatConverter.pendingPlan.tasks || []).length === 0
                        && !errorDialog.visible) {
                    mouseClick(convert, convert.width / 2,
                               convert.height / 2, Qt.LeftButton)
                }
                tryVerify(function() {
                    return (FormatConverter.pendingPlan.tasks || []).length > 0
                           || errorDialog.visible
                }, 3000, capability.key + "/" + mode.key
                   + " preflight/error dialog did not open")
                tryVerify(function() {
                    return preflight.visible || errorDialog.visible
                }, 3000, capability.key + "/" + mode.key
                   + " preflight/error dialog was not visible")
                verify(preflight.visible, capability.key + "/" + mode.key
                       + ": " + String(errorDialog.summary || ""))
                preflight.accept()
                tryVerify(function() {
                    return !FormatConverter.busy
                           && FormatConverter.completedCount
                                  + FormatConverter.failedCount === 1
                }, 30000, capability.key + "/" + mode.key)
                compare(FormatConverter.failedCount, 0,
                        capability.key + "/" + mode.key + ": "
                        + String(FormatConverter.files[0].errorMessage || ""))
                const row = FormatConverter.files[0]
                compare(row.status, "Done")
                const probe = nativeDropHelper.probeMedia(row.outputPath)
                verify(probe.readable, capability.key + "/" + mode.key)
                verify(probe.durationMs > 0)
                verify(probe.sampleRate > 0)
                tryVerify(function() {
                    return !preflight.visible && !errorDialog.visible
                }, 3000, capability.key + "/" + mode.key
                   + " dialog did not finish closing")
                FormatConverter.clear()
                tryCompare(FormatConverter, "fileCount", 0, 3000)
                wait(150)
            }
            ++availableCount
        }
        verify(availableCount > 0)
    }
}
