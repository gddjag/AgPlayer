import QtQuick
import QtQuick.Controls
import QtTest
import AgPlayer

TestCase {
    id: suite
    name: "FormatResponsive"
    when: windowShown
    Component {
        id: toolWindow
        AudioToolsWindow {
            // Exercise the real shell at the available viewport independently
            // of native minimum-size policy, which has separate coverage.
            minimumWidth: 0
            minimumHeight: 0
            visible: true
        }
    }

    function init() {
        AudioToolsController.selectTool(1)
        SettingsController.transcodeFormat = "MP3"
        SettingsController.defaultOutputDirectory = ""
        FormatConverter.rejectPendingPlan()
        FormatConverter.clear()
        SettingsController.parallelJobs = 10
    }

    function inside(item, ancestor) {
        const point = item.mapToItem(ancestor, 0, 0)
        return item.visible && item.width > 0 && item.height > 0
                && point.x >= -0.5 && point.y >= -0.5
                && point.x + item.width <= ancestor.width + 0.5
                && point.y + item.height <= ancestor.height + 0.5
    }

    function scrollToControl(scroll, item) {
        for (let attempt = 0; attempt < 24 && !inside(item, scroll); ++attempt) {
            const point = item.mapToItem(scroll, 0, 0)
            mouseWheel(scroll, scroll.width - 24, scroll.height / 2, 0,
                       point.y < 0 ? 120 : -120)
            wait(30)
        }
        verify(inside(item, scroll), item.objectName + " must be reachable by scrolling")
    }

    function captureLayout(host, tag) {
        if (!visualFixtureOutput) return
        let saved = false
        host.contentItem.grabToImage(function(result) {
            saved = result.saveToFile(visualFixtureOutput + "-" + tag + ".png")
        })
        tryVerify(function() { return saved }, 5000)
    }

    function test_optionsAndStartRemainReachable_data() {
        return [
            {tag: "laptop-998x530", w: 998, h: 530},
            {tag: "laptop-1065x568", w: 1065, h: 568},
            {tag: "minimum-760x420", w: 760, h: 420},
            {tag: "desktop-1672x941", w: 1672, h: 941}
        ]
    }

    function test_optionsAndStartRemainReachable(data) {
        const host = createTemporaryObject(toolWindow, null, {width: data.w, height: data.h})
        verify(host)
        host.requestActivate()
        tryCompare(host, "width", data.w)
        tryCompare(host, "height", data.h)
        tryVerify(function() { return findChild(host, "formatSettingsPanel") !== null })
        const page = findChild(host, "formatConvertPage")
        const panel = findChild(page, "formatSettingsPanel")
        const scroll = findChild(panel, "formatSettingsScroll")
        const toggle = findChild(panel, "formatSettingsAdvancedToggle")
        verify(waitForRendering(page))
        verify(scroll.visible, "Conversion options must not be forced closed by laptop width")
        verify(inside(panel, page))

        const taskPanel = findChild(page, "formatTaskPanel")
        const filters = findChild(taskPanel, "formatStatusFilters")
        const filterButtons = []
        for (let index = 0; index < filters.children.length; ++index) {
            const child = filters.children[index]
            if (child.checkable === true) filterButtons.push(child)
        }
        compare(filterButtons.length, 5)
        for (const button of filterButtons) {
            verify(inside(button, taskPanel), button.text + " filter must remain in the task panel")
            verify(button.contentItem.contentWidth <= button.contentItem.width + 0.5,
                   button.text + " filter text must not be clipped")
            if (!button.checked) mouseClick(button, button.width / 2, button.height / 2)
            tryCompare(button, "checked", true)
        }
        compare(FormatConverter.filteredTaskModel.statusFilter, "Cancelled")
        mouseClick(filterButtons[0], filterButtons[0].width / 2, filterButtons[0].height / 2)
        compare(FormatConverter.filteredTaskModel.statusFilter, "All")

        const parallel = findChild(page, "converterParallelJobsBox")
        compare(parallel.displayText, "10")
        verify(!parallel.contentItem.truncated, "The selected parallel-job count must remain readable")
        const table = findChild(taskPanel, "formatTaskTableView")
        verify(table.height >= Theme.listRowHeight, "Wrapping filters must retain space for task rows")

        // The rightmost format option used to overflow the narrow side panel.
        const aac = findChild(panel, "formatOutputFormatButton-aac")
        verify(inside(aac, scroll), "All format choices must fit the side-panel viewport")
        mouseClick(aac, aac.width / 2, aac.height / 2)
        tryCompare(FormatConverter, "selectedFormat", "aac")
        const mp3 = findChild(panel, "formatOutputFormatButton-mp3")
        mouseClick(mp3, mp3.width / 2, mp3.height / 2)
        tryCompare(FormatConverter, "selectedFormat", "mp3")
        captureLayout(host, data.tag + "-formats")

        const bitrate = findChild(panel, "formatBitrateBox")
        scrollToControl(scroll, bitrate)
        mouseClick(bitrate, bitrate.width / 2, bitrate.height / 2)
        tryCompare(bitrate.popup, "visible", true)
        bitrate.popup.close()
        const directory = findChild(panel, "formatOutputDirectoryRow")
        scrollToControl(scroll, directory)
        mouseClick(directory, directory.width / 2, directory.height / 2)
        tryCompare(directory, "activeFocus", true)
        const keepMetadata = findChild(panel, "keepMetadataCheck")
        scrollToControl(scroll, keepMetadata)
        const originalMetadata = keepMetadata.checked
        mouseClick(keepMetadata, keepMetadata.width / 2, keepMetadata.height / 2)
        tryCompare(keepMetadata, "checked", !originalMetadata)
        compare(SettingsController.preserveMetadata, !originalMetadata)
        mouseClick(keepMetadata, keepMetadata.width / 2, keepMetadata.height / 2)
        if (data.tag === "laptop-998x530") captureLayout(host, data.tag + "-output")

        mouseClick(toggle, toggle.width / 2, toggle.height / 2)
        tryCompare(scroll, "visible", false)
        verify(inside(toggle, panel), "The reopen affordance must not be clipped")
        mouseClick(toggle, toggle.width / 2, toggle.height / 2)
        tryCompare(scroll, "visible", true)

        const start = findChild(page, "convertAllButton")
        const cancel = findChild(page, "cancelAllButton")
        verify(inside(start, page) && inside(cancel, page), "Primary actions must remain in the viewport")
        FormatConverter.addUrls([testAudioUrl])
        tryVerify(function() { return !FormatConverter.busy && FormatConverter.checkedCount === 1 })
        tryCompare(start, "enabled", true)
        mouseClick(start, start.width / 2, start.height / 2)
        const preflight = findChild(page, "formatPreflightDialog")
        tryCompare(preflight, "visible", true)
        FormatConverter.rejectPendingPlan()
        preflight.close()
        host.visible = false
    }
}
