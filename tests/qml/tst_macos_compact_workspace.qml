import QtQuick
import QtTest
import AgPlayer

TestCase {
    id: suite
    name: "MacCompactWorkspace"
    width: 1800
    height: 1000
    visible: true
    when: windowShown

    function init() { failOnWarning(/Binding loop detected/) }
    function component(name) {
        const c = Qt.createComponent("../../app/qml/AgPlayer/components/tools/" + name + ".qml")
        compare(c.status, Component.Ready, c.errorString())
        return c
    }
    function inside(item, owner) {
        verify(item && item.visible)
        const a = item.mapToItem(owner, 0, 0)
        const b = item.mapToItem(owner, item.width, item.height)
        verify(a.x >= -1 && a.y >= -1 && b.x <= owner.width + 1 && b.y <= owner.height + 1,
               item.objectName + " outside: " + a + " / " + b)
    }
    function test_left_right_data() {
        const rows = []
        for (const size of [[980, 620], [1280, 720], [1672, 853]]) {
            for (const name of ["MetadataEditPage", "FilenameProcessPage", "LosslessIdentifyPage"])
                rows.push({tag: name + size.join("x"), name: name, w: size[0], h: size[1]})
        }
        return rows
    }
    function test_left_right(data) {
        const page = createTemporaryObject(component(data.name), suite,
            {width: data.w, height: data.h, macDesktopLayout: true})
        verify(page)
        waitForRendering(page)
        wait(0)
        const names = data.name === "MetadataEditPage"
            ? ["metadataFilePanel", "metadataInspectorPanel", "metadataApplyButton"]
            : data.name === "FilenameProcessPage"
              ? ["filenameFilePanel", "filenameRulesPanel", "filenameStartButton"]
              : ["losslessTaskPanel", "losslessEvidencePanel", "losslessStartButton"]
        const left = findChild(page, names[0]), right = findChild(page, names[1])
        inside(left, page); inside(right, page)
        verify(right.mapToItem(page, 0, 0).x >= left.mapToItem(page, left.width, 0).x)
        inside(findChild(page, names[2]), page)
        if (data.name === "LosslessIdentifyPage") inside(findChild(page, "losslessConclusionPanel"), page)
        if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length)
            grabImage(page).save(visualFixtureOutput + "-" + data.tag + ".png")
    }
    function test_recording_row_data() {
        return [{tag: "small", w: 980}, {tag: "medium", w: 1280}, {tag: "wide", w: 1672}]
    }
    function test_recording_row(data) {
        const page = createTemporaryObject(component("AudioEditorPage"), suite,
            {width: data.w, height: 720, macDesktopLayout: true})
        verify(page)
        waitForRendering(page); wait(0)
        const main = findChild(page, "editorMainColumn")
        verify(main.singleRowTransport, "live workspace must use the Mac transport")
        const button = findChild(page, "editorRecordButton")
        button.text = "停止录音"
        wait(0)
        verify(button.contentItem.implicitWidth <= button.availableWidth + 1,
               "stop recording label must fit inside its capsule")
        const playback = findChild(page, "editorPlaybackTransport")
        compare(playback.y, button.y)
        for (const name of ["editorInputDevice", "editorRecordButton", "editorPauseRecordingButton",
                            "editorPlaybackTransport", "editorCurrentTime"])
            inside(findChild(page, name), main)
        const tracks = findChild(page, "editorTrackScroller")
        verify(tracks.height >= tracks.contentHeight, "six tracks fit at desktop height")
        verify(findChild(page, "editorInspector").width <= 280)
        if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length)
            grabImage(page).save(visualFixtureOutput + "-editor-" + data.tag + ".png")
    }
    function test_separation_playback_data() {
        return [{tag: "compact", w: 980}, {tag: "wide", w: 1672}]
    }
    function test_separation_playback(data) {
        const page = createTemporaryObject(component("VocalSeparationPage"), suite,
            {width: data.w, height: 720, macCompactPlayback: true})
        verify(page)
        waitForRendering(page); wait(0)
        const bar = findChild(page, "separationBottomBar")
        inside(bar, page)
        verify(bar.height <= (page.compact ? 100 : 58) + 1)
        inside(findChild(page, "separationPrimaryAction"), bar)
        if (typeof visualFixtureOutput !== "undefined" && visualFixtureOutput.length)
            grabImage(page).save(visualFixtureOutput + "-separation-" + data.tag + ".png")
    }
}
