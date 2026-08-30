import QtCore
import QtQuick
import QtTest
import AgPlayer

TestCase {
    name: "EqualizerVisual"
    when: windowShown
    visible: true
    width: 160
    height: 120

    property var equalizer: null
    property bool captureDone: false
    property int previousThemeMode: 0
    property string temporaryPresetId: ""

    Component {
        id: equalizerComponent
        EqualizerWindow { visible: true }
    }

    function capture(path, size) {
        captureDone = false
        equalizer.contentItem.grabToImage(function(result) {
            captureDone = result.saveToFile(path)
        }, size)
        tryVerify(function() { return captureDone }, 5000)
    }

    function init() {
        failOnWarning(/.?/)
        previousThemeMode = SettingsController.themeMode
        SettingsController.themeMode = 0
        temporaryPresetId = ""
        equalizer = equalizerComponent.createObject(null)
        verify(equalizer)
        wait(80)
        EqualizerController.resetAll()
        verify(EqualizerController.setGainRangeDb(12))
        verify(EqualizerController.setPrecisionMode("high"))
        EqualizerController.enabled = true
        EqualizerController.bypassed = false
        EqualizerController.autoClipProtection = true
    }

    function cleanup() {
        if (temporaryPresetId.length > 0)
            EqualizerController.deleteCustomPreset(temporaryPresetId)
        EqualizerController.resetAll()
        EqualizerController.bypassed = false
        EqualizerController.autoClipProtection = true
        if (equalizer) {
            equalizer.hide()
            equalizer.destroy()
            equalizer = null
        }
        SettingsController.themeMode = previousThemeMode
    }

    function test_all_visible_controls_drive_the_controller() {
        var enabledSwitch = findChild(equalizer, "equalizerEnabledSwitch")
        verify(enabledSwitch)
        EqualizerController.enabled = false
        tryCompare(enabledSwitch, "checked", false)
        mouseClick(enabledSwitch, enabledSwitch.width / 2,
                   enabledSwitch.height / 2)
        tryCompare(EqualizerController, "enabled", true)

        var presetBox = findChild(equalizer, "equalizerPresetBox")
        verify(presetBox)
        EqualizerController.setBandGain(0, 2.0)
        compare(EqualizerController.currentPresetId, "custom")
        var flatIndex = EqualizerController.presetIds.indexOf("flat")
        verify(flatIndex >= 0)
        presetBox.activated(flatIndex)
        tryCompare(EqualizerController, "currentPresetId", "flat")

        var saveButton = findChild(equalizer, "equalizerSaveButton")
        var saveDialog = findChild(equalizer, "equalizerSaveDialog")
        var saveName = findChild(equalizer, "equalizerSaveNameField")
        var saveConfirm = findChild(equalizer, "equalizerSaveConfirmButton")
        verify(saveButton && saveDialog && saveName && saveConfirm)
        mouseClick(saveButton)
        tryCompare(saveDialog, "visible", true)
        saveName.text = "Task 4 visual preset"
        mouseClick(saveConfirm)
        tryVerify(function() {
            return EqualizerController.presetNames.indexOf(
                        "Task 4 visual preset") >= 0
        })
        var savedIndex = EqualizerController.presetNames.indexOf(
                    "Task 4 visual preset")
        temporaryPresetId = EqualizerController.presetIds[savedIndex]
        verify(temporaryPresetId.indexOf("custom-") === 0)

        var manageButton = findChild(equalizer, "equalizerManageButton")
        var managePopup = findChild(equalizer, "equalizerManagePopup")
        var manageBox = findChild(equalizer, "equalizerManagePresetBox")
        var renameField = findChild(equalizer, "equalizerRenameField")
        var renameButton = findChild(equalizer, "equalizerRenameButton")
        var deleteButton = findChild(equalizer, "equalizerDeleteButton")
        verify(manageButton && managePopup && manageBox && renameField
               && renameButton && deleteButton)
        mouseClick(manageButton)
        tryCompare(managePopup, "visible", true)
        manageBox.currentIndex = manageBox.count - 1
        renameField.text = "Task 4 renamed preset"
        mouseClick(renameButton)
        tryVerify(function() {
            return EqualizerController.presetNames.indexOf(
                        "Task 4 renamed preset") >= 0
        })

        var bypassButton = findChild(equalizer, "equalizerBypassButton")
        var autoProtection = findChild(equalizer,
                                       "equalizerAutoProtection")
        verify(bypassButton && autoProtection,
               "advanced bypass and protection must remain reachable in Manage")
        mouseClick(bypassButton)
        tryCompare(EqualizerController, "bypassed", true)
        mouseClick(autoProtection)
        tryCompare(EqualizerController, "autoClipProtection", false)

        manageBox.currentIndex = manageBox.count - 1
        mouseClick(deleteButton)
        tryVerify(function() {
            return EqualizerController.presetIds.indexOf(temporaryPresetId) < 0
        })
        temporaryPresetId = ""
        managePopup.close()

        var contentScroller = findChild(equalizer,
                                        "equalizerContentScroller")
        contentScroller.contentY = contentScroller.contentHeight
                                   - contentScroller.height
        wait(50)
        var range6 = findChild(equalizer, "equalizerRange6Button")
        var range12 = findChild(equalizer, "equalizerRange12Button")
        var range18 = findChild(equalizer, "equalizerRange18Button")
        var precisionHigh = findChild(equalizer,
                                      "equalizerPrecisionHighButton")
        var precisionMedium = findChild(equalizer,
                                        "equalizerPrecisionMediumButton")
        var precisionLow = findChild(equalizer,
                                     "equalizerPrecisionLowButton")
        verify(range6 && range12 && range18)
        verify(precisionHigh && precisionMedium && precisionLow)
        mouseClick(range6)
        tryCompare(EqualizerController, "gainRangeDb", 6)
        mouseClick(range18)
        tryCompare(EqualizerController, "gainRangeDb", 18)
        mouseClick(range12)
        tryCompare(EqualizerController, "gainRangeDb", 12)
        mouseClick(precisionLow)
        tryCompare(EqualizerController, "precisionMode", "low")
        compare(EqualizerController.gainStepDb, 1)
        mouseClick(precisionMedium)
        tryCompare(EqualizerController, "precisionMode", "medium")
        compare(EqualizerController.gainStepDb, 0.5)
        mouseClick(precisionHigh)
        tryCompare(EqualizerController, "precisionMode", "high")
        compare(EqualizerController.gainStepDb, 0.1)

        var bands = findChild(equalizer, "equalizerBandRepeater")
        compare(bands.count, 18)
        var tenKhzBand = bands.itemAt(14)
        compare(tenKhzBand.frequencyLabel, "10k")
        var firstBand = bands.itemAt(0)
        var firstControl = findChild(firstBand, "eqBandSlider-0-control")
        firstBand.setGain(0)
        tryCompare(firstBand, "gainDb", 0)
        firstControl.forceActiveFocus()
        keyPress(Qt.Key_Up)
        tryCompare(EqualizerController, "currentPresetId", "custom")
        compare(EqualizerController.bandGain(0), 0.1)
        mouseWheel(firstControl, firstControl.width / 2,
                   firstControl.height / 2, 0, -120)
        tryCompare(firstBand, "gainDb", 0)
        mousePress(firstControl, firstControl.width / 2,
                   firstControl.height / 2, Qt.LeftButton)
        mouseMove(firstControl, firstControl.width / 2,
                  firstControl.height / 4, 40, Qt.LeftButton)
        mouseRelease(firstControl, firstControl.width / 2,
                     firstControl.height / 4, Qt.LeftButton)
        verify(firstBand.gainDb >= 5.5 && firstBand.gainDb <= 6.5)
        mouseDoubleClickSequence(firstControl, firstControl.width / 2,
                                 firstControl.height / 2)
        tryCompare(firstBand, "gainDb", 0)

        var preamp = findChild(equalizer, "equalizerPreampSlider")
        verify(preamp)
        preamp.setGain(-1.5)
        tryCompare(EqualizerController, "preampDb", -1.5)
        contentScroller.contentY = 0
        wait(50)
        var resetButton = findChild(equalizer, "equalizerResetButton")
        verify(resetButton)
        EqualizerController.setBandGain(17, -2.0)
        mouseClick(resetButton)
        tryCompare(EqualizerController, "preampDb", 0)
        compare(EqualizerController.bandGain(17), 0)
    }

    function test_reference_and_minimum_viewports_render() {
        compare(equalizer.width, 1000)
        compare(equalizer.height, 600)
        compare(equalizer.minimumWidth, 880)
        compare(equalizer.minimumHeight, 520)

        var gains = [2, 1.5, 0, -1, 0.5, -0.5, -1.5, -0.5,
                     0.5, 1.5, 2, 1, 2, 1.5, 1, 0, -1, -2]
        verify(EqualizerController.setGainRangeDb(12))
        verify(EqualizerController.setPrecisionMode("high"))
        EqualizerController.enabled = true
        EqualizerController.preampDb = -1.5
        for (var index = 0; index < gains.length; ++index) {
            EqualizerController.setBandGain(index, gains[index])
            compare(EqualizerController.bandGain(index), gains[index])
        }
        equalizer.testDisplayOutputPeakDb = -1.5

        equalizer.width = 1672
        equalizer.height = 941
        wait(120)
        compare(equalizer.width, 1672)
        compare(equalizer.height, 941)
        compare(findChild(equalizer, "equalizerTitleBar").height, 72)
        compare(findChild(equalizer, "equalizerHeaderPanel").height, 86)
        compare(findChild(equalizer, "equalizerResponsePanel").height, 291)
        compare(findChild(equalizer, "equalizerBandsPanel").height, 375)
        compare(findChild(equalizer, "equalizerFooterPanel").height, 77)
        compare(findChild(equalizer, "equalizerOutputLevelText").text,
                "-1.5 dB")
        var temp = StandardPaths.writableLocation(StandardPaths.TempLocation)
        capture(temp + "/AgPlayer-equalizer-1672x941.png",
                Qt.size(1672, 941))

        equalizer.width = 1180
        equalizer.height = 680
        wait(100)
        compare(equalizer.width, 1180)
        compare(equalizer.height, 680)
        capture(temp + "/AgPlayer-equalizer-1180x680.png",
                Qt.size(1180, 680))

        equalizer.width = 880
        equalizer.height = 520
        wait(100)
        compare(equalizer.width, 880)
        compare(equalizer.height, 520)
        var contentScroller = findChild(equalizer,
                                        "equalizerContentScroller")
        verify(contentScroller.contentHeight > contentScroller.height)
        contentScroller.contentY = contentScroller.contentHeight
                                   - contentScroller.height
        wait(50)
        var footer = findChild(equalizer, "equalizerFooterPanel")
        var footerPoint = footer.mapToItem(contentScroller, 0, 0)
        verify(footerPoint.y >= 0 && footerPoint.y < contentScroller.height,
               "footer controls must be vertically reachable at minimum size")
        var bandScroller = findChild(equalizer, "equalizerBandScroller")
        verify(bandScroller.contentWidth > bandScroller.width)
        bandScroller.contentX = bandScroller.contentWidth - bandScroller.width
        wait(50)
        var preamp = findChild(equalizer, "equalizerPreampSlider")
        var preampPoint = preamp.mapToItem(bandScroller, 0, 0)
        verify(preampPoint.x + preamp.width > 0
               && preampPoint.x < bandScroller.width,
               "preamp must be horizontally reachable at minimum size")
        contentScroller.contentY = 0
        bandScroller.contentX = 0
        capture(temp + "/AgPlayer-equalizer-880x520.png",
                Qt.size(880, 520))
    }

    function test_status_meter_refreshes_only_while_visible() {
        var timer = findChild(equalizer, "equalizerStatusRefreshTimer")
        var meter = findChild(equalizer, "equalizerOutputMeter")
        var levelText = findChild(equalizer, "equalizerOutputLevelText")
        verify(timer && meter && levelText)
        compare(timer.interval, 33)
        verify(timer.running)
        var revision = equalizer.statusRefreshRevision
        tryVerify(function() {
            return equalizer.statusRefreshRevision > revision
        }, 250)
        equalizer.testDisplayOutputPeakDb = -1.5
        tryCompare(levelText, "text", "-1.5 dB")
        equalizer.hide()
        tryCompare(timer, "running", false)
    }
}
