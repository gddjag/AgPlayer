import QtQuick
import QtQuick.Controls
import AgPlayer

Rectangle {
    id: canvas
    color: Theme.editorCanvas
    border.color: Theme.divider
    border.width: 1
    clip: true
    property bool controlModifierHeld: false
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
    function selectionContains(frame) {
        return AudioEditorController.selectionStart >= 0
            && frame >= AudioEditorController.selectionStart
            && frame < AudioEditorController.selectionEnd
    }
    function eventIdAtFrame(frame) {
        const events = AudioEditorController.timelineEventViews
        for (let index = events.length - 1; index >= 0; --index) {
            const event = events[index]
            if (frame >= Number(event.timelineStart)
                    && frame < Number(event.timelineEnd))
                return String(event.id)
        }
        return ""
    }
    function selectWholeEvent(eventId) {
        if (!eventId || String(eventId).length === 0)
            return false
        AudioEditorController.clearSelection()
        AudioEditorController.selectEvent(String(eventId))
        return true
    }
    function finishMoveGesture(eventId, timelineStart, moved) {
        let updated = true
        if (moved)
            updated = AudioEditorController.moveEvent(eventId, timelineStart)
        const ended = AudioEditorController.endEventGesture()
        return updated && ended
    }
    function previousTimelineEvent(id) {
        const events = AudioEditorController.timelineEventViews
        for (let index = 1; index < events.length; ++index) {
            if (events[index].id === id) return events[index - 1]
        }
        return null
    }
    function nextTimelineEvent(id) {
        const events = AudioEditorController.timelineEventViews
        for (let index = 0; index + 1 < events.length; ++index) {
            if (events[index].id === id) return events[index + 1]
        }
        return null
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
    function fadeGain(position, curve) {
        const bounded = Math.max(0, Math.min(1, position))
        const normalizedCurve = String(curve || "linear").toLowerCase()
        if (normalizedCurve === "smooth")
            return bounded * bounded * (3 - 2 * bounded)
        if (normalizedCurve === "exponential")
            return (Math.exp(5 * bounded) - 1) / (Math.exp(5) - 1)
        return bounded
    }
    function selectionDurationText() {
        const frames = Math.max(0,
            displayedSelectionEnd - displayedSelectionStart)
        const milliseconds = AudioEditorController.sampleRate > 0
            ? Math.round(frames * 1000 / AudioEditorController.sampleRate) : 0
        const hours = Math.floor(milliseconds / 3600000)
        const minutes = Math.floor(milliseconds % 3600000 / 60000)
        const seconds = Math.floor(milliseconds % 60000 / 1000)
        const millis = milliseconds % 1000
        return (hours > 0 ? String(hours).padStart(2, "0") + ":" : "")
            + String(minutes).padStart(2, "0") + ":"
            + String(seconds).padStart(2, "0") + "."
            + String(millis).padStart(3, "0")
    }
    function addEnvelopePointForEvent(eventId, timelineStart, timelineEnd,
                                      canvasX, lineY, lineHeight,
                                      relativeGainForComposite) {
        const offset = Math.max(0, Math.min(
            Number(timelineEnd) - Number(timelineStart) - 1,
            frameAtCanvasPixel(canvasX) - Number(timelineStart)))
        const compositeGain = gainFromY(lineY, lineHeight)
        const gain = typeof relativeGainForComposite === "function"
            ? relativeGainForComposite(Math.round(offset), compositeGain)
            : compositeGain
        AudioEditorController.addEnvelopePoint(
            eventId, Math.round(offset), gain)
    }

    function syncViewportWaveformMetrics() {
        AudioEditorController.viewport.setViewportWidth(Math.max(1, width))
        AudioEditorController.setViewportWaveformDevicePixelRatio(
            Screen.devicePixelRatio)
    }

    onWidthChanged: syncViewportWaveformMetrics()
    onWindowChanged: syncViewportWaveformMetrics()
    Component.onCompleted: {
        syncViewportWaveformMetrics()
    }

    Rectangle {
        id: waveformCenterLine
        objectName: "editorWaveformCenterLine"
        x: 0
        y: Math.round((canvas.height - height) * 0.5)
        width: canvas.width
        height: 1
        color: Theme.borderStrong
        opacity: 0.7
    }

    AudioEditorWaveformItem {
        id: waveform
        objectName: "editorWaveformGeometry"
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
        sampleMode: AudioEditorController.viewport.visibleFrameCount
            <= Math.max(2, Math.floor(width) * 2)
        visible: AudioEditorController.hasDocument
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
        color: Theme.editorSelection
        border.width: 0
        z: 3

        Canvas {
            id: selectionDashedBorder
            objectName: "editorSelectionDashedBorder"
            anchors.fill: parent
            property color borderColor: Theme.focus
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
            color: Theme.surfacePressed
            border.color: Theme.editorSelectionLabel
            border.width: 1
            Text {
                id: handoffLabel
                objectName: "editorSelectionHandoffLabel"
                anchors.centerIn: parent
                text: qsTr("拖出片段")
                color: Theme.editorSelectionLabel
                font.pixelSize: Theme.fontSizeCaption
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
                onReleased: AudioEditorController.releaseSelectionHandoff()
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
            color: Theme.surfacePressed
            border.color: Theme.editorSelectionLabel
            border.width: 1
            Text {
                id: selectionDuration
                objectName: "editorSelectionDuration"
                anchors.centerIn: parent
                text: canvas.selectionDurationText()
                color: Theme.editorSelectionLabel
                font.pixelSize: Theme.fontSizeCaption
            }
        }
    }

    Repeater {
        model: AudioEditorController.timelineEventViews
        delegate: Rectangle {
            id: eventDelegate
            objectName: "editorEventVisualBoundary"
            required property var modelData
            readonly property double displayedSourceStart: leftTrim.gestureActive
                ? leftTrim.candidateSourceStart : Number(modelData.sourceStart)
            readonly property double displayedSourceEnd: rightTrim.gestureActive
                ? rightTrim.candidateSourceEnd : Number(modelData.sourceEnd)
            readonly property double eventFrames: displayedSourceEnd
                - displayedSourceStart
            readonly property double displayedTimelineStart:
                eventMoveArea.gestureActive
                    ? eventMoveArea.candidateTimelineStart
                    : leftTrim.gestureActive
                        ? leftTrim.candidateTimelineStart
                        : Number(modelData.timelineStart)
            readonly property double displayedTimelineEnd: displayedTimelineStart
                + eventFrames
            readonly property real rawStart: canvas.pixelAtFrame(
                displayedTimelineStart)
            readonly property real rawEnd: canvas.pixelAtFrame(
                displayedTimelineEnd)
            x: Math.max(0, rawStart)
            y: 8
            width: Math.max(0, Math.min(canvas.width, rawEnd) - x)
            height: canvas.height - 16
            visible: width > 0 && rawEnd > 0 && rawStart < canvas.width
            color: "transparent"
            border.color: Theme.waveformBlue
            border.width: 1
            z: 2

            Rectangle {
                objectName: "editorEventSelectedOverlay"
                anchors.fill: parent
                color: String(eventDelegate.modelData.id)
                    === AudioEditorController.selectedEventId
                    ? Qt.rgba(Theme.focus.r, Theme.focus.g,
                              Theme.focus.b, 0.22) : "transparent"
                border.color: Theme.focus
                border.width: String(eventDelegate.modelData.id)
                    === AudioEditorController.selectedEventId ? 2 : 0
            }

            MouseArea {
                id: eventMoveArea
                objectName: "editorEventHeaderInteraction"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                // The clip move/selection seam is a fixed logical-pixel
                // contract at every supported shell size.
                height: 24
                z: 5
                cursorShape: AudioEditorController.activeTool === "scissors"
                    ? Qt.CrossCursor : Qt.ArrowCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true
                property real pressCanvasX: 0
                property double pressFrame: 0
                property double originalTimelineStart: 0
                property double candidateTimelineStart: 0
                property bool duplicateMove: false
                property bool movedDuringPress: false
                property bool gestureActive: false
                property bool playOnRelease: false
                onPressed: function(mouse) {
                    movedDuringPress = false
                    playOnRelease = false
                    canvas.cancelSelectionPreview()
                    if (mouse.button === Qt.RightButton) {
                        if ((mouse.modifiers & Qt.ControlModifier) !== 0
                                || canvas.controlModifierHeld) {
                            canvas.selectWholeEvent(modelData.id)
                            mouse.accepted = true
                            return
                        }
                        const point = mapToItem(canvas, mouse.x, mouse.y)
                        const frame = canvas.frameAtCanvasPixel(point.x)
                        if (canvas.selectionContains(frame)) {
                            AudioEditorController.clearSelection()
                            AudioEditorController.clearEventSelection()
                        }
                        mouse.accepted = true
                        return
                    }
                    AudioEditorController.selectEvent(String(modelData.id))
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    pressCanvasX = point.x
                    originalTimelineStart = Number(modelData.timelineStart)
                    candidateTimelineStart = originalTimelineStart
                    const frame = canvas.frameAtCanvasPixel(point.x)
                    pressFrame = frame
                    const outsideSelection = !canvas.selectionContains(frame)
                    playOnRelease = canvas.displayedSelectionEnd
                        > canvas.displayedSelectionStart && outsideSelection
                    AudioEditorController.seekFrame(frame)
                    if (AudioEditorController.activeTool === "scissors") {
                        AudioEditorController.splitEvent(modelData.id, frame)
                        mouse.accepted = true
                        return
                    }
                    duplicateMove = (mouse.modifiers & Qt.ControlModifier) !== 0
                        || canvas.controlModifierHeld
                    gestureActive = AudioEditorController.beginEventGesture(
                        modelData.id, "move", duplicateMove)
                }
                onDoubleClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton
                            && ((mouse.modifiers & Qt.ControlModifier) !== 0
                                || canvas.controlModifierHeld)) {
                        AudioEditorController.clearSelection()
                        AudioEditorController.clearEventSelection()
                        mouse.accepted = true
                    }
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !gestureActive
                            || AudioEditorController.activeTool === "scissors")
                        return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const frame = canvas.frameAtCanvasPixel(point.x)
                    if (frame !== pressFrame)
                        movedDuringPress = true
                    if (frame !== pressFrame) {
                        candidateTimelineStart = Math.max(0, Math.round(
                            originalTimelineStart + frame - pressFrame))
                    }
                }
                onReleased: function(mouse) {
                    const hadGesture = gestureActive
                    const moved = movedDuringPress
                    const shouldPlay = playOnRelease && !moved
                            && !AudioEditorController.playing
                    const eventId = String(modelData.id)
                    const destination = candidateTimelineStart
                    gestureActive = false
                    duplicateMove = false
                    playOnRelease = false
                    if (shouldPlay)
                        AudioEditorController.playPause()
                    if (hadGesture)
                        canvas.finishMoveGesture(eventId, destination, moved)
                }
                onCanceled: {
                    if (gestureActive)
                        AudioEditorController.cancelEventGesture()
                    gestureActive = false
                    duplicateMove = false
                    playOnRelease = false
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
                property string sharedLeftId: ""
                property string sharedRightId: ""
                property bool useSharedBoundary: false
                property bool gestureActive: false
                property bool hasPreviousEvent: false
                property double candidateSourceStart: 0
                property double candidateTimelineStart: 0
                onPressed: function(mouse) {
                    originalTimelineStart = Number(modelData.timelineStart)
                    originalSourceStart = Number(modelData.sourceStart)
                    candidateSourceStart = originalSourceStart
                    candidateTimelineStart = originalTimelineStart
                    const previous = canvas.previousTimelineEvent(modelData.id)
                    hasPreviousEvent = previous !== null
                    sharedLeftId = previous ? previous.id : ""
                    sharedRightId = modelData.id
                    useSharedBoundary = previous
                        && Number(previous.timelineEnd)
                            === Number(modelData.timelineStart)
                        && Number(previous.sourceEnd)
                            === Number(modelData.sourceStart)
                    if (useSharedBoundary) {
                        gestureActive = AudioEditorController.beginSharedBoundaryGesture(
                            sharedLeftId, sharedRightId)
                    } else {
                        gestureActive = AudioEditorController.beginEventGesture(
                            modelData.id, "trim")
                    }
                    forceActiveFocus()
                    mouse.accepted = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !gestureActive) return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const requestedTimeline = Math.max(0,
                        canvas.frameAtCanvasPixel(point.x))
                    const delta = requestedTimeline - originalTimelineStart
                    let nextSourceStart = Math.max(0, Math.min(
                        Number(modelData.sourceEnd) - 1,
                        Math.round(originalSourceStart + delta)))
                    // Trimming the leading edge of the first audible clip is a
                    // crop, not a move: keep the edited result pinned to zero.
                    let nextTimeline = hasPreviousEvent ? Math.max(0, Math.round(
                        originalTimelineStart
                            + nextSourceStart - originalSourceStart)) : 0
                    let updated = false
                    if (useSharedBoundary) {
                        updated = AudioEditorController.trimSharedBoundary(
                            sharedLeftId, sharedRightId,
                            nextSourceStart)
                    } else {
                        updated = AudioEditorController.trimEvent(
                            modelData.id,
                            nextSourceStart,
                            Number(modelData.sourceEnd),
                            nextTimeline)
                    }
                    if (updated) {
                        candidateSourceStart = nextSourceStart
                        candidateTimelineStart = nextTimeline
                    }
                }
                onReleased: {
                    const active = gestureActive
                    gestureActive = false
                    useSharedBoundary = false
                    if (active)
                        AudioEditorController.endEventGesture()
                }
                onCanceled: {
                    const active = gestureActive
                    gestureActive = false
                    useSharedBoundary = false
                    if (active)
                        AudioEditorController.cancelEventGesture()
                }
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
                property string sharedLeftId: ""
                property string sharedRightId: ""
                property bool useSharedBoundary: false
                property bool gestureActive: false
                property double candidateSourceEnd: 0
                onPressed: function(mouse) {
                    originalSourceEnd = Number(modelData.sourceEnd)
                    originalTimelineEnd = Number(modelData.timelineEnd)
                    candidateSourceEnd = originalSourceEnd
                    const next = canvas.nextTimelineEvent(modelData.id)
                    sharedLeftId = modelData.id
                    sharedRightId = next ? next.id : ""
                    useSharedBoundary = next
                        && Number(next.timelineStart)
                            === Number(modelData.timelineEnd)
                        && Number(next.sourceStart)
                            === Number(modelData.sourceEnd)
                    if (useSharedBoundary) {
                        gestureActive = AudioEditorController.beginSharedBoundaryGesture(
                            sharedLeftId, sharedRightId)
                    } else {
                        gestureActive = AudioEditorController.beginEventGesture(
                            modelData.id, "trim")
                    }
                    forceActiveFocus()
                    mouse.accepted = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !gestureActive) return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const nextEnd = canvas.frameAtCanvasPixel(point.x)
                    const boundary = Math.round(originalSourceEnd
                        + nextEnd - originalTimelineEnd)
                    const nextSourceEnd = Math.max(
                        Number(modelData.sourceStart) + 1, boundary)
                    let updated = false
                    if (useSharedBoundary) {
                        updated = AudioEditorController.trimSharedBoundary(
                            sharedLeftId, sharedRightId, boundary)
                    } else {
                        updated = AudioEditorController.trimEvent(
                            modelData.id, Number(modelData.sourceStart),
                            nextSourceEnd,
                            Number(modelData.timelineStart))
                    }
                    if (updated)
                        candidateSourceEnd = nextSourceEnd
                }
                onReleased: {
                    const active = gestureActive
                    gestureActive = false
                    useSharedBoundary = false
                    if (active)
                        AudioEditorController.endEventGesture()
                }
                onCanceled: {
                    const active = gestureActive
                    gestureActive = false
                    useSharedBoundary = false
                    if (active)
                        AudioEditorController.cancelEventGesture()
                }
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

            Item {
                id: volumeLine
                objectName: "editorEventVolumeLine"
                x: 10; width: parent.width - 20
                y: canvas.height < 80 ? 18 : 10
                height: canvas.height < 80
                    ? Math.max(12, parent.height - 18)
                    : parent.height - 20
                z: 9
                property real gainCandidate: Number(eventDelegate.modelData.gain)
                readonly property real displayedGain: gainInteraction.pressed
                    ? gainCandidate : Number(eventDelegate.modelData.gain)
                property bool envelopePreviewActive: false
                property double envelopePreviewOriginalOffset: 0
                property double envelopePreviewOffset: 0
                property double envelopePreviewGain: 1
                readonly property double displayedFadeIn: Math.max(0,
                    Number(eventDelegate.modelData.fadeIn || 0))
                readonly property double displayedFadeOut: Math.max(0,
                    Number(eventDelegate.modelData.fadeOut || 0))
                readonly property var displayedEnvelope: {
                    const source = eventDelegate.modelData.envelope || []
                    if (!envelopePreviewActive)
                        return source
                    const result = []
                    let replaced = false
                    for (let index = 0; index < source.length; ++index) {
                        const point = source[index]
                        if (!replaced && Number(point.offset)
                                === envelopePreviewOriginalOffset) {
                            result.push({ offset: envelopePreviewOffset,
                                          gain: envelopePreviewGain })
                            replaced = true
                        } else {
                            result.push({ offset: Number(point.offset),
                                          gain: Number(point.gain) })
                        }
                    }
                    result.sort(function(first, second) {
                        return first.offset - second.offset
                    })
                    return result
                }

                function fadeGainAtOffset(offset) {
                    const eventFrames = Number(eventDelegate.modelData.timelineEnd)
                        - Number(eventDelegate.modelData.timelineStart)
                    const fadeIn = displayedFadeIn
                    const fadeOut = displayedFadeOut
                    let fade = 1
                    if (fadeIn > 0 && offset < fadeIn)
                        fade = canvas.fadeGain(offset / Math.max(1, fadeIn - 1),
                            eventDelegate.modelData.fadeInCurve || "linear")
                    if (fadeOut > 0 && offset >= eventFrames - fadeOut)
                        fade *= canvas.fadeGain((eventFrames - 1 - offset)
                            / Math.max(1, fadeOut - 1),
                            eventDelegate.modelData.fadeOutCurve || "linear")
                    return Math.max(0, fade)
                }

                function relativeEnvelopeGainForComposite(offset, compositeGain) {
                    const base = Math.max(0, displayedGain)
                        * fadeGainAtOffset(offset)
                    if (base <= 0.000001)
                        return compositeGain <= 0 ? 0 : 2
                    return Math.max(0, Math.min(2, compositeGain / base))
                }

                function requestCombinedGainPaint() {
                    combinedGainCurve.requestPaint()
                }

                function combinedGainAtOffset(offset) {
                    return Math.max(0, Math.min(2, displayedGain
                        * canvas.envelopeGainAtOffset(displayedEnvelope, offset)
                        * fadeGainAtOffset(offset)))
                }

                HoverHandler {
                    id: combinedGainHover
                    acceptedDevices: PointerDevice.Mouse
                }

                Item {
                    id: combinedGainLine
                    objectName: "editorEventCombinedGainLine"
                    x: 0; y: Math.round(volumeLine.height / 2)
                    width: volumeLine.width; height: 1
                    visible: true
                }

                Canvas {
                    id: combinedGainCurve
                    objectName: "editorEventCombinedGainCurve"
                    anchors.fill: parent
                    z: 3
                    onPaint: {
                        const context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        const eventStart = Number(eventDelegate.modelData.timelineStart)
                        const eventEnd = Number(eventDelegate.modelData.timelineEnd)
                        const eventFrames = Math.max(1, eventEnd - eventStart)
                        context.strokeStyle = Theme.focus
                        context.lineWidth = 1.5
                        context.beginPath()
                        for (let pixel = 0; pixel <= width; ++pixel) {
                            const frame = canvas.frameAtCanvasPixel(
                                eventDelegate.x + volumeLine.x + pixel)
                            const offset = Math.max(0, Math.min(eventFrames,
                                frame - eventStart))
                            const y = (1 - volumeLine.combinedGainAtOffset(offset) / 2)
                                * height
                            if (pixel === 0) context.moveTo(pixel, y)
                            else context.lineTo(pixel, y)
                        }
                        context.stroke()
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Connections {
                        target: AudioEditorController
                        function onDocumentChanged() { combinedGainCurve.requestPaint() }
                    }
                }

                Menu {
                    id: fadeCurveMenu
                    objectName: "editorFadeCurveMenu"
                    property int actionCount: 3
                    property var curveLabels: [qsTr("线性"), qsTr("平滑"), qsTr("指数")]
                    property string eventId: ""
                    property string fadeSide: ""
                    MenuItem { text: qsTr("线性"); onTriggered: fadeCurveMenu.applyCurve("Linear") }
                    MenuItem { text: qsTr("平滑"); onTriggered: fadeCurveMenu.applyCurve("Smooth") }
                    MenuItem { text: qsTr("指数"); onTriggered: fadeCurveMenu.applyCurve("Exponential") }
                    function applyCurve(curve) {
                        if (typeof AudioEditorController.setEventFadeCurve === "function")
                            AudioEditorController.setEventFadeCurve(
                                eventId, fadeSide === "in", curve)
                    }
                }

                Rectangle {
                    objectName: "editorEventGainLine"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    y: (1 - Math.max(0, Math.min(2,
                        volumeLine.displayedGain)) / 2) * volumeLine.height
                    height: 1; color: Theme.textSecondary; opacity: 0.65
                    visible: false
                }

                Canvas {
                    id: envelopeLine
                    objectName: "editorEnvelopeLine"
                    anchors.fill: parent
                    visible: false
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
                        context.strokeStyle = Theme.focus
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
                    readonly property double hoverOffset: {
                        const localX = combinedGainHover.hovered
                            ? combinedGainHover.point.position.x : width / 2
                        const frame = canvas.frameAtCanvasPixel(
                            eventDelegate.x + volumeLine.x
                                + Math.max(0, Math.min(width, localX)))
                        return Math.max(0, frame
                            - Number(eventDelegate.modelData.timelineStart))
                    }
                    y: (1 - volumeLine.combinedGainAtOffset(hoverOffset) / 2)
                        * volumeLine.height - height / 2
                    // Keep the centre line plus six pixels on either side
                    // inside the native pointer target after scene rounding.
                    height: 13
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.SizeVerCursor
                    readonly property var fadeMenu: fadeCurveMenu
                    property bool gainMoved: false
                    property bool playOnRelease: false
                    function fadeSideAtOffset(offset) {
                        const eventFrames = Number(
                            eventDelegate.modelData.timelineEnd)
                            - Number(eventDelegate.modelData.timelineStart)
                        const fadeIn = volumeLine.displayedFadeIn
                        const fadeOut = volumeLine.displayedFadeOut
                        const inFade = fadeIn > 0 && offset < fadeIn
                        const outFade = fadeOut > 0
                            && offset >= eventFrames - fadeOut
                        if (inFade && outFade) {
                            const distanceToIn = offset
                            const distanceToOut = eventFrames - 1 - offset
                            return distanceToIn <= distanceToOut ? "in" : "out"
                        }
                        if (inFade) return "in"
                        if (outFade) return "out"
                        return ""
                    }
                    onPressed: function(mouse) {
                        const canvasPoint = mapToItem(
                            canvas, mouse.x, mouse.y)
                        const frame = canvas.frameAtCanvasPixel(canvasPoint.x)
                        if (mouse.button === Qt.RightButton) {
                            if ((mouse.modifiers & Qt.ControlModifier) !== 0
                                    || canvas.controlModifierHeld) {
                                canvas.selectWholeEvent(
                                    eventDelegate.modelData.id)
                                mouse.accepted = true
                                return
                            }
                            if (canvas.selectionContains(frame)) {
                                AudioEditorController.clearSelection()
                                mouse.accepted = true
                                return
                            }
                            const offset = Math.max(0, Math.min(
                                Number(eventDelegate.modelData.timelineEnd)
                                    - Number(eventDelegate.modelData.timelineStart) - 1,
                                frame - Number(eventDelegate.modelData.timelineStart)))
                            const fadeSide = fadeSideAtOffset(offset)
                            if (fadeSide.length === 0) {
                                mouse.accepted = false
                                return
                            }
                            fadeCurveMenu.eventId = eventDelegate.modelData.id
                            fadeCurveMenu.fadeSide = fadeSide
                            fadeCurveMenu.popup()
                            mouse.accepted = true
                            return
                        }
                        const outsideSelection = !canvas.selectionContains(frame)
                        playOnRelease = canvas.displayedSelectionEnd
                            > canvas.displayedSelectionStart && outsideSelection
                        if (outsideSelection)
                            AudioEditorController.setLoopEnabled(false)
                        // Defer the seek until the composed pointer sequence
                        // completes, otherwise documentChanged can recreate
                        // this delegate between the two native clicks.
                        Qt.callLater(function() {
                            AudioEditorController.seekFrame(frame)
                        })
                        volumeLine.gainCandidate = Number(
                            eventDelegate.modelData.gain)
                        gainMoved = false
                        mouse.accepted = true
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed
                                || (pressedButtons & Qt.LeftButton) === 0)
                            return
                        const point = mapToItem(volumeLine, mouse.x, mouse.y)
                        const nextGain = canvas.gainFromY(
                            point.y, volumeLine.height)
                        if (Math.abs(nextGain - volumeLine.gainCandidate) > 0.000001)
                            gainMoved = true
                        volumeLine.gainCandidate = nextGain
                        combinedGainCurve.requestPaint()
                    }
                    onReleased: function(mouse) {
                        if (mouse.button !== Qt.LeftButton) return
                        if (gainMoved)
                            AudioEditorController.setEventGain(
                                eventDelegate.modelData.id,
                                volumeLine.gainCandidate)
                        if (playOnRelease && !gainMoved
                                && !AudioEditorController.playing)
                            AudioEditorController.playPause()
                        playOnRelease = false
                        gainMoved = false
                        combinedGainCurve.requestPaint()
                    }
                    onCanceled: {
                        volumeLine.gainCandidate = Number(
                            eventDelegate.modelData.gain)
                        playOnRelease = false
                        gainMoved = false
                        combinedGainCurve.requestPaint()
                    }
                    onDoubleClicked: function(mouse) {
                        if (mouse.button !== Qt.LeftButton) {
                            mouse.accepted = false
                            return
                        }
                        const point = mapToItem(canvas, mouse.x, mouse.y)
                        const linePoint = mapToItem(
                            volumeLine, mouse.x, mouse.y)
                        canvas.addEnvelopePointForEvent(
                            eventDelegate.modelData.id,
                            Number(eventDelegate.modelData.timelineStart),
                            Number(eventDelegate.modelData.timelineEnd),
                            point.x, linePoint.y, volumeLine.height,
                            volumeLine.relativeEnvelopeGainForComposite)
                        mouse.accepted = true
                    }
                }

                Repeater {
                    model: modelData.envelope || []
                    Item {
                        required property var modelData
                        objectName: "editorEnvelopePoint"
                        property bool dragging: false
                        property double candidateOffset: Number(modelData.offset)
                        property double candidateGain: Number(modelData.gain)
                        readonly property double displayedGain: dragging
                            ? candidateGain : Number(modelData.gain)
                        readonly property double displayedCompositeGain:
                            volumeLine.combinedGainAtOffset(
                                dragging ? candidateOffset
                                         : Number(modelData.offset))
                        width: 18; height: 18
                        x: canvas.pixelAtFrame(Number(eventDelegate.modelData.timelineStart)
                            + (dragging ? candidateOffset : Number(modelData.offset))) - eventDelegate.x
                            - volumeLine.x - width / 2
                        y: (1 - Math.max(0, Math.min(2,
                                displayedCompositeGain)) / 2)
                            * volumeLine.height
                            - height / 2
                        z: 2
                        Rectangle {
                            anchors.centerIn: parent
                            width: 8; height: 8; radius: 4
                            color: Theme.editorWaveform
                            border.color: Theme.textPrimary; border.width: 1
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            property double originalOffset: 0
                            property double originalGain: 1
                            onPressed: function(mouse) {
                                originalOffset = Number(parent.modelData.offset)
                                originalGain = Number(parent.modelData.gain)
                                if (mouse.button === Qt.RightButton) {
                                    if ((mouse.modifiers & Qt.ControlModifier) !== 0
                                            || canvas.controlModifierHeld) {
                                        canvas.selectWholeEvent(
                                            eventDelegate.modelData.id)
                                        mouse.accepted = true
                                        return
                                    }
                                    AudioEditorController.removeEnvelopePoint(
                                        eventDelegate.modelData.id,
                                        originalOffset)
                                } else {
                                    if (!AudioEditorController.beginEnvelopePointGesture(
                                            eventDelegate.modelData.id, originalOffset)) {
                                        mouse.accepted = false
                                        return
                                    }
                                    parent.dragging = true
                                    parent.candidateOffset = originalOffset
                                    parent.candidateGain = Number(parent.modelData.gain)
                                    volumeLine.envelopePreviewActive = true
                                    volumeLine.envelopePreviewOriginalOffset = originalOffset
                                    volumeLine.envelopePreviewOffset = originalOffset
                                    volumeLine.envelopePreviewGain = Number(parent.modelData.gain)
                                    volumeLine.requestCombinedGainPaint()
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
                                parent.candidateOffset = Math.round(offset)
                                const compositeGain = canvas.gainFromY(
                                    point.y, volumeLine.height)
                                parent.candidateGain = volumeLine
                                    .relativeEnvelopeGainForComposite(
                                        parent.candidateOffset, compositeGain)
                                volumeLine.envelopePreviewOffset = parent.candidateOffset
                                volumeLine.envelopePreviewGain = parent.candidateGain
                                volumeLine.requestCombinedGainPaint()
                            }
                            onReleased: function(mouse) {
                                if (mouse.button !== Qt.LeftButton) return
                                const candidateOffset = parent.candidateOffset
                                const candidateGain = parent.candidateGain
                                parent.dragging = false
                                volumeLine.envelopePreviewActive = false
                                volumeLine.requestCombinedGainPaint()
                                let committed = false
                                if (typeof AudioEditorController.commitEnvelopePointGesture
                                        === "function") {
                                    committed = AudioEditorController
                                        .commitEnvelopePointGesture(
                                            candidateOffset, candidateGain)
                                } else {
                                    committed = AudioEditorController
                                        .updateEnvelopePointGesture(
                                            candidateOffset, candidateGain)
                                        && AudioEditorController
                                            .endEnvelopePointGesture()
                                }
                                if (!committed) {
                                    parent.candidateOffset = originalOffset
                                    parent.candidateGain = originalGain
                                }
                            }
                            onCanceled: {
                                AudioEditorController.cancelEnvelopePointGesture()
                                parent.dragging = false
                                volumeLine.envelopePreviewActive = false
                                parent.candidateOffset = originalOffset
                                parent.candidateGain = originalGain
                                volumeLine.requestCombinedGainPaint()
                            }
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
        color: Theme.editorPlayhead
        visible: AudioEditorController.hasDocument
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
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        enabled: AudioEditorController.hasDocument
        z: 1
        property double pressFrame: 0
        property real lastPanX: 0
        property bool selecting: false
        property bool playOnRelease: false
        onPressed: function(mouse) {
            canvas.forceActiveFocus()
            playOnRelease = false
            if (mouse.button === Qt.MiddleButton) {
                lastPanX = mouse.x
                return
            }
            if (mouse.button === Qt.RightButton) {
                canvas.cancelSelectionPreview()
                if ((mouse.modifiers & Qt.ControlModifier) !== 0
                        || canvas.controlModifierHeld) {
                    canvas.selectWholeEvent(canvas.eventIdAtFrame(
                        canvas.frameAtCanvasPixel(mouse.x)))
                    return
                }
                if (canvas.selectionContains(
                        canvas.frameAtCanvasPixel(mouse.x))) {
                    AudioEditorController.clearSelection()
                }
                return
            }
            pressFrame = canvas.frameAtCanvasPixel(mouse.x)
            selecting = false
            canvas.cancelSelectionPreview()
            const outsideSelection = !canvas.selectionContains(pressFrame)
            playOnRelease = canvas.displayedSelectionEnd
                > canvas.displayedSelectionStart && outsideSelection
            if (outsideSelection)
                AudioEditorController.setLoopEnabled(false)
            AudioEditorController.seekFrame(pressFrame)
        }
        onDoubleClicked: function(mouse) {
            if (mouse.button === Qt.RightButton
                    && ((mouse.modifiers & Qt.ControlModifier) !== 0
                        || canvas.controlModifierHeld)) {
                AudioEditorController.clearSelection()
                AudioEditorController.clearEventSelection()
                mouse.accepted = true
            }
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            if ((pressedButtons & Qt.MiddleButton) !== 0) {
                AudioEditorController.viewport.panByPixels(mouse.x - lastPanX)
                lastPanX = mouse.x
                return
            }
            if ((pressedButtons & Qt.LeftButton) === 0)
                return
            const frame = canvas.frameAtCanvasPixel(mouse.x)
            if (frame === pressFrame) return
            selecting = true
            canvas.previewSelection(pressFrame, frame)
        }
        onReleased: {
            if (selecting) canvas.commitSelection()
            else if (playOnRelease && !AudioEditorController.playing)
                AudioEditorController.playPause()
            selecting = false
            playOnRelease = false
        }
        onCanceled: {
            canvas.cancelSelectionPreview()
            selecting = false
            playOnRelease = false
        }
        onWheel: function(wheel) {
            if ((wheel.modifiers & Qt.ControlModifier) !== 0) {
                let anchor = wheel.x
                if (canvas.displayedSelectionEnd
                        > canvas.displayedSelectionStart) {
                    const midpoint = (canvas.displayedSelectionStart
                        + canvas.displayedSelectionEnd) / 2
                    anchor = Math.max(0, Math.min(canvas.width,
                        canvas.pixelAtFrame(midpoint)))
                }
                AudioEditorController.viewport.zoomAt(
                    wheel.angleDelta.y > 0 ? 1.25 : 0.8, anchor)
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
        text: qsTr("导入音频后开始编辑")
        color: Theme.textTertiary
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeBody
    }
}
