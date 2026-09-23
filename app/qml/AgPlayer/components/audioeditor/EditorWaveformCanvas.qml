import QtQuick
import QtQuick.Controls
import AgPlayer

Rectangle {
    id: canvas
    color: Theme.editorCanvas
    clip: true
    focus: true
    property bool interactive: true
    property real rowHeight: 94
    property double selectionCandidateStart: -1
    property double selectionCandidateEnd: -1
    property int selectionCandidateTrack: -1
    readonly property int displayedSelectionTrack: selectionCandidateStart >= 0
        ? selectionCandidateTrack : AudioEditorController.selectionTrack
    property int peakRevision: 0
    readonly property var events: AudioEditorController.timelineEventViews
    readonly property bool anySolo: AudioEditorController.tracks.some(function(track) { return track.solo })
    readonly property double displayedSelectionStart: selectionCandidateStart >= 0
        ? selectionCandidateStart : AudioEditorController.selectionStart
    readonly property double displayedSelectionEnd: selectionCandidateEnd >= 0
        ? selectionCandidateEnd : AudioEditorController.selectionEnd
    readonly property double displayedPlayheadFrame: AudioEditorController.playheadFrame

    function boundedPixel(pixel) { return Math.max(0, Math.min(width, pixel)) }
    // Contour uses the player's average-absolute/RMS statistics, not peak
    // normalization or a reduced vertical scale.
    function readablePeaks(channels, displayScale = 1) {
        return channels.map(function(channel) {
            return channel.map(function(value) {
                return value === null ? null : isFinite(value) ? Math.max(-1, Math.min(1, value * displayScale)) : 0
            })
        })
    }
    function frameAtCanvasPixel(pixel) {
        if (AudioEditorController.viewport.visibleFrameCount <= 0)
            return Math.round(boundedPixel(pixel) / Math.max(1, width) * 48000 * 30)
        return AudioEditorController.viewport.frameAtPixel(boundedPixel(pixel))
    }
    function pixelAtFrame(frame) {
        // Reading these Q_PROPERTY values keeps clip geometry reactive when the
        // viewport is assigned after delegate creation or resized/zoomed.
        const viewport = AudioEditorController.viewport
        return (Number(frame) - viewport.visibleStartFrame) * canvas.width / Math.max(1, viewport.visibleFrameCount)
    }
    function trackAtY(y) { return Math.max(0, Math.min(5, Math.floor(y / rowHeight))) }
    function eventAt(frame, track) {
        for (let i = events.length - 1; i >= 0; --i) {
            const event = events[i]
            if (Number(event.trackIndex) === track && frame >= Number(event.timelineStart)
                    && frame < Number(event.timelineEnd)) return event
        }
        return null
    }
    function eventById(id) {
        for (let i = 0; i < events.length; ++i) if (String(events[i].id) === String(id)) return events[i]
        return null
    }
    function edgeEventAt(x, track) {
        for (let i = 0; i < events.length; ++i) {
            const event = events[i]
            if (Number(event.trackIndex) === track
                    && (Math.abs(x - pixelAtFrame(Number(event.timelineStart))) <= 8
                        || Math.abs(x - pixelAtFrame(Number(event.timelineEnd))) <= 8)) return event
        }
        return null
    }
    function hasVolumeLine(event) {
        return event && ((event.envelope || []).length > 0
            || Number(event.gain) !== 1 || event.fadeIn > 0 || event.fadeOut > 0)
    }
    function eventIdAtFrame(frame) {
        const event = eventAt(frame, AudioEditorController.selectedTrack)
        return event ? String(event.id) : ""
    }
    function selectionContains(frame) {
        return displayedSelectionStart >= 0 && frame >= displayedSelectionStart && frame < displayedSelectionEnd
    }
    function selectWholeEvent(id) {
        if (!id) return false
        AudioEditorController.clearSelection()
        AudioEditorController.selectEvent(String(id))
        return true
    }
    function previewSelection(first, second) {
        selectionCandidateStart = Math.min(first, second)
        selectionCandidateEnd = Math.max(first, second)
    }
    function commitSelection() {
        if (selectionCandidateEnd > selectionCandidateStart)
            AudioEditorController.setSelection(selectionCandidateStart, selectionCandidateEnd, selectionCandidateTrack)
        cancelSelectionPreview()
    }
    function cancelSelectionPreview() { selectionCandidateStart = -1; selectionCandidateEnd = -1 }
    function envelopeGainAtOffset(points, offset) {
        let previous = {offset: 0, gain: 1}
        for (let i = 0; i < points.length; ++i) {
            const point = points[i]
            if (offset <= Number(point.offset)) {
                const span = Number(point.offset) - Number(previous.offset)
                return span === 0 ? Number(point.gain) : Number(previous.gain)
                    + (Number(point.gain) - Number(previous.gain)) * (offset - Number(previous.offset)) / span
            }
            previous = point
        }
        return Number(previous.gain)
    }
    function fadeGain(t, curve) {
        t = Math.max(0, Math.min(1, t))
        return curve === "linear" ? t : curve === "exponential"
            ? (Math.exp(5 * t) - 1) / (Math.exp(5) - 1) : t * t * (3 - 2 * t)
    }
    function fadeAt(event, offset) {
        const frames = Number(event.timelineEnd) - Number(event.timelineStart)
        let gain = 1
        if (event.fadeIn > 0 && offset < event.fadeIn)
            gain *= fadeGain(event.fadeIn === 1 ? 0 : offset / (event.fadeIn - 1), event.fadeInCurve)
        if (event.fadeOut > 0 && offset >= frames - event.fadeOut)
            gain *= fadeGain(event.fadeOut === 1 ? 0 : (frames - 1 - offset) / (event.fadeOut - 1), event.fadeOutCurve)
        return gain
    }
    function amplitude(event, offset) {
        return Math.max(0, Math.min(2, Number(event.gain) * fadeAt(event, offset)
            * envelopeGainAtOffset(event.envelope || [], offset)))
    }
    function gainY(gain, track) { return track * rowHeight + 12 + (1 - Math.max(0, Math.min(2, gain)) / 2) * (rowHeight - 24) }
    function gainFromY(y, track) { return Math.max(0, Math.min(2, 2 * (1 - (y - track * rowHeight - 12) / (rowHeight - 24)))) }
    function pointAt(event, x, y) {
        if (!event) return null
        const points = event.envelope || []
        for (let i = points.length - 1; i >= 0; --i) {
            const point = points[i]
            const px = pixelAtFrame(Number(event.timelineStart) + Number(point.offset))
            const py = gainY(Number(event.gain) * fadeAt(event, Number(point.offset)) * Number(point.gain), Number(event.trackIndex))
            if (Math.abs(px - x) <= 7 && Math.abs(py - y) <= 7) return point
        }
        return null
    }
    function envelopeGainForY(event, offset, y) {
        const base = Number(event.gain) * fadeAt(event, offset)
        return base > 0.0001 ? Math.max(0, Math.min(2, gainFromY(y, Number(event.trackIndex)) / base))
                            : envelopeGainAtOffset(event.envelope || [], offset)
    }
    function syncViewportWaveformMetrics() {
        AudioEditorController.viewport.setViewportWidth(Math.max(1, width))
        AudioEditorController.setViewportWaveformDevicePixelRatio(Screen.devicePixelRatio)
    }
    function cancelGesture() {
        if (interaction.mode === "point") AudioEditorController.cancelEnvelopePointGesture()
        else if (interaction.mode === "gain") AudioEditorController.cancelEventGainGesture()
        else if (interaction.mode === "move" || interaction.mode === "trimLeft" || interaction.mode === "trimRight" || interaction.mode === "sharedBoundary")
            AudioEditorController.cancelEventGesture()
        else if (interaction.mode === "handoff") AudioEditorController.cancelSelectionHandoff()
        else if (interaction.mode === "scrub") AudioEditorController.cancelScrub()
        interaction.mode = ""
        cancelSelectionPreview()
    }
    onWidthChanged: syncViewportWaveformMetrics()
    onWindowChanged: syncViewportWaveformMetrics()
    Component.onCompleted: syncViewportWaveformMetrics()
    Keys.onEscapePressed: function(event) { cancelGesture(); event.accepted = true }
    Connections {
        target: AudioEditorController
        function onViewportChannelPeaksChanged() { canvas.peakRevision += 1 }
        function onWaveformChanged() { canvas.peakRevision += 1 }
    }

    Repeater {
        model: 6
        Rectangle {
            required property int index
            x: 0; y: index * canvas.rowHeight
            width: canvas.width; height: canvas.rowHeight
            color: AudioEditorController.selectedTrack === index ? Theme.surfaceElevated : "transparent"
            border.color: Theme.divider
            Rectangle { x: 0; y: parent.height / 2; width: parent.width; height: 1; color: Theme.divider; opacity: 0.65 }
            Repeater {
                model: 13
                Rectangle {
                    required property int index
                    x: index * canvas.width / 12; width: 1; height: parent.height
                    color: Theme.divider; opacity: 0.35
                }
            }
        }
    }

    Repeater {
        model: canvas.events
        Rectangle {
            id: clipItem
            required property var modelData
            objectName: "editorClip_" + modelData.id
            readonly property real rawStart: canvas.pixelAtFrame(Number(modelData.timelineStart))
            readonly property real rawEnd: canvas.pixelAtFrame(Number(modelData.timelineEnd))
            readonly property color trackColor: AudioEditorController.tracks[Number(modelData.trackIndex)].color
            readonly property bool selected: String(modelData.id) === AudioEditorController.selectedEventId
            x: Math.max(0, rawStart)
            y: Number(modelData.trackIndex) * canvas.rowHeight
            width: Math.max(0, Math.min(canvas.width, rawEnd) - x)
            height: canvas.rowHeight
            visible: width > 0
            color: selected ? Qt.rgba(trackColor.r, trackColor.g, trackColor.b, 0.10) : "transparent"
            border.width: 0
            clip: true
            opacity: modelData.mute || AudioEditorController.tracks[Number(modelData.trackIndex)].muted
                || canvas.anySolo && !AudioEditorController.tracks[Number(modelData.trackIndex)].solo ? 0.4 : 1
            Rectangle {
                objectName: "editorTrimLeft_" + clipItem.modelData.id
                visible: clipItem.selected && clipItem.rawStart >= 0
                width: 12; height: 30; y: (parent.height - height) / 2
                radius: 3; color: Theme.surfaceElevated; border.color: Theme.focus; z: 2
                Rectangle { anchors.centerIn: parent; width: 2; height: 16; color: Theme.focus }
            }
            Rectangle {
                objectName: "editorTrimRight_" + clipItem.modelData.id
                visible: clipItem.selected && clipItem.rawEnd <= canvas.width
                x: parent.width - width; width: 12; height: 30; y: (parent.height - height) / 2
                radius: 3; color: Theme.surfaceElevated; border.color: Theme.focus; z: 2
                Rectangle { anchors.centerIn: parent; width: 2; height: 16; color: Theme.focus }
            }
            AudioEditorWaveformItem {
                objectName: "editorWaveformGeometry_" + clipItem.modelData.id
                anchors.fill: parent; anchors.topMargin: 22; anchors.bottomMargin: 3
                waveformColor: clipItem.trackColor
                density: 2; lineWidth: 1
                channelPeaks: {
                    canvas.peakRevision
                    AudioEditorController.viewport.visibleStartFrame
                    AudioEditorController.viewport.visibleEndFrame
                    return canvas.readablePeaks(AudioEditorController.eventPeaks(String(clipItem.modelData.id),
                        Math.ceil(width * Screen.devicePixelRatio), sampleMode ? 0 : SettingsController.waveformPeakAlgorithm === 1 ? 2 : 1))
                }
                sampleMode: Number(clipItem.modelData.timelineEnd) - Number(clipItem.modelData.timelineStart) <= width * 2
            }
            Label {
                visible: true
                x: 7; y: 2; width: parent.width - 14; height: 21
                text: clipItem.modelData.name; font.pixelSize: 11; color: Theme.textPrimary; elide: Text.ElideMiddle
            }
            Canvas {
                id: automation
                visible: canvas.hasVolumeLine(clipItem.modelData)
                anchors.fill: parent
                opacity: clipItem.selected || clipItem.modelData.envelope.length > 0 ? 0.95 : 0.28
                onPaint: {
                    const context = getContext("2d")
                    context.clearRect(0, 0, width, height)
                    context.lineWidth = 1.5
                    context.strokeStyle = Theme.textPrimary
                    context.beginPath()
                    const samples = Math.max(2, Math.min(1024, Math.ceil(width / 4)))
                    for (let i = 0; i <= samples; ++i) {
                        const px = width * i / samples
                        const offset = Math.max(0, Math.min(Number(clipItem.modelData.timelineEnd) - Number(clipItem.modelData.timelineStart) - 1,
                            canvas.frameAtCanvasPixel(clipItem.x + px) - Number(clipItem.modelData.timelineStart)))
                        const py = canvas.gainY(canvas.amplitude(clipItem.modelData, offset), 0)
                        if (i === 0) context.moveTo(px, py)
                        else context.lineTo(px, py)
                    }
                    context.stroke()
                    const points = clipItem.modelData.envelope || []
                    context.fillStyle = Theme.textPrimary
                    for (let p = 0; p < points.length; ++p) {
                        const point = points[p]
                        const px = canvas.pixelAtFrame(Number(clipItem.modelData.timelineStart) + Number(point.offset)) - clipItem.x
                        const gain = Number(clipItem.modelData.gain) * canvas.fadeAt(clipItem.modelData, Number(point.offset)) * Number(point.gain)
                        context.beginPath()
                        context.arc(px, canvas.gainY(gain, 0), 3.5, 0, Math.PI * 2)
                        context.fill()
                    }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Component.onCompleted: requestPaint()
                Connections { target: Theme; function onTextPrimaryChanged() { automation.requestPaint() } }
            }
        }
    }

    Rectangle {
        id: selectionOverlay
        objectName: "editorSelectionOverlay"
        x: Math.max(0, canvas.pixelAtFrame(canvas.displayedSelectionStart))
        width: Math.max(0, Math.min(canvas.width, canvas.pixelAtFrame(canvas.displayedSelectionEnd)) - x)
        y: canvas.displayedSelectionTrack >= 0 ? canvas.displayedSelectionTrack * canvas.rowHeight : 0
        height: canvas.displayedSelectionTrack >= 0 ? canvas.rowHeight : canvas.height
        visible: canvas.displayedSelectionStart >= 0 && canvas.displayedSelectionEnd > canvas.displayedSelectionStart
        color: Theme.editorSelection
        border.color: Theme.focus
        z: 3
        Rectangle {
            objectName: "editorSelectionStartHandle"
            x: 0; y: (parent.height - height) / 2; width: 12; height: 32
            radius: 3; color: Theme.surfaceElevated; border.color: Theme.focus
            Rectangle { anchors.centerIn: parent; width: 2; height: 18; color: Theme.focus }
        }
        Rectangle {
            objectName: "editorSelectionEndHandle"
            x: parent.width - width; y: (parent.height - height) / 2; width: 12; height: 32
            radius: 3; color: Theme.surfaceElevated; border.color: Theme.focus
            Rectangle { anchors.centerIn: parent; width: 2; height: 18; color: Theme.focus }
        }
    }
    Rectangle {
        id: recordingPreview
        objectName: "editorLiveRecordingWaveform"
        readonly property var recorder: AudioEditorController.recorder
        readonly property double startFrame: AudioEditorController.recordingStartFrame
        readonly property double endFrame: startFrame + recorder.recordedFrames
        readonly property double visibleStart: Math.max(startFrame, AudioEditorController.viewport.visibleStartFrame)
        readonly property double visibleEnd: Math.min(endFrame, AudioEditorController.viewport.visibleEndFrame)
        visible: AudioEditorController.recordingTrack >= 0 && visibleEnd > visibleStart
        x: canvas.pixelAtFrame(visibleStart)
        y: Math.max(0, AudioEditorController.recordingTrack) * canvas.rowHeight
        width: Math.max(0, canvas.pixelAtFrame(visibleEnd) - x)
        height: canvas.rowHeight
        color: Theme.editorCanvas; z: 2
        AudioEditorWaveformItem {
            anchors.fill: parent; anchors.topMargin: 22; anchors.bottomMargin: 3
            waveformColor: AudioEditorController.tracks[Math.max(0, AudioEditorController.recordingTrack)].color
            channelPeaks: {
                recordingPreview.recorder.recordedFrames
                return canvas.readablePeaks(recordingPreview.recorder.waveformPeaks(recordingPreview.visibleStart - recordingPreview.startFrame,
                    recordingPreview.visibleEnd - recordingPreview.startFrame, Math.ceil(width)), 4)
            }
        }
    }
    Rectangle {
        objectName: "editorPlayheadLine"
        x: canvas.pixelAtFrame(canvas.displayedPlayheadFrame)
        width: 2; height: canvas.height
        color: Theme.editorPlayhead
        visible: AudioEditorController.hasDocument
        z: 4
    }

    Rectangle {
        id: selectionCapsule
        objectName: "editorSelectionDragCapsule"
        visible: selectionOverlay.visible && canvas.interactive
        x: Math.max(4, Math.min(canvas.width - width - 4, selectionOverlay.x))
        y: Math.max(0, canvas.displayedSelectionTrack) * canvas.rowHeight + 3
        width: Math.min(canvas.width - 8, capsuleText.implicitWidth + 20)
        height: 26; radius: 13; color: Theme.surfaceElevated
        border.color: Theme.accent; z: 20
        function time(frame) {
            const seconds = Math.max(0, frame) / Math.max(1, AudioEditorController.sampleRate)
            return Math.floor(seconds / 60) + ":" + (seconds % 60).toFixed(3).padStart(6, "0")
        }
        Label {
            id: capsuleText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 16)
            elide: Text.ElideRight; color: Theme.textPrimary
            text: qsTr("拖出片段") + "  " + selectionCapsule.time(canvas.displayedSelectionStart)
                + " – " + selectionCapsule.time(canvas.displayedSelectionEnd)
        }
        MouseArea {
            anchors.fill: parent; preventStealing: true; cursorShape: Qt.OpenHandCursor
            property bool handingOff: false
            onPressed: function(mouse) {
                canvas.forceActiveFocus()
                const p = mapToItem(canvas, mouse.x, mouse.y)
                handingOff = AudioEditorController.beginSelectionHandoff(p.x, p.y)
                if (handingOff) interaction.mode = "handoff"
            }
            onPositionChanged: function(mouse) {
                if (!pressed || !handingOff) return
                const p = mapToItem(canvas, mouse.x, mouse.y)
                AudioEditorController.updateSelectionHandoff(p.x, p.y)
            }
            onReleased: { if (handingOff) AudioEditorController.releaseSelectionHandoff(); handingOff = false; interaction.mode = "" }
            onCanceled: { AudioEditorController.cancelSelectionHandoff(); handingOff = false; interaction.mode = "" }
        }
    }

    // The mouse grab belongs to this fixed item, never a clip delegate. A
    // preview may replace every timelineEventViews delegate without ending it.
    MouseArea {
        id: interaction
        objectName: "editorWaveformInteraction"
        anchors.fill: parent
        z: 10
        enabled: canvas.interactive && !AudioEditorController.busy
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        hoverEnabled: true
        preventStealing: true
        property string mode: ""
        property var originalEvent: null
        property double pressFrame: 0
        property real pressX: 0
        property real pressY: 0
        property real lastPanX: 0
        property double grabOffset: 0
        property double originalPointOffset: 0
        property double candidatePointOffset: 0
        property double candidatePointGain: 1
        readonly property var hoveredEdge: canvas.edgeEventAt(mouseX, canvas.trackAtY(mouseY))
        cursorShape: mode === "move" || mode === "pan" ? Qt.ClosedHandCursor : mode === "point" || mode === "gain"
            ? Qt.SizeVerCursor : mode === "trimLeft" || mode === "trimRight" || mode === "sharedBoundary" || hoveredEdge ? Qt.SizeHorCursor : Qt.ArrowCursor

        onPressed: function(mouse) {
            canvas.forceActiveFocus()
            pressX = mouse.x; pressY = mouse.y
            pressFrame = canvas.frameAtCanvasPixel(mouse.x)
            if (mouse.button === Qt.MiddleButton || mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ControlModifier)) {
                mode = "pan"; lastPanX = mouse.x; return
            }
            const track = canvas.trackAtY(mouse.y)
            AudioEditorController.selectedTrack = track
            const selected = canvas.eventById(AudioEditorController.selectedEventId)
            const selectedEdge = selected && Number(selected.trackIndex) === track
                && mouse.y - track * canvas.rowHeight >= 24
                && (Math.abs(mouse.x - canvas.pixelAtFrame(Number(selected.timelineStart))) <= 8
                    || Math.abs(mouse.x - canvas.pixelAtFrame(Number(selected.timelineEnd))) <= 8)
            originalEvent = selectedEdge && mouse.button === Qt.LeftButton ? selected
                : canvas.eventAt(pressFrame, track) || canvas.edgeEventAt(mouse.x, track)
            mode = ""
            const point = canvas.pointAt(originalEvent, mouse.x, mouse.y)
            if (mouse.button === Qt.RightButton) {
                if (originalEvent) AudioEditorController.selectEvent(String(originalEvent.id))
                else AudioEditorController.clearEventSelection()
                contextMenu.eventId = originalEvent ? String(originalEvent.id) : ""
                contextMenu.pointOffset = point ? Number(point.offset) : -1
                contextMenu.timelineFrame = pressFrame
                contextMenu.onVolumeLine = originalEvent && canvas.hasVolumeLine(originalEvent)
                    && Math.abs(mouse.y - canvas.gainY(canvas.amplitude(originalEvent,
                        pressFrame - Number(originalEvent.timelineStart)), track)) <= 7
                mode = "rightSelection"
                canvas.cancelSelectionPreview()
                canvas.selectionCandidateTrack = track
                return
            }
            if (!AudioEditorController.hasDocument) return
            if (!originalEvent) AudioEditorController.clearEventSelection()
            if (point && AudioEditorController.beginEnvelopePointGesture(String(originalEvent.id), Number(point.offset))) {
                AudioEditorController.selectEvent(String(originalEvent.id))
                originalPointOffset = Number(point.offset)
                candidatePointOffset = originalPointOffset; candidatePointGain = Number(point.gain)
                mode = "point"; return
            }
            const localY = mouse.y - track * canvas.rowHeight
            if (originalEvent) {
                const left = canvas.pixelAtFrame(Number(originalEvent.timelineStart))
                const right = canvas.pixelAtFrame(Number(originalEvent.timelineEnd))
                if ((Math.abs(mouse.x - left) <= 8 || Math.abs(mouse.x - right) <= 8) && localY >= 24) {
                    if (AudioEditorController.beginEventGesture(String(originalEvent.id), "trim")) {
                        AudioEditorController.selectEvent(String(originalEvent.id))
                        mode = Math.abs(mouse.x - left) <= 8 ? "trimLeft" : "trimRight"
                        return
                    }
                }
                if (AudioEditorController.activeTool === "scissors") {
                    AudioEditorController.splitEvent(String(originalEvent.id), pressFrame)
                    return
                }
                const gainY = canvas.gainY(canvas.amplitude(originalEvent, pressFrame - Number(originalEvent.timelineStart)), track)
                if (canvas.hasVolumeLine(originalEvent) && Math.abs(mouse.y - gainY) <= 5 && AudioEditorController.beginEventGainGesture(String(originalEvent.id))) {
                    AudioEditorController.selectEvent(String(originalEvent.id))
                    mode = "gain"; return
                }
            }
            if (canvas.displayedSelectionEnd > canvas.displayedSelectionStart
                    && (canvas.displayedSelectionTrack < 0 || canvas.displayedSelectionTrack === track)) {
                canvas.selectionCandidateTrack = canvas.displayedSelectionTrack
                if (Math.abs(mouse.x - canvas.pixelAtFrame(canvas.displayedSelectionStart)) <= 8) {
                    mode = "selectionStart"; canvas.previewSelection(canvas.displayedSelectionStart, canvas.displayedSelectionEnd); return
                }
                if (Math.abs(mouse.x - canvas.pixelAtFrame(canvas.displayedSelectionEnd)) <= 8) {
                    mode = "selectionEnd"; canvas.previewSelection(canvas.displayedSelectionStart, canvas.displayedSelectionEnd); return
                }
            }
            if (originalEvent && AudioEditorController.beginEventGesture(String(originalEvent.id), "move")) {
                canvas.selectWholeEvent(String(originalEvent.id))
                grabOffset = pressFrame - Number(originalEvent.timelineStart)
                mode = "move"; return
            }
            AudioEditorController.seekFrame(pressFrame)
            canvas.cancelSelectionPreview()
            mode = ""
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const frame = canvas.frameAtCanvasPixel(mouse.x)
            if (mode === "pan") { AudioEditorController.viewport.panByPixels(lastPanX - mouse.x); lastPanX = mouse.x }
            else if (mode === "scrub") AudioEditorController.previewScrub(frame)
            else if (mode === "move") AudioEditorController.moveEventToTrack(String(originalEvent.id), Math.max(0, frame - grabOffset), canvas.trackAtY(mouse.y))
            else if (mode === "point") {
                candidatePointOffset = Math.max(0, Math.min(Number(originalEvent.timelineEnd) - Number(originalEvent.timelineStart) - 1,
                    Math.round(frame - Number(originalEvent.timelineStart))))
                candidatePointGain = canvas.envelopeGainForY(originalEvent, candidatePointOffset, mouse.y)
                AudioEditorController.updateEnvelopePointGesture(candidatePointOffset, candidatePointGain)
            } else if (mode === "gain") {
                const offset = Math.max(0, frame - Number(originalEvent.timelineStart))
                const base = canvas.fadeAt(originalEvent, offset) * canvas.envelopeGainAtOffset(originalEvent.envelope || [], offset)
                if (base > 0.0001) AudioEditorController.updateEventGainGesture(Math.min(2, canvas.gainFromY(mouse.y, Number(originalEvent.trackIndex)) / base))
            } else if (mode === "trimLeft" || mode === "trimRight") {
                const sourceRate = Number(originalEvent.sourceSampleRate || AudioEditorController.sampleRate)
                const projectRate = Number(originalEvent.projectSampleRate || AudioEditorController.sampleRate)
                const speed = Number(originalEvent.speedRatio || 1)
                const ratio = sourceRate * speed / Math.max(1, projectRate)
                let start = Number(originalEvent.sourceStart), end = Number(originalEvent.sourceEnd)
                let timeline = Number(originalEvent.timelineStart)
                if (mode === "trimLeft") {
                    start = Math.max(0, Math.min(end - 1, start + Math.round((frame - timeline) * ratio)))
                    timeline += Math.round((start - Number(originalEvent.sourceStart)) / ratio)
                    if (timeline < 0) return
                } else {
                    const maximum = Number(originalEvent.sourceTotalFrames || originalEvent.sourceEnd)
                    end = Math.max(start + 1, Math.min(maximum,
                        Number(originalEvent.sourceEnd) + Math.round((frame - Number(originalEvent.timelineEnd)) * ratio)))
                }
                AudioEditorController.trimEvent(String(originalEvent.id), start, end, timeline)
            } else if (mode === "rightSelection" && Math.abs(mouse.x - pressX) >= 3) canvas.previewSelection(pressFrame, frame)
            else if (mode === "selectionStart") canvas.selectionCandidateStart = Math.min(frame, canvas.selectionCandidateEnd - 1)
            else if (mode === "selectionEnd") canvas.selectionCandidateEnd = Math.max(frame, canvas.selectionCandidateStart + 1)
            else if (mode === "handoff") AudioEditorController.updateSelectionHandoff(mouse.x, mouse.y)
        }
        onReleased: {
            const finishedMode = mode
            mode = ""
            if (finishedMode === "move" || finishedMode === "trimLeft" || finishedMode === "trimRight" || finishedMode === "sharedBoundary") AudioEditorController.endEventGesture()
            else if (finishedMode === "point") AudioEditorController.commitEnvelopePointGesture(candidatePointOffset, candidatePointGain)
            else if (finishedMode === "gain") AudioEditorController.endEventGainGesture()
            else if (finishedMode === "rightSelection") {
                if (canvas.selectionCandidateEnd > canvas.selectionCandidateStart) canvas.commitSelection()
                else if (AudioEditorController.selectionEnd > AudioEditorController.selectionStart) {
                    AudioEditorController.clearSelection()
                    canvas.cancelSelectionPreview()
                } else contextMenu.popup(pressX, pressY)
            }
            else if (finishedMode === "selectionStart" || finishedMode === "selectionEnd") canvas.commitSelection()
            else if (finishedMode === "handoff") AudioEditorController.releaseSelectionHandoff()
            else if (finishedMode === "scrub") AudioEditorController.endScrub()
        }
        onCanceled: canvas.cancelGesture()
        onDoubleClicked: function(mouse) {
            if (mouse.button === Qt.RightButton && mouse.modifiers & Qt.ControlModifier) {
                canvas.cancelGesture(); AudioEditorController.clearEventSelection(); AudioEditorController.clearSelection(); return
            }
            if (mouse.button !== Qt.LeftButton) return
            if (mouse.modifiers & Qt.ControlModifier) return
            const event = canvas.eventAt(canvas.frameAtCanvasPixel(mouse.x), canvas.trackAtY(mouse.y))
            if (!event || canvas.pointAt(event, mouse.x, mouse.y)) return
            const offset = Math.max(0, Math.min(Number(event.timelineEnd) - Number(event.timelineStart) - 1,
                canvas.frameAtCanvasPixel(mouse.x) - Number(event.timelineStart)))
            const py = canvas.gainY(canvas.amplitude(event, offset), Number(event.trackIndex))
            if (Math.abs(mouse.y - py) <= 7) {
                canvas.cancelGesture()
                AudioEditorController.addEnvelopePoint(String(event.id), Math.round(offset), canvas.envelopeGainForY(event, offset, mouse.y))
            }
        }
        onWheel: function(wheel) {
            if (wheel.modifiers & Qt.ControlModifier) {
                AudioEditorController.viewport.panByPixels(-(wheel.angleDelta.x || wheel.angleDelta.y) / 3)
                wheel.accepted = true
            } else {
                AudioEditorController.viewport.zoomAt(wheel.angleDelta.y > 0 ? 1.25 : 0.8, wheel.x)
                wheel.accepted = true
            }
        }
    }

    component EditorMenu: Menu {
        id: menu
        delegate: ThemedMenuItem {}
        width: {
            let widest = 0
            for (let i = 0; i < count; ++i) {
                const entry = itemAt(i)
                if (entry && entry.visible && entry.text !== undefined) widest = Math.max(widest, entry.implicitWidth)
            }
            return widest + leftPadding + rightPadding
        }
        padding: 5
        background: Rectangle { color: Theme.surfaceElevated; border.color: Theme.borderStrong; radius: Theme.radiusSm }
    }
    EditorMenu {
        id: contextMenu
        objectName: "editorClipContextMenu"
        property string eventId: ""
        property double pointOffset: -1
        property double timelineFrame: 0
        property bool onVolumeLine: false
        ThemedMenuItem {
            objectName: "editorAddVolumeLine"
            text: qsTr("添加音量线")
            enabled: contextMenu.eventId.length > 0 && !canvas.hasVolumeLine(canvas.eventById(contextMenu.eventId))
            onTriggered: AudioEditorController.addEnvelopePoint(contextMenu.eventId, 0, 1)
        }
        ThemedMenuItem {
            objectName: "editorAddVolumePoint"
            text: qsTr("添加控制点")
            enabled: contextMenu.onVolumeLine && contextMenu.pointOffset < 0
                && canvas.eventById(contextMenu.eventId) !== null
            onTriggered: {
                const event = canvas.eventById(contextMenu.eventId)
                if (!event) return
                const offset = Math.max(0, Math.min(Number(event.timelineEnd) - Number(event.timelineStart) - 1,
                    Math.round(contextMenu.timelineFrame - Number(event.timelineStart))))
                AudioEditorController.addEnvelopePoint(contextMenu.eventId, offset,
                    canvas.envelopeGainAtOffset(event.envelope || [], offset))
            }
        }
        ThemedMenuItem {
            text: qsTr("删除音量控制点"); visible: contextMenu.pointOffset >= 0
            height: visible ? implicitHeight : 0
            onTriggered: AudioEditorController.removeEnvelopePoint(contextMenu.eventId, contextMenu.pointOffset)
        }
        ThemedMenuItem {
            text: qsTr("在此分割"); enabled: contextMenu.eventId.length > 0
            onTriggered: AudioEditorController.splitEvent(contextMenu.eventId, contextMenu.timelineFrame)
        }
        ThemedMenuItem {
            text: qsTr("复制片段"); enabled: contextMenu.eventId.length > 0
            onTriggered: { canvas.selectWholeEvent(contextMenu.eventId); AudioEditorController.triggerAction("editor.copy") }
        }
        ThemedMenuItem {
            text: qsTr("剪切片段"); enabled: contextMenu.eventId.length > 0
            onTriggered: { canvas.selectWholeEvent(contextMenu.eventId); AudioEditorController.triggerAction("editor.cut") }
        }
        ThemedMenuItem {
            text: qsTr("粘贴到当前轨道"); enabled: AudioEditorController.actionEnabled("editor.paste")
            onTriggered: { AudioEditorController.seekFrame(contextMenu.timelineFrame); AudioEditorController.triggerAction("editor.paste") }
        }
        ThemedMenuItem {
            text: qsTr("删除片段"); enabled: contextMenu.eventId.length > 0
            onTriggered: { canvas.selectWholeEvent(contextMenu.eventId); AudioEditorController.triggerAction("editor.deleteSelection") }
        }
        MenuSeparator {}
        ThemedMenuItem {
            text: qsTr("淡入"); enabled: contextMenu.eventId.length > 0
            onTriggered: {
                const event = canvas.eventById(contextMenu.eventId)
                if (event) AudioEditorController.setEventFadeIn(contextMenu.eventId,
                    Math.max(1, Math.min(AudioEditorController.sampleRate,
                        Math.floor((Number(event.timelineEnd) - Number(event.timelineStart) - Number(event.fadeOut)) / 4))))
            }
        }
        ThemedMenuItem {
            text: qsTr("淡出"); enabled: contextMenu.eventId.length > 0
            onTriggered: {
                const event = canvas.eventById(contextMenu.eventId)
                if (event) AudioEditorController.setEventFadeOut(contextMenu.eventId,
                    Math.max(1, Math.min(AudioEditorController.sampleRate,
                        Math.floor((Number(event.timelineEnd) - Number(event.timelineStart) - Number(event.fadeIn)) / 4))))
            }
        }
        EditorMenu {
            title: qsTr("淡入曲线"); enabled: contextMenu.eventId.length > 0
            ThemedMenuItem { text: qsTr("线性"); onTriggered: AudioEditorController.setEventFadeCurve(contextMenu.eventId, true, "linear") }
            ThemedMenuItem { text: qsTr("平滑"); onTriggered: AudioEditorController.setEventFadeCurve(contextMenu.eventId, true, "smooth") }
            ThemedMenuItem { text: qsTr("指数"); onTriggered: AudioEditorController.setEventFadeCurve(contextMenu.eventId, true, "exponential") }
        }
        EditorMenu {
            title: qsTr("淡出曲线"); enabled: contextMenu.eventId.length > 0
            ThemedMenuItem { text: qsTr("线性"); onTriggered: AudioEditorController.setEventFadeCurve(contextMenu.eventId, false, "linear") }
            ThemedMenuItem { text: qsTr("平滑"); onTriggered: AudioEditorController.setEventFadeCurve(contextMenu.eventId, false, "smooth") }
            ThemedMenuItem { text: qsTr("指数"); onTriggered: AudioEditorController.setEventFadeCurve(contextMenu.eventId, false, "exponential") }
        }
    }
}
