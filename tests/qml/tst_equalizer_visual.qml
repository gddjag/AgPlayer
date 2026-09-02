import QtCore
import QtQuick
import QtQuick.Controls
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

    function childObjects(parent) {
        var result = []
        function append(values) {
            if (!values)
                return
            for (var index = 0; index < values.length; ++index) {
                if (values[index] && result.indexOf(values[index]) < 0)
                    result.push(values[index])
            }
        }
        append(parent.children)
        append(parent.data)
        if (parent.contentItem && result.indexOf(parent.contentItem) < 0)
            result.push(parent.contentItem)
        return result
    }

    function findChildrenByPrefix(parent, prefix, visited) {
        var matches = []
        if (!parent)
            return matches
        visited = visited || []
        if (visited.indexOf(parent) >= 0)
            return matches
        visited.push(parent)
        var children = childObjects(parent)
        for (var i = 0; i < children.length; ++i) {
            var child = children[i]
            if (child.objectName
                    && child.objectName.indexOf(prefix) === 0
                    && /^equalizerBand-\d+$/.test(child.objectName))
                matches.push(child)
            matches = matches.concat(findChildrenByPrefix(child, prefix,
                                                           visited))
        }
        return matches
    }

    function findChild(parent, objectName, visited) {
        if (!parent)
            return null
        visited = visited || []
        if (visited.indexOf(parent) >= 0)
            return null
        visited.push(parent)
        var legacyObjectName = objectName.replace(/^equalizerBand-(\d+)(-.+)$/,
                                                   "eqBandSlider-$1$2")
        if (parent.objectName === objectName
                || parent.objectName === legacyObjectName)
            return parent
        var children = childObjects(parent)
        for (var i = 0; i < children.length; ++i) {
            var child = children[i]
            if (child.objectName === objectName || child.objectName === legacyObjectName)
                return child
            var match = findChild(child, objectName, visited)
            if (match)
                return match
        }
        return null
    }

    function capture(path, size) {
        captureDone = false
        equalizer.contentItem.grabToImage(function(result) {
            captureDone = result.saveToFile(path)
        }, size)
        tryVerify(function() { return captureDone }, 5000)
    }

    function verifyFooterOutputReachable(viewportWidth, viewportHeight) {
        equalizer.width = viewportWidth
        equalizer.height = viewportHeight
        wait(60)
        var footerScroller = findChild(equalizer, "equalizerFooterScroller")
        var outputText = findChild(equalizer, "equalizerOutputLevelText")
        verify(footerScroller && outputText)
        if (footerScroller.contentWidth > footerScroller.width) {
            footerScroller.contentX = footerScroller.contentWidth
                                      - footerScroller.width
            wait(30)
        } else {
            compare(footerScroller.contentX, 0)
        }
        var point = outputText.mapToItem(footerScroller, 0, 0)
        verify(point.x >= -0.5,
               "output level left edge must be visible after scrolling right")
        verify(point.x + outputText.width <= footerScroller.width + 0.5,
               "output level right edge must be fully visible after scrolling right")
        footerScroller.contentX = 0
        wait(20)
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
        compare(findChild(equalizer, "equalizerBandsMaxLabel").text, "+6")
        compare(findChild(equalizer, "equalizerBandsZeroLabel").text, "0")
        compare(findChild(equalizer, "equalizerBandsMinLabel").text, "−6")
        mouseClick(range18)
        tryCompare(EqualizerController, "gainRangeDb", 18)
        compare(findChild(equalizer, "equalizerBandsMaxLabel").text, "+18")
        compare(findChild(equalizer, "equalizerBandsMinLabel").text, "−18")
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
        firstBand.setGain(1)
        tryCompare(firstBand, "gainDb", 1)
        mouseWheel(firstControl, firstControl.width / 2,
                   firstControl.height / 2, 120, 0)
        tryCompare(firstBand, "gainDb", 1)
        compare(firstControl.lastWheelAccepted, false)
        verify(firstControl.applyWheelDelta(0, 12))
        tryCompare(firstBand, "gainDb", 1.1)
        compare(firstControl.lastWheelAccepted, true)
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

    function test_meter_ticks_and_fill_share_segmented_mapping() {
        var meter = findChild(equalizer, "equalizerOutputMeter")
        verify(meter)
        var boundaries = [-24, -12, -6, -3, 0]
        var fractions = [0, 0.25, 0.5, 0.75, 1]
        var expectedBlocks = [0, 5, 9, 14, 18]
        for (var index = 0; index < boundaries.length; ++index) {
            var position = equalizer.meterPositionForDb(boundaries[index],
                                                        meter.width)
            compare(Math.round(position * 1000),
                    Math.round(meter.width * fractions[index] * 1000))
            var tick = findChild(meter, "equalizerMeterTick-" + index)
            verify(tick)
            compare(Math.round(tick.x + tick.width / 2),
                    Math.round(position))
            compare(equalizer.meterActiveBlockCount(boundaries[index]),
                    expectedBlocks[index])
        }
    }

    function test_response_curve_uses_equal_band_slots_and_real_gain_envelope() {
        equalizer.width = 1672
        equalizer.height = 941
        EqualizerController.setBandGain(0, -12)
        EqualizerController.setBandGain(14, 2.3)
        EqualizerController.setBandGain(17, 12)
        wait(80)
        var curve = findChild(equalizer, "equalizerResponseCurve")
        verify(curve)
        var envelope = curve.envelopePoints()
        compare(envelope.length, 18)
        compare(Math.round(curve.bandX(0)), Math.round(curve.plotLeft))
        compare(Math.round(curve.bandX(17)),
                Math.round(curve.width - curve.plotRight))
        var slotWidth = curve.bandX(1) - curve.bandX(0)
        for (var index = 0; index < envelope.length; ++index) {
            compare(Math.round(envelope[index].x * 1000),
                    Math.round(curve.bandX(index) * 1000))
            compare(Math.round(envelope[index].y * 1000),
                    Math.round(curve.gainY(
                                   EqualizerController.bandGain(index)) * 1000))
            if (index > 0) {
                verify(Math.abs((curve.bandX(index)
                                 - curve.bandX(index - 1)) - slotWidth) < 0.1,
                       "adjacent control bands must use equal visual slots")
            }
        }
        compare(Math.round(envelope[0].y * 1000),
                Math.round(curve.gainY(-12) * 1000))
        compare(Math.round(envelope[14].y * 1000),
                Math.round(curve.gainY(2.3) * 1000))
        compare(Math.round(envelope[17].y * 1000),
                Math.round(curve.gainY(12) * 1000))

        var wideLabels = []
        for (var band = 13; band <= 17; ++band) {
            var label = findChild(curve,
                                  "equalizerResponseFrequency-" + band)
            verify(label)
            compare(Math.round(label.x + label.width / 2),
                    Math.round(curve.bandX(band)))
            compare(label.y, findChild(curve,
                        "equalizerResponseFrequency-13").y)
            wideLabels.push(label)
        }
        for (var labelIndex = 1; labelIndex < wideLabels.length;
             ++labelIndex) {
            verify(wideLabels[labelIndex - 1].x
                   + wideLabels[labelIndex - 1].width
                   <= wideLabels[labelIndex].x,
                   "8k through 20k labels must remain readable on one wide row")
        }
    }

    function test_nineteen_controls_keep_readable_uniform_geometry() {
        equalizer.width = 860
        equalizer.height = 520
        wait(80)
        var scroller = findChild(equalizer, "equalizerBandScroller")
        var repeater = findChild(equalizer, "equalizerBandRepeater")
        var preamp = findChild(equalizer, "equalizerPreampSlider")
        verify(scroller && repeater && preamp)
        compare(repeater.count, 18)
        verify(scroller.contentWidth > scroller.width,
               "default EQ must scroll rather than squeeze nineteen columns")
        var first = repeater.itemAt(0)
        verify(first.width >= 52, "band width=" + first.width)
        compare(preamp.width, first.width)
        for (var index = 1; index < repeater.count; ++index) {
            compare(repeater.itemAt(index).width, first.width)
            compare(findChild(repeater.itemAt(index),
                              "eqBandSlider-" + index + "-control").height,
                    findChild(first, "eqBandSlider-0-control").height)
        }
        var firstHandle = findChild(first, "eqBandSlider-0-control")
        verify(firstHandle.width > 0 && firstHandle.height > 0)
        scroller.contentX = scroller.contentWidth - scroller.width
        wait(30)
        var preampPoint = preamp.mapToItem(scroller, 0, 0)
        verify(preampPoint.x < scroller.width
               && preampPoint.x + preamp.width > 0)
    }

    function test_reference_and_minimum_viewports_render() {
        compare(equalizer.width, 1180)
        compare(equalizer.height, 680)
        compare(equalizer.minimumWidth, 1080)
        compare(equalizer.minimumHeight, 480)
        compare(findChildrenByPrefix(equalizer, "equalizerBand-").length, 18)
        compare(findChild(equalizer, "equalizerTitle").font.pixelSize,
                Theme.fontSizePageTitle)
        compare(findChild(equalizer, "equalizerBand-0-frequency").font.pixelSize,
                Theme.fontSizeCaption)
        compare(findChild(equalizer, "equalizerBand-0-value").font.pixelSize,
                Theme.fontSizeCaption)

        var firstBand = findChild(equalizer, "equalizerBand-0")
        var footer = findChild(equalizer, "equalizerFooterPanel")
        var outputMeter = findChild(equalizer, "equalizerOutputMeter")
        var firstPoint = firstBand.mapToItem(equalizer.contentItem, 0, 0)
        var footerPoint = footer.mapToItem(equalizer.contentItem, 0, 0)
        var outputPoint = outputMeter.mapToItem(equalizer.contentItem, 0, 0)
        verify(firstPoint.x >= 0 && firstPoint.y >= 0)
        verify(footerPoint.y + footer.height <= equalizer.height)
        verify(outputPoint.x + outputMeter.width <= equalizer.width)
        var contentScroller = findChild(equalizer, "equalizerContentScroller")
        var bandScroller = findChild(equalizer, "equalizerBandScroller")
        var footerScroller = findChild(equalizer, "equalizerFooterScroller")
        verify(contentScroller.contentHeight <= contentScroller.height + 0.5)
        verify(bandScroller.contentWidth <= bandScroller.width + 0.5)
        verify(footerScroller.contentWidth <= footerScroller.width + 0.5)

        equalizer.width = 1080
        equalizer.height = 480
        wait(120)
        compare(equalizer.width, 1080)
        compare(equalizer.height, 480)
        footerPoint = footer.mapToItem(equalizer.contentItem, 0, 0)
        outputPoint = outputMeter.mapToItem(equalizer.contentItem, 0, 0)
        verify(footerPoint.y + footer.height <= equalizer.height)
        verify(outputPoint.x + outputMeter.width <= equalizer.width)
        verify(bandScroller.contentWidth <= bandScroller.width + 0.5,
               "all 18 EQ bands must fit without horizontal scrolling at the visual minimum")
        verify(footerScroller.contentWidth <= footerScroller.width + 0.5,
               "footer controls must fit without horizontal scrolling at the visual minimum")

        equalizer.width = 1180
        equalizer.height = 680
        wait(120)

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
        compare(findChild(equalizer, "equalizerTitleBar").height, 44)
        compare(findChild(equalizer, "equalizerHeaderPanel").height, 72)
        compare(findChild(equalizer, "equalizerResponsePanel").height, 291)
        compare(findChild(equalizer, "equalizerBandsPanel").height, 375)
        compare(findChild(equalizer, "equalizerFooterPanel").height, 77)
        compare(findChild(equalizer, "equalizerBandsMaxLabel").text, "+12")
        compare(findChild(equalizer, "equalizerBandsZeroLabel").text, "0")
        compare(findChild(equalizer, "equalizerBandsMinLabel").text, "−12")
        compare(findChild(equalizer, "equalizerOutputLevelText").text,
                "-1.5 dB")
        var resetButton = findChild(equalizer, "equalizerResetButton")
        verify(resetButton.iconSource.toString().indexOf("restore-line.svg") >= 0)
        compare(resetButton.icon.width, Theme.iconSizeMd)
        compare(findChild(equalizer, "equalizerMinimizeButton").icon.width, 18)
        compare(findChild(equalizer, "equalizerMaximizeButton").icon.width, 18)
        compare(findChild(equalizer, "equalizerCloseButton").icon.width, 18)
        var headerDivider = findChild(equalizer, "equalizerHeaderDivider")
        var dividerPoint = headerDivider.mapToItem(equalizer.contentItem, 0, 0)
        verify(dividerPoint.x > 0 && dividerPoint.x < equalizer.width)
        var saveButton = findChild(equalizer, "equalizerSaveButton")
        var savePoint = saveButton.mapToItem(equalizer.contentItem, 0, 0)
        verify(savePoint.x > dividerPoint.x)
        verify(savePoint.x + saveButton.width <= equalizer.width)
        compare(findChild(equalizer, "equalizerContentScrollBar").policy,
                ScrollBar.AlwaysOff)
        compare(findChild(equalizer, "equalizerBandScrollBar").policy,
                ScrollBar.AlwaysOff)
        compare(findChild(equalizer, "equalizerFooterScrollBar").policy,
                ScrollBar.AlwaysOff)
        verify(!findChild(equalizer, "equalizerContentScrollBar").visible)
        verify(!findChild(equalizer, "equalizerBandScrollBar").visible)
        verify(!findChild(equalizer, "equalizerFooterScrollBar").visible)
        var temp = StandardPaths.writableLocation(StandardPaths.TempLocation)
        capture(temp + "/AgPlayer-equalizer-1672x941.png",
                Qt.size(1672, 941))

        equalizer.width = 1180
        equalizer.height = 680
        wait(100)
        compare(equalizer.width, 1180)
        compare(equalizer.height, 680)
        compare(findChild(equalizer, "equalizerContentScrollBar").policy,
                ScrollBar.AlwaysOff)
        compare(findChild(equalizer, "equalizerBandScrollBar").policy,
                ScrollBar.AlwaysOff)
        compare(findChild(equalizer, "equalizerFooterScrollBar").policy,
                ScrollBar.AlwaysOff)
        verify(!findChild(equalizer, "equalizerContentScrollBar").visible)
        verify(!findChild(equalizer, "equalizerBandScrollBar").visible)
        verify(!findChild(equalizer, "equalizerFooterScrollBar").visible)
        capture(temp + "/AgPlayer-equalizer-1180x680.png",
                Qt.size(1180, 680))

        equalizer.width = 860
        equalizer.height = 520
        wait(100)
        compare(equalizer.width, 860)
        compare(equalizer.height, 520)
        compare(findChild(equalizer, "equalizerContentScrollBar").policy,
                ScrollBar.AlwaysOff)
        compare(findChild(equalizer, "equalizerBandScrollBar").policy,
                ScrollBar.AlwaysOn)
        compare(findChild(equalizer, "equalizerFooterScrollBar").policy,
                ScrollBar.AlwaysOff)
        verify(!findChild(equalizer, "equalizerContentScrollBar").visible)
        verify(findChild(equalizer, "equalizerBandScrollBar").visible)
        verify(!findChild(equalizer, "equalizerFooterScrollBar").visible)
        verifyFooterOutputReachable(860, 520)
        var compactCurve = findChild(equalizer, "equalizerResponseCurve")
        var highFrequencyLabels = []
        for (var highBand = 13; highBand <= 17; ++highBand) {
            var highLabel = findChild(
                        compactCurve,
                        "equalizerResponseFrequency-" + highBand)
            verify(highLabel.x >= 0
                   && highLabel.x + highLabel.width <= compactCurve.width,
                   "band " + highBand + " horizontal bounds: x="
                   + highLabel.x + " width=" + highLabel.width
                   + " curve=" + compactCurve.width)
            verify(highLabel.y >= 0
                   && highLabel.y + highLabel.height <= compactCurve.height,
                   "band " + highBand + " vertical bounds: y="
                   + highLabel.y + " height=" + highLabel.height
                   + " curve=" + compactCurve.height)
            highFrequencyLabels.push(highLabel)
        }
        for (var first = 0; first < highFrequencyLabels.length; ++first) {
            for (var second = first + 1;
                 second < highFrequencyLabels.length; ++second) {
                var a = highFrequencyLabels[first]
                var b = highFrequencyLabels[second]
                var overlaps = a.x < b.x + b.width
                               && a.x + a.width > b.x
                               && a.y < b.y + b.height
                               && a.y + a.height > b.y
                verify(!overlaps,
                       "compact frequency labels must remain legible: "
                       + (first + 13) + " [" + a.x + "," + a.y + ","
                       + a.width + "," + a.height + "] vs "
                       + (second + 13) + " [" + b.x + "," + b.y + ","
                       + b.width + "," + b.height + "]")
            }
        }
        var contentScroller = findChild(equalizer,
                                        "equalizerContentScroller")
        verify(contentScroller.contentHeight <= contentScroller.height + 0.5)
        var footer = findChild(equalizer, "equalizerFooterPanel")
        var footerPoint = footer.mapToItem(contentScroller, 0, 0)
        verify(footerPoint.y >= 0
               && footerPoint.y + footer.height <= contentScroller.height + 0.5,
               "footer controls must remain fully visible at minimum size")
        var bandScroller = findChild(equalizer, "equalizerBandScroller")
        verify(bandScroller.contentWidth > bandScroller.width)
        bandScroller.contentX = bandScroller.contentWidth - bandScroller.width
        wait(50)
        var preamp = findChild(equalizer, "equalizerPreampSlider")
        var preampPoint = preamp.mapToItem(bandScroller, 0, 0)
        verify(preampPoint.x + preamp.width > 0
               && preampPoint.x < bandScroller.width,
               "preamp must be horizontally reachable at minimum size")
        bandScroller.contentX = 0
        capture(temp + "/AgPlayer-equalizer-860x520.png",
                Qt.size(860, 520))
    }

    function test_status_meter_refreshes_only_while_visible() {
        var timer = findChild(equalizer, "equalizerStatusRefreshTimer")
        var meter = findChild(equalizer, "equalizerOutputMeter")
        var levelText = findChild(equalizer, "equalizerOutputLevelText")
        verify(timer && meter && levelText)
        verify(isNaN(equalizer.testDisplayOutputPeakDb))
        compare(equalizer.displayedOutputPeakDb,
                EqualizerController.outputPeakDb)
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
