import QtQuick
import QtQuick.Controls
import AgPlayer

Rectangle {
    id: canvas
    color: "#04182b"
    border.color: "#23415d"
    border.width: 1
    clip: true

    function boundedPixel(pixel) {
        return Math.max(0, Math.min(width, pixel))
    }
    function frameAtCanvasPixel(pixel) {
        return AudioEditorController.viewport.frameAtPixel(boundedPixel(pixel))
    }
    function pixelAtFrame(frame) {
        return AudioEditorController.viewport.pixelAtFrame(frame)
    }

    onWidthChanged: AudioEditorController.viewport.setViewportWidth(
        Math.max(1, width))
    Component.onCompleted: AudioEditorController.viewport.setViewportWidth(
        Math.max(1, width))

    Repeater {
        model: Math.max(1, AudioEditorController.channels)
        Rectangle {
            required property int index
            x: 0
            y: (index + 0.5) * canvas.height
               / Math.max(1, AudioEditorController.channels)
            width: canvas.width
            height: 1
            color: "#42617f"
            opacity: 0.7
        }
    }

    AudioEditorWaveformItem {
        id: waveform
        anchors.fill: parent
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        channelPeaks: AudioEditorController.viewportChannelPeaks
        waveformColor: "#2587ff"
        visible: AudioEditorController.hasDocument
            || AudioEditorController.recording
    }

    Rectangle {
        id: selectionOverlay
        visible: AudioEditorController.selectionStart >= 0
            && AudioEditorController.selectionEnd
                > AudioEditorController.viewport.visibleStartFrame
            && AudioEditorController.selectionStart
                < AudioEditorController.viewport.visibleEndFrame
        x: Math.max(0, canvas.pixelAtFrame(
            AudioEditorController.selectionStart))
        width: Math.max(0, Math.min(canvas.width,
            canvas.pixelAtFrame(AudioEditorController.selectionEnd)) - x)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 8
        color: "#224b7d99"
        border.color: "#5da8ff"
        border.width: 1
    }

    Repeater {
        model: AudioEditorController.timelineEventViews
        delegate: Rectangle {
            id: eventDelegate
            required property var modelData
            readonly property real rawStart: canvas.pixelAtFrame(
                Number(modelData.timelineStart))
            readonly property real rawEnd: canvas.pixelAtFrame(
                Number(modelData.timelineEnd))
            x: Math.max(0, rawStart)
            y: 8
            width: Math.max(0, Math.min(canvas.width, rawEnd) - x)
            height: canvas.height - 16
            visible: width > 0 && rawEnd > 0 && rawStart < canvas.width
            color: "transparent"
            border.color: "#247fe0"
            border.width: 1
            z: 2

            MouseArea {
                id: eventMoveArea
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                cursorShape: AudioEditorController.activeTool === "scissors"
                    ? Qt.CrossCursor : Qt.SizeHorCursor
                acceptedButtons: Qt.LeftButton
                property real pressCanvasX: 0
                property double originalTimelineStart: 0
                onPressed: function(mouse) {
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    pressCanvasX = point.x
                    originalTimelineStart = Number(modelData.timelineStart)
                    const frame = canvas.frameAtCanvasPixel(point.x)
                    AudioEditorController.seekFrame(frame)
                    if (AudioEditorController.activeTool === "scissors") {
                        AudioEditorController.splitEvent(modelData.id, frame)
                        mouse.accepted = true
                        return
                    }
                    AudioEditorController.beginEventGesture(
                        modelData.id, "move",
                        (mouse.modifiers & Qt.ControlModifier) !== 0)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed
                            || AudioEditorController.activeTool === "scissors")
                        return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const delta = canvas.frameAtCanvasPixel(point.x)
                        - canvas.frameAtCanvasPixel(pressCanvasX)
                    AudioEditorController.moveEvent(
                        modelData.id,
                        Math.max(0, Math.round(originalTimelineStart + delta)))
                }
                onReleased: {
                    if (AudioEditorController.activeTool !== "scissors")
                        AudioEditorController.endEventGesture()
                }
                onCanceled: AudioEditorController.cancelEventGesture()
            }

            MouseArea {
                id: leftTrim
                width: 18
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                cursorShape: Qt.SizeHorCursor
                z: 3
                property double originalTimelineStart: 0
                property double originalSourceStart: 0
                onPressed: function(mouse) {
                    originalTimelineStart = Number(modelData.timelineStart)
                    originalSourceStart = Number(modelData.sourceStart)
                    AudioEditorController.beginEventGesture(modelData.id, "trim")
                    mouse.accepted = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const nextTimeline = canvas.frameAtCanvasPixel(point.x)
                    const delta = nextTimeline - originalTimelineStart
                    AudioEditorController.trimEvent(
                        modelData.id,
                        Math.max(0, Math.round(originalSourceStart + delta)),
                        Number(modelData.sourceEnd),
                        Math.max(0, nextTimeline))
                }
                onReleased: AudioEditorController.endEventGesture()
                onCanceled: AudioEditorController.cancelEventGesture()
            }

            MouseArea {
                id: rightTrim
                width: 18
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                cursorShape: Qt.SizeHorCursor
                z: 3
                property double originalSourceEnd: 0
                property double originalTimelineEnd: 0
                onPressed: function(mouse) {
                    originalSourceEnd = Number(modelData.sourceEnd)
                    originalTimelineEnd = Number(modelData.timelineEnd)
                    AudioEditorController.beginEventGesture(modelData.id, "trim")
                    mouse.accepted = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const nextEnd = canvas.frameAtCanvasPixel(point.x)
                    AudioEditorController.trimEvent(
                        modelData.id, Number(modelData.sourceStart),
                        Math.max(Number(modelData.sourceStart) + 1,
                            Math.round(originalSourceEnd
                                + nextEnd - originalTimelineEnd)),
                        Number(modelData.timelineStart))
                }
                onReleased: AudioEditorController.endEventGesture()
                onCanceled: AudioEditorController.cancelEventGesture()
            }
        }
    }

    Rectangle {
        id: playheadLine
        x: canvas.pixelAtFrame(AudioEditorController.playheadFrame)
        y: 0
        width: 2
        height: canvas.height
        color: "#ffaf00"
        visible: (AudioEditorController.hasDocument
                  || AudioEditorController.recording)
            && AudioEditorController.playheadFrame
                >= AudioEditorController.viewport.visibleStartFrame
            && AudioEditorController.playheadFrame
                <= AudioEditorController.viewport.visibleEndFrame
        z: 6
    }

    MouseArea {
        id: backgroundInteraction
        objectName: "editorWaveformInteraction"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        enabled: AudioEditorController.hasDocument
        z: 1
        property double pressFrame: 0
        property real lastPanX: 0
        property bool selecting: false
        onPressed: function(mouse) {
            canvas.forceActiveFocus()
            if (mouse.button === Qt.MiddleButton) {
                lastPanX = mouse.x
                return
            }
            pressFrame = canvas.frameAtCanvasPixel(mouse.x)
            selecting = false
            AudioEditorController.seekFrame(pressFrame)
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            if ((pressedButtons & Qt.MiddleButton) !== 0) {
                AudioEditorController.viewport.panByPixels(mouse.x - lastPanX)
                lastPanX = mouse.x
                return
            }
            const frame = canvas.frameAtCanvasPixel(mouse.x)
            if (frame === pressFrame) return
            selecting = true
            AudioEditorController.setSelection(
                Math.min(pressFrame, frame), Math.max(pressFrame, frame))
        }
        onWheel: function(wheel) {
            if ((wheel.modifiers & Qt.ControlModifier) !== 0) {
                AudioEditorController.viewport.zoomAt(
                    wheel.angleDelta.y > 0 ? 0.8 : 1.25, wheel.x)
                wheel.accepted = true
            } else if ((wheel.modifiers & Qt.ShiftModifier) !== 0) {
                AudioEditorController.viewport.panByPixels(
                    wheel.angleDelta.y / 3)
                wheel.accepted = true
            }
        }
    }

    MouseArea {
        id: playheadHandle
        objectName: "editorPlayheadHandle"
        z: 8
        width: 24
        height: canvas.height
        x: playheadLine.x - width / 2
        enabled: playheadLine.visible
        cursorShape: Qt.SizeHorCursor
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const point = mapToItem(canvas, mouse.x, mouse.y)
            AudioEditorController.seekFrame(canvas.frameAtCanvasPixel(point.x))
        }
    }

    MouseArea {
        id: selectionStartHandle
        objectName: "editorSelectionStartHandle"
        z: 7
        width: 24
        height: selectionOverlay.height
        x: selectionOverlay.x - width / 2
        y: selectionOverlay.y
        enabled: selectionOverlay.visible
        visible: enabled
        cursorShape: Qt.SizeHorCursor
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const point = mapToItem(canvas, mouse.x, mouse.y)
            const frame = Math.min(canvas.frameAtCanvasPixel(point.x),
                                   AudioEditorController.selectionEnd - 1)
            AudioEditorController.setSelection(
                frame, AudioEditorController.selectionEnd)
        }
    }

    MouseArea {
        id: selectionEndHandle
        objectName: "editorSelectionEndHandle"
        z: 7
        width: 24
        height: selectionOverlay.height
        x: selectionOverlay.x + selectionOverlay.width - width / 2
        y: selectionOverlay.y
        enabled: selectionOverlay.visible
        visible: enabled
        cursorShape: Qt.SizeHorCursor
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const point = mapToItem(canvas, mouse.x, mouse.y)
            const frame = Math.max(canvas.frameAtCanvasPixel(point.x),
                                   AudioEditorController.selectionStart + 1)
            AudioEditorController.setSelection(
                AudioEditorController.selectionStart, frame)
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !AudioEditorController.hasDocument
            && !AudioEditorController.recording
        text: qsTr("导入音频后开始编辑")
        color: "#7891aa"
        font.family: Theme.fontPrimary
        font.pixelSize: 14
    }
}
