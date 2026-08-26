import QtQuick
import AgPlayer

Rectangle {
    id: canvas
    color: "#04182b"
    border.color: "#23415d"
    border.width: 1
    clip: true
    property double playheadCandidateFrame: -1
    property double selectionCandidateStart: -1
    property double selectionCandidateEnd: -1
    readonly property bool hasSelectionCandidate:
        selectionCandidateStart >= 0
        && selectionCandidateEnd > selectionCandidateStart
    readonly property double displayedSelectionStart: hasSelectionCandidate
        ? selectionCandidateStart : AudioEditorController.selectionStart
    readonly property double displayedSelectionEnd: hasSelectionCandidate
        ? selectionCandidateEnd : AudioEditorController.selectionEnd
    readonly property double displayedPlayheadFrame:
        playheadCandidateFrame >= 0 ? playheadCandidateFrame
                                    : AudioEditorController.playheadFrame

    function boundedPixel(pixel) {
        return Math.max(0, Math.min(width, pixel))
    }
    function frameAtCanvasPixel(pixel) {
        return AudioEditorController.viewport.frameAtPixel(boundedPixel(pixel))
    }
    function pixelAtFrame(frame) {
        return AudioEditorController.viewport.pixelAtFrame(frame)
    }
    function previewSelection(first, second) {
        selectionCandidateStart = Math.min(first, second)
        selectionCandidateEnd = Math.max(first, second)
    }
    function commitSelection() {
        if (hasSelectionCandidate) {
            AudioEditorController.setSelection(
                selectionCandidateStart, selectionCandidateEnd)
        }
        selectionCandidateStart = -1
        selectionCandidateEnd = -1
    }
    function cancelSelectionPreview() {
        selectionCandidateStart = -1
        selectionCandidateEnd = -1
    }
    function gainFromY(y, height) {
        return Math.max(0, Math.min(2, 2 * (1 - y / height)))
    }
    function envelopeGainAtOffset(points, offset) {
        let previousOffset = 0
        let previousGain = 1
        for (let index = 0; index < points.length; ++index) {
            const pointOffset = Number(points[index].offset)
            const pointGain = Number(points[index].gain)
            if (offset <= pointOffset) {
                if (pointOffset === previousOffset)
                    return pointGain
                const fraction = (offset - previousOffset)
                    / (pointOffset - previousOffset)
                return previousGain
                    + (pointGain - previousGain) * fraction
            }
            previousOffset = pointOffset
            previousGain = pointGain
        }
        return previousGain
    }
    function selectionDurationText() {
        const frames = Math.max(0,
            displayedSelectionEnd - displayedSelectionStart)
        const centiseconds = AudioEditorController.sampleRate > 0
            ? Math.round(frames * 100 / AudioEditorController.sampleRate) : 0
        const minutes = Math.floor(centiseconds / 6000)
        const seconds = Math.floor(centiseconds % 6000 / 100)
        const hundredths = centiseconds % 100
        return String(minutes).padStart(2, "0") + ":"
            + String(seconds).padStart(2, "0") + "."
            + String(hundredths).padStart(2, "0")
    }
    function addEnvelopePointForEvent(eventId, timelineStart, timelineEnd,
                                      canvasX, lineY, lineHeight) {
        const offset = Math.max(0, Math.min(
            Number(timelineEnd) - Number(timelineStart) - 1,
            frameAtCanvasPixel(canvasX) - Number(timelineStart)))
        const gain = Math.max(0, Math.min(2,
            2 * (1 - lineY / lineHeight)))
        AudioEditorController.addEnvelopePoint(
            eventId, Math.round(offset), gain)
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
        waveformColor: SettingsController.waveformMode === 0
            ? SettingsController.waveformSolidBaseColor
            : (SettingsController.waveformMode === 2
                ? SettingsController.spectrumSolidColor
                : SettingsController.waveformRgbBaseColor)
        density: SettingsController.waveformMode === 2
            ? 1.0 : SettingsController.waveformDensity
        lineWidth: SettingsController.waveformMode === 2
            ? 3.0 : SettingsController.waveformThickness
        visible: AudioEditorController.hasDocument
            || AudioEditorController.recording
    }

    Rectangle {
        id: selectionOverlay
        visible: canvas.displayedSelectionStart >= 0
            && canvas.displayedSelectionEnd
                > AudioEditorController.viewport.visibleStartFrame
            && canvas.displayedSelectionStart
                < AudioEditorController.viewport.visibleEndFrame
        x: Math.max(0, canvas.pixelAtFrame(
            canvas.displayedSelectionStart))
        width: Math.max(0, Math.min(canvas.width,
            canvas.pixelAtFrame(canvas.displayedSelectionEnd)) - x)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 8
        color: "#224b7d99"
        border.width: 0
        z: 3

        Canvas {
            id: selectionDashedBorder
            objectName: "editorSelectionDashedBorder"
            anchors.fill: parent
            property color borderColor: "#78baff"
            onPaint: {
                const context = getContext("2d")
                context.clearRect(0, 0, width, height)
                context.strokeStyle = borderColor
                context.lineWidth = 1
                context.setLineDash([5, 4])
                context.strokeRect(0.5, 0.5,
                    Math.max(0, width - 1), Math.max(0, height - 1))
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            Component.onCompleted: requestPaint()
        }
        onVisibleChanged: selectionDashedBorder.requestPaint()
        onXChanged: selectionDashedBorder.requestPaint()
        onWidthChanged: selectionDashedBorder.requestPaint()
        onHeightChanged: selectionDashedBorder.requestPaint()
        Connections {
            target: canvas
            function onDisplayedSelectionStartChanged() {
                selectionDashedBorder.requestPaint()
            }
            function onDisplayedSelectionEndChanged() {
                selectionDashedBorder.requestPaint()
            }
        }

        Rectangle {
            id: handoffCapsule
            objectName: "editorSelectionHandoffCapsule"
            x: 6
            y: parent.height - height - 6
            width: handoffLabel.implicitWidth + 20
            height: 28
            radius: 14
            color: "#241b0e"
            border.color: "#ff8a00"
            border.width: 1
            Text {
                id: handoffLabel
                objectName: "editorSelectionHandoffLabel"
                anchors.centerIn: parent
                text: qsTr("拖出片段")
                color: "#ff8a00"
                font.pixelSize: 12
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.DragCopyCursor
                onPressed: function(mouse) {
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    mouse.accepted = AudioEditorController
                        .beginSelectionHandoff(point.x, point.y)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    AudioEditorController.updateSelectionHandoff(
                        point.x, point.y)
                }
                onCanceled: AudioEditorController.cancelSelectionHandoff()
                onReleased: AudioEditorController.cancelSelectionHandoff()
            }
        }

        Rectangle {
            id: selectionDurationCapsule
            objectName: "editorSelectionDurationCapsule"
            x: parent.width - width - 6
            y: 6
            width: selectionDuration.implicitWidth + 12
            height: 22
            radius: 4
            color: "#241b0e"
            border.color: "#ff8a00"
            border.width: 1
            Text {
                id: selectionDuration
                objectName: "editorSelectionDuration"
                anchors.centerIn: parent
                text: canvas.selectionDurationText()
                color: "#ff8a00"
                font.pixelSize: 12
            }
        }
    }

    Repeater {
        model: AudioEditorController.timelineEventViews
        delegate: Rectangle {
            id: eventDelegate
            objectName: "editorEventVisualBoundary"
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
            border.color: "transparent"
            border.width: 0
            z: 2

            MouseArea {
                id: eventMoveArea
                objectName: "editorEventBodyInteraction"
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                z: 5
                cursorShape: AudioEditorController.activeTool === "scissors"
                    ? Qt.CrossCursor : Qt.ArrowCursor
                acceptedButtons: Qt.LeftButton
                property real pressCanvasX: 0
                property double pressFrame: 0
                property double originalTimelineStart: 0
                property bool duplicateMove: false
                property bool movedDuringPress: false
                onPressed: function(mouse) {
                    movedDuringPress = false
                    canvas.cancelSelectionPreview()
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    pressCanvasX = point.x
                    originalTimelineStart = Number(modelData.timelineStart)
                    const frame = canvas.frameAtCanvasPixel(point.x)
                    pressFrame = frame
                    AudioEditorController.seekFrame(frame)
                    if (AudioEditorController.activeTool === "scissors") {
                        AudioEditorController.splitEvent(modelData.id, frame)
                        mouse.accepted = true
                        return
                    }
                    duplicateMove = (mouse.modifiers & Qt.ControlModifier) !== 0
                    if (duplicateMove) {
                        AudioEditorController.beginEventGesture(
                            modelData.id, "move", true)
                    }
                }
                onPositionChanged: function(mouse) {
                    if (!pressed
                            || AudioEditorController.activeTool === "scissors")
                        return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const frame = canvas.frameAtCanvasPixel(point.x)
                    if (frame !== pressFrame)
                        movedDuringPress = true
                    if (duplicateMove) {
                        AudioEditorController.moveEvent(
                            modelData.id, Math.max(0, Math.round(
                                originalTimelineStart + frame - pressFrame)))
                    } else if (frame !== pressFrame) {
                        canvas.previewSelection(pressFrame, frame)
                    }
                }
                onReleased: function(mouse) {
                    if (duplicateMove) AudioEditorController.endEventGesture()
                    if (!duplicateMove && movedDuringPress)
                        canvas.commitSelection()
                    duplicateMove = false
                }
                onCanceled: {
                    if (duplicateMove) AudioEditorController.cancelEventGesture()
                    else canvas.cancelSelectionPreview()
                    duplicateMove = false
                }
            }

            MouseArea {
                id: leftTrim
                objectName: "editorEventLeftTrimHandle"
                width: 18
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                cursorShape: Qt.SizeHorCursor
                z: 7
                activeFocusOnTab: true
                Accessible.name: qsTr("片段左修剪手柄")
                Accessible.role: Accessible.Slider
                property double originalTimelineStart: 0
                property double originalSourceStart: 0
                onPressed: function(mouse) {
                    originalTimelineStart = Number(modelData.timelineStart)
                    originalSourceStart = Number(modelData.sourceStart)
                    AudioEditorController.beginEventGesture(modelData.id, "trim")
                    forceActiveFocus()
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
                Keys.onPressed: function(event) {
                    if (event.key !== Qt.Key_Left && event.key !== Qt.Key_Right)
                        return
                    const delta = event.key === Qt.Key_Left ? -1 : 1
                    AudioEditorController.trimEvent(
                        modelData.id,
                        Math.max(0, Number(modelData.sourceStart) + delta),
                        Number(modelData.sourceEnd),
                        Math.max(0, Number(modelData.timelineStart) + delta))
                    event.accepted = true
                }
            }

            MouseArea {
                id: rightTrim
                objectName: "editorEventRightTrimHandle"
                width: 18
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                cursorShape: Qt.SizeHorCursor
                z: 7
                activeFocusOnTab: true
                Accessible.name: qsTr("片段右修剪手柄")
                Accessible.role: Accessible.Slider
                property double originalSourceEnd: 0
                property double originalTimelineEnd: 0
                onPressed: function(mouse) {
                    originalSourceEnd = Number(modelData.sourceEnd)
                    originalTimelineEnd = Number(modelData.timelineEnd)
                    AudioEditorController.beginEventGesture(modelData.id, "trim")
                    forceActiveFocus()
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
                Keys.onPressed: function(event) {
                    if (event.key !== Qt.Key_Left && event.key !== Qt.Key_Right)
                        return
                    const delta = event.key === Qt.Key_Left ? -1 : 1
                    AudioEditorController.trimEvent(
                        modelData.id, Number(modelData.sourceStart),
                        Math.max(Number(modelData.sourceStart) + 1,
                            Number(modelData.sourceEnd) + delta),
                        Number(modelData.timelineStart))
                    event.accepted = true
                }
            }

            Canvas {
                id: fadeCurve
                objectName: "editorEventFadeOutCurve"
                anchors.fill: parent
                z: 4
                visible: fadeOutHandle.displayedFadeOut > 0
                onPaint: {
                    const context = getContext("2d")
                    context.clearRect(0, 0, width, height)
                    const startX = canvas.pixelAtFrame(
                        Number(modelData.timelineEnd)
                            - fadeOutHandle.displayedFadeOut)
                        - eventDelegate.x
                    context.strokeStyle = "#e9d7cf"
                    context.lineWidth = 1.2
                    context.beginPath()
                    context.moveTo(Math.max(0, startX), 10)
                    context.bezierCurveTo(width - 28, 18,
                        width - 8, height * 0.48, width - 2, height - 10)
                    context.stroke()
                }
                Connections {
                    target: AudioEditorController
                    function onDocumentChanged() { fadeCurve.requestPaint() }
                }
            }

            MouseArea {
                id: fadeOutHandle
                objectName: "editorEventFadeOutHandle"
                z: 8
                x: 0; y: 0
                width: parent.width; height: 24
                property double candidateFadeOut: Number(modelData.fadeOut)
                readonly property double displayedFadeOut: pressed
                    ? candidateFadeOut : Number(modelData.fadeOut)
                cursorShape: Qt.SizeHorCursor
                activeFocusOnTab: true
                Accessible.name: qsTr("淡出控制点")
                Accessible.role: Accessible.Slider
                onPressed: function(mouse) {
                    candidateFadeOut = Number(modelData.fadeOut)
                    forceActiveFocus()
                    mouse.accepted = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const eventFrames = Number(modelData.timelineEnd)
                        - Number(modelData.timelineStart)
                    const frames = Math.max(0, Math.min(eventFrames,
                        Number(modelData.timelineEnd)
                            - canvas.frameAtCanvasPixel(point.x)))
                    candidateFadeOut = Math.round(frames)
                    fadeCurve.requestPaint()
                }
                onReleased: AudioEditorController.setEventFadeOut(
                    modelData.id, candidateFadeOut)
                onCanceled: candidateFadeOut = Number(modelData.fadeOut)
                Rectangle {
                    x: Math.max(0, Math.min(parent.width - width,
                        canvas.pixelAtFrame(Number(modelData.timelineEnd)
                            - fadeOutHandle.displayedFadeOut) - eventDelegate.x
                            - width / 2))
                    anchors.verticalCenter: parent.verticalCenter
                    width: 9; height: 9; radius: width / 2
                    color: "#1d7fff"
                    border.color: "#e7f1ff"; border.width: 2
                }
            }

            Item {
                id: volumeLine
                objectName: "editorEventVolumeLine"
                x: 10; width: parent.width - 20
                y: 10; height: parent.height - 20
                z: 9
                property real gainCandidate: Number(eventDelegate.modelData.gain)
                readonly property real displayedGain: gainInteraction.pressed
                    ? gainCandidate : Number(eventDelegate.modelData.gain)

                Rectangle {
                    objectName: "editorEventGainLine"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    y: (1 - Math.max(0, Math.min(2,
                        volumeLine.displayedGain)) / 2) * volumeLine.height
                    height: 1; color: "#d8e6f2"; opacity: 0.65
                }

                Canvas {
                    id: envelopeLine
                    objectName: "editorEnvelopeLine"
                    anchors.fill: parent
                    readonly property real implicitStartGain:
                        canvas.envelopeGainAtOffset(
                            eventDelegate.modelData.envelope || [], 0)
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        const points = eventDelegate.modelData.envelope || []
                        if (points.length === 0) return
                        function pointY(gain) {
                            return (1 - Math.max(0, Math.min(2,
                                Number(gain))) / 2) * height
                        }
                        context.strokeStyle = "#78baff"
                        context.lineWidth = 1.5
                        context.beginPath()
                        context.moveTo(0, pointY(implicitStartGain))
                        for (let index = 0; index < points.length; ++index) {
                            const point = points[index]
                            const x = canvas.pixelAtFrame(
                                Number(eventDelegate.modelData.timelineStart)
                                + Number(point.offset))
                                - eventDelegate.x - volumeLine.x
                            context.lineTo(x, pointY(point.gain))
                        }
                        context.lineTo(width,
                            pointY(points[points.length - 1].gain))
                        context.stroke()
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Connections {
                        target: AudioEditorController
                        function onDocumentChanged() {
                            envelopeLine.requestPaint()
                        }
                    }
                }

                MouseArea {
                    id: gainInteraction
                    objectName: "editorEventGainInteraction"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    y: (1 - Math.max(0, Math.min(2,
                        volumeLine.displayedGain)) / 2) * volumeLine.height
                        - height / 2
                    height: 24
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.SizeVerCursor
                    onPressed: function(mouse) {
                        volumeLine.gainCandidate = Number(
                            eventDelegate.modelData.gain)
                        AudioEditorController.beginEventGainGesture(
                            eventDelegate.modelData.id)
                        mouse.accepted = true
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const point = mapToItem(volumeLine, mouse.x, mouse.y)
                        volumeLine.gainCandidate = canvas.gainFromY(
                            point.y, volumeLine.height)
                        AudioEditorController.updateEventGainGesture(
                            volumeLine.gainCandidate)
                    }
                    onReleased: AudioEditorController.endEventGainGesture()
                    onCanceled: {
                        AudioEditorController.cancelEventGainGesture()
                        volumeLine.gainCandidate = Number(
                            eventDelegate.modelData.gain)
                    }
                    onDoubleClicked: function(mouse) {
                        const point = mapToItem(canvas, mouse.x, mouse.y)
                        const linePoint = mapToItem(
                            volumeLine, mouse.x, mouse.y)
                        canvas.addEnvelopePointForEvent(
                            eventDelegate.modelData.id,
                            Number(eventDelegate.modelData.timelineStart),
                            Number(eventDelegate.modelData.timelineEnd),
                            point.x, linePoint.y, volumeLine.height)
                        mouse.accepted = true
                    }
                }

                Repeater {
                    model: modelData.envelope || []
                    Item {
                        required property var modelData
                        objectName: "editorEnvelopePoint"
                        width: 18; height: 18
                        x: canvas.pixelAtFrame(Number(eventDelegate.modelData.timelineStart)
                            + Number(modelData.offset)) - eventDelegate.x
                            - volumeLine.x - width / 2
                        y: (1 - Math.max(0, Math.min(2,
                            Number(modelData.gain))) / 2) * volumeLine.height
                            - height / 2
                        z: 2
                        Rectangle {
                            anchors.centerIn: parent
                            width: 8; height: 8; radius: 4
                            color: "#1d7fff"
                            border.color: "#e7f1ff"; border.width: 1
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            property double originalOffset: 0
                            onPressed: function(mouse) {
                                originalOffset = Number(parent.modelData.offset)
                                if (mouse.button === Qt.RightButton) {
                                    AudioEditorController.removeEnvelopePoint(
                                        eventDelegate.modelData.id,
                                        originalOffset)
                                } else {
                                    AudioEditorController.beginEnvelopePointGesture(
                                        eventDelegate.modelData.id,
                                        originalOffset)
                                }
                                mouse.accepted = true
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed
                                        || (pressedButtons & Qt.LeftButton) === 0)
                                    return
                                const point = mapToItem(
                                    volumeLine, mouse.x, mouse.y)
                                const canvasPoint = mapToItem(
                                    canvas, mouse.x, mouse.y)
                                const offset = Math.max(0, Math.min(
                                    Number(eventDelegate.modelData.timelineEnd)
                                        - Number(eventDelegate.modelData.timelineStart) - 1,
                                    canvas.frameAtCanvasPixel(canvasPoint.x)
                                        - Number(eventDelegate.modelData.timelineStart)))
                                AudioEditorController.updateEnvelopePointGesture(
                                    Math.round(offset),
                                    canvas.gainFromY(point.y, volumeLine.height))
                            }
                            onReleased: function(mouse) {
                                if (mouse.button === Qt.LeftButton)
                                    AudioEditorController.endEnvelopePointGesture()
                            }
                            onCanceled: AudioEditorController
                                .cancelEnvelopePointGesture()
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: playheadLine
        objectName: "editorPlayheadLine"
        x: canvas.pixelAtFrame(canvas.displayedPlayheadFrame)
        y: 0
        width: 2
        height: canvas.height
        color: "#ffaf00"
        visible: (AudioEditorController.hasDocument
                  || AudioEditorController.recording)
            && canvas.displayedPlayheadFrame
                >= AudioEditorController.viewport.visibleStartFrame
            && canvas.displayedPlayheadFrame
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
            canvas.cancelSelectionPreview()
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
            canvas.previewSelection(pressFrame, frame)
        }
        onReleased: {
            if (selecting) canvas.commitSelection()
            selecting = false
        }
        onCanceled: {
            canvas.cancelSelectionPreview()
            selecting = false
        }
        onWheel: function(wheel) {
            if ((wheel.modifiers & Qt.ControlModifier) !== 0) {
                AudioEditorController.viewport.zoomAt(
                    wheel.angleDelta.y > 0 ? 1.25 : 0.8, wheel.x)
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
        height: 28
        x: playheadLine.x - width / 2
        enabled: playheadLine.visible
        cursorShape: Qt.SizeHorCursor
        activeFocusOnTab: true
        Accessible.name: qsTr("播放头")
        Accessible.role: Accessible.Slider
        onPressed: function(mouse) {
            canvas.playheadCandidateFrame = AudioEditorController.playheadFrame
            forceActiveFocus()
            mouse.accepted = true
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const point = mapToItem(canvas, mouse.x, mouse.y)
            canvas.playheadCandidateFrame = canvas.frameAtCanvasPixel(point.x)
        }
        onReleased: {
            AudioEditorController.seekFrame(canvas.playheadCandidateFrame)
            canvas.playheadCandidateFrame = -1
        }
        onCanceled: canvas.playheadCandidateFrame = -1
        Keys.onPressed: function(event) {
            if (event.key !== Qt.Key_Left && event.key !== Qt.Key_Right)
                return
            AudioEditorController.seekFrame(Math.max(0, Math.min(
                AudioEditorController.totalFrames,
                AudioEditorController.playheadFrame
                    + (event.key === Qt.Key_Left ? -1 : 1))))
            event.accepted = true
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
        activeFocusOnTab: true
        Accessible.name: qsTr("选区起点")
        Accessible.role: Accessible.Slider
        onPressed: {
            canvas.previewSelection(AudioEditorController.selectionStart,
                                    AudioEditorController.selectionEnd)
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const point = mapToItem(canvas, mouse.x, mouse.y)
            const frame = Math.min(canvas.frameAtCanvasPixel(point.x),
                                   canvas.selectionCandidateEnd - 1)
            canvas.previewSelection(frame, canvas.selectionCandidateEnd)
        }
        onReleased: canvas.commitSelection()
        onCanceled: canvas.cancelSelectionPreview()
        Keys.onPressed: function(event) {
            if (event.key !== Qt.Key_Left && event.key !== Qt.Key_Right)
                return
            AudioEditorController.setSelection(Math.max(0, Math.min(
                AudioEditorController.selectionEnd - 1,
                AudioEditorController.selectionStart
                    + (event.key === Qt.Key_Left ? -1 : 1))),
                AudioEditorController.selectionEnd)
            event.accepted = true
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
        activeFocusOnTab: true
        Accessible.name: qsTr("选区终点")
        Accessible.role: Accessible.Slider
        onPressed: {
            canvas.previewSelection(AudioEditorController.selectionStart,
                                    AudioEditorController.selectionEnd)
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const point = mapToItem(canvas, mouse.x, mouse.y)
            const frame = Math.max(canvas.frameAtCanvasPixel(point.x),
                                   canvas.selectionCandidateStart + 1)
            canvas.previewSelection(canvas.selectionCandidateStart, frame)
        }
        onReleased: canvas.commitSelection()
        onCanceled: canvas.cancelSelectionPreview()
        Keys.onPressed: function(event) {
            if (event.key !== Qt.Key_Left && event.key !== Qt.Key_Right)
                return
            AudioEditorController.setSelection(
                AudioEditorController.selectionStart,
                Math.max(AudioEditorController.selectionStart + 1, Math.min(
                    AudioEditorController.totalFrames,
                    AudioEditorController.selectionEnd
                        + (event.key === Qt.Key_Left ? -1 : 1))))
            event.accepted = true
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
