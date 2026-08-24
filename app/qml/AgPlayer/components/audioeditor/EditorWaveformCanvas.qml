import QtQuick
import AgPlayer

Rectangle {
    id: canvas
    color: Theme.editorCanvas
    border.color: Theme.border
    border.width: 1
    clip: true
    property double playheadCandidateFrame: -1
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
    function frameTimeText(frame) {
        const sampleRate = Math.max(1, AudioEditorController.sampleRate)
        const milliseconds = Math.max(0, Math.round(frame * 1000 / sampleRate))
        const minutes = Math.floor(milliseconds / 60000)
        const seconds = Math.floor(milliseconds % 60000 / 1000)
        const millis = milliseconds % 1000
        return String(minutes).padStart(2, "0") + ":"
            + String(seconds).padStart(2, "0") + "."
            + String(millis).padStart(3, "0")
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
            color: Theme.border
            opacity: 0.7
        }
    }

    AudioEditorWaveformItem {
        id: waveform
        objectName: "editorWaveformGeometry"
        anchors.fill: parent
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        channelPeaks: AudioEditorController.viewportChannelPeaks
        waveformColor: SettingsController.waveformMode === 0
                       ? (SettingsController.waveformSolidBaseColor
                          || Theme.waveformMagenta)
                       : (SettingsController.waveformMode === 2
                          ? SettingsController.spectrumSolidColor
                          : (SettingsController.waveformRgbBaseColor
                             || Theme.waveformMagenta))
        density: SettingsController.waveformMode === 2
                 ? 1.0 : SettingsController.waveformDensity
        lineWidth: SettingsController.waveformMode === 2
                   ? 3.0 : SettingsController.waveformThickness
        sampleMode: AudioEditorController.viewport.visibleFrameCount
            <= Math.max(2, Math.floor(width) * 2)
        visible: AudioEditorController.hasDocument
            || AudioEditorController.recording
    }

    Rectangle {
        id: selectionOverlay
        objectName: "editorSelectionOverlay"
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
        color: Theme.editorSelection
        border.color: Theme.accent
        border.width: 1
        z: 3
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton) {
                    AudioEditorController.clearSelection()
                    mouse.accepted = true
                }
            }
        }
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
            border.color: Theme.accent
            border.width: 1
            z: 2

            MouseArea {
                id: eventMoveArea
                objectName: "editorEventBodyInteraction"
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                z: 5
                cursorShape: AudioEditorController.activeTool === "scissors"
                    ? Qt.CrossCursor : Qt.ArrowCursor
                acceptedButtons: Qt.LeftButton
                property real pressCanvasX: 0
                property double pressFrame: 0
                property double originalTimelineStart: 0
                property bool duplicateMove: false
                property bool movedDuringPress: false
                property double lastEnvelopeClickMs: 0
                property real lastEnvelopeClickX: 0
                property real lastEnvelopeClickY: 0
                onPressed: function(mouse) {
                    movedDuringPress = false
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
                        AudioEditorController.setSelection(
                            Math.min(pressFrame, frame),
                            Math.max(pressFrame, frame))
                    }
                }
                onReleased: function(mouse) {
                    if (duplicateMove) AudioEditorController.endEventGesture()
                    if (!duplicateMove && !movedDuringPress
                            && AudioEditorController.activeTool !== "scissors"
                            && mouse.y >= volumeLine.y
                            && mouse.y <= volumeLine.y + volumeLine.height) {
                        const now = Date.now()
                        if (now - lastEnvelopeClickMs <= 500
                                && Math.abs(mouse.x - lastEnvelopeClickX) <= 6
                                && Math.abs(mouse.y - lastEnvelopeClickY) <= 6) {
                            const point = mapToItem(canvas, mouse.x, mouse.y)
                            canvas.addEnvelopePointForEvent(
                                modelData.id,
                                Number(modelData.timelineStart),
                                Number(modelData.timelineEnd), point.x,
                                mouse.y - volumeLine.y, volumeLine.height)
                            lastEnvelopeClickMs = 0
                        } else {
                            lastEnvelopeClickMs = now
                            lastEnvelopeClickX = mouse.x
                            lastEnvelopeClickY = mouse.y
                        }
                    }
                    duplicateMove = false
                }
                onCanceled: {
                    if (duplicateMove) AudioEditorController.cancelEventGesture()
                    duplicateMove = false
                }
            }

            MouseArea {
                id: leftTrim
                objectName: "editorEventLeftTrimHandle"
                width: 24
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
                width: 24
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
                id: fadeInCurve
                objectName: "editorEventFadeInCurve"
                anchors.fill: parent
                z: 4
                visible: fadeInHandle.displayedFadeIn > 0
                onPaint: {
                    const context = getContext("2d")
                    context.clearRect(0, 0, width, height)
                    const endX = canvas.pixelAtFrame(
                        Number(modelData.timelineStart)
                            + fadeInHandle.displayedFadeIn)
                        - eventDelegate.x
                    context.strokeStyle = "#e9d7cf"
                    context.lineWidth = 1.2
                    context.beginPath()
                    context.moveTo(2, height - 10)
                    context.bezierCurveTo(8, height * 0.48,
                        Math.max(8, endX - 28), 18, Math.max(2, endX), 10)
                    context.stroke()
                }
                Connections {
                    target: AudioEditorController
                    function onDocumentChanged() { fadeInCurve.requestPaint() }
                }
            }

            MouseArea {
                id: fadeInHandle
                objectName: "editorEventFadeInHandle"
                z: 8
                x: Math.max(0, Math.min(eventDelegate.width - width,
                    canvas.pixelAtFrame(Number(modelData.timelineStart)
                        + Number(modelData.fadeIn)) - eventDelegate.x
                        - width / 2))
                y: 28
                width: 24; height: 24
                property double candidateFadeIn: Number(modelData.fadeIn)
                readonly property double displayedFadeIn: pressed
                    ? candidateFadeIn : Number(modelData.fadeIn)
                cursorShape: Qt.SizeHorCursor
                activeFocusOnTab: true
                Accessible.name: qsTr("淡入控制点")
                Accessible.role: Accessible.Slider
                onPressed: function(mouse) {
                    candidateFadeIn = Number(modelData.fadeIn)
                    forceActiveFocus()
                    mouse.accepted = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const point = mapToItem(canvas, mouse.x, mouse.y)
                    const eventFrames = Number(modelData.timelineEnd)
                        - Number(modelData.timelineStart)
                    candidateFadeIn = Math.round(Math.max(0,
                        Math.min(eventFrames - Number(modelData.fadeOut),
                            canvas.frameAtCanvasPixel(point.x)
                                - Number(modelData.timelineStart))))
                    fadeInCurve.requestPaint()
                }
                onReleased: AudioEditorController.setEventFadeIn(
                    modelData.id, candidateFadeIn)
                onCanceled: candidateFadeIn = Number(modelData.fadeIn)
                Rectangle {
                    anchors.centerIn: parent
                    width: 9; height: 9; radius: width / 2
                    color: Theme.accent
                    border.color: Theme.panel; border.width: 2
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
                x: Math.max(0, Math.min(eventDelegate.width - width,
                    canvas.pixelAtFrame(Number(modelData.timelineEnd)
                        - Number(modelData.fadeOut)) - eventDelegate.x
                        - width / 2))
                y: 28
                width: 24; height: 24
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
                    anchors.centerIn: parent
                    width: 9; height: 9; radius: width / 2
                    color: Theme.accent
                    border.color: Theme.panel; border.width: 2
                }
            }

            Item {
                id: volumeLine
                objectName: "editorEventVolumeLine"
                x: 10; width: parent.width - 20
                y: parent.height / 2 - 12; height: 24
                z: 4
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 1; color: Theme.secondaryText; opacity: 0.65
                }
                Repeater {
                    model: modelData.envelope || []
                    Rectangle {
                        required property var modelData
                        width: 8; height: 8; radius: 4
                        x: canvas.pixelAtFrame(Number(eventDelegate.modelData.timelineStart)
                            + Number(modelData.offset)) - eventDelegate.x
                            - volumeLine.x - width / 2
                        y: (1 - Math.max(0, Math.min(2,
                            Number(modelData.gain))) / 2) * volumeLine.height
                            - height / 2
                        color: Theme.accent
                        border.color: Theme.panel; border.width: 1
                    }
                }
            }
        }
    }

    Rectangle {
        id: playheadLine
        x: canvas.pixelAtFrame(canvas.displayedPlayheadFrame)
        y: 0
        width: 2
        height: canvas.height
        color: AudioEditorController.recording ? "#ff3d4f" : "#ffaf00"
        visible: (AudioEditorController.hasDocument
                  || AudioEditorController.recording)
            && canvas.displayedPlayheadFrame
                >= AudioEditorController.viewport.visibleStartFrame
            && canvas.displayedPlayheadFrame
                <= AudioEditorController.viewport.visibleEndFrame
        z: 6
    }

    Rectangle {
        id: playheadTimeCapsule
        objectName: "editorPlayheadTimeCapsule"
        property alias text: playheadTimeLabel.text
        visible: playheadLine.visible
        x: Math.max(4, Math.min(canvas.width - width - 4,
            playheadLine.x - width / 2))
        y: 4
        width: playheadTimeLabel.implicitWidth + 12
        height: 22
        radius: 11
        color: Theme.elevated
        border.color: Theme.accent
        z: 9
        Text {
            id: playheadTimeLabel
            anchors.centerIn: parent
            text: canvas.frameTimeText(canvas.displayedPlayheadFrame)
            color: Theme.primaryText
            font.pixelSize: 10
        }
    }

    Rectangle {
        id: selectionTimeCapsule
        objectName: "editorSelectionTimeCapsule"
        property alias text: selectionTimeLabel.text
        visible: selectionOverlay.visible
        x: Math.max(4, Math.min(canvas.width - width - 4, selectionOverlay.x + 6))
        y: 31
        width: selectionTimeLabel.implicitWidth + 12
        height: 22
        radius: 11
        color: Theme.elevated
        border.color: Theme.accent
        z: 9
        Text {
            id: selectionTimeLabel
            anchors.centerIn: parent
            text: canvas.frameTimeText(AudioEditorController.selectionStart)
                + " – "
                + canvas.frameTimeText(AudioEditorController.selectionEnd)
            color: Theme.primaryText
            font.pixelSize: 10
        }
    }

    Rectangle {
        id: selectionDurationCapsule
        objectName: "editorSelectionDurationCapsule"
        property alias text: selectionDurationLabel.text
        visible: selectionOverlay.visible
        x: Math.max(4, Math.min(canvas.width - width - 4,
            selectionOverlay.x + selectionOverlay.width / 2 - width / 2))
        y: canvas.height - height - 8
        width: selectionDurationLabel.implicitWidth + 12
        height: 22
        radius: 11
        color: Theme.activeSelection
        border.color: Theme.accent
        z: 9
        Text {
            id: selectionDurationLabel
            anchors.centerIn: parent
            text: qsTr("时长 %1").arg(canvas.frameTimeText(
                AudioEditorController.selectionFrames))
            color: Theme.activeSelectionText
            font.pixelSize: 10
        }
    }

    Rectangle {
        id: selectionDragCapsule
        objectName: "editorSelectionDragCapsule"
        property alias text: selectionDragLabel.text
        visible: selectionOverlay.visible
        x: Math.max(4, Math.min(canvas.width - width - 4, selectionOverlay.x + 6))
        y: canvas.height - height - 8
        width: selectionDragLabel.implicitWidth + 18
        height: 24
        radius: 12
        color: Theme.accent
        z: 10
        Text {
            id: selectionDragLabel
            anchors.centerIn: parent
            text: AudioEditorController.selectionDragReady
                ? qsTr("拖出片段 WAV") : qsTr("按住准备 WAV")
            color: "white"
            font.pixelSize: 10
        }
        MouseArea {
            objectName: "editorSelectionFileDragInteraction"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            property point pressPoint
            property bool dragStarted: false
            onPressed: function(mouse) {
                pressPoint = Qt.point(mouse.x, mouse.y)
                dragStarted = false
                if (!AudioEditorController.selectionDragReady)
                    AudioEditorController.prepareSelectionDrag()
                mouse.accepted = true
            }
            onPositionChanged: function(mouse) {
                if (!pressed || dragStarted
                    || !AudioEditorController.selectionDragReady) return
                const distance = Math.abs(mouse.x - pressPoint.x)
                    + Math.abs(mouse.y - pressPoint.y)
                if (distance < 8) return
                dragStarted = AudioEditorController.startSelectionFileDrag(
                    selectionDragCapsule)
            }
            onReleased: dragStarted = false
            onCanceled: dragStarted = false
        }
    }

    Text {
        objectName: "editorZoomGuide"
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.top: parent.top
        anchors.topMargin: 32
        text: qsTr("Ctrl+滚轮缩放 · Shift+滚轮平移")
        color: Theme.secondaryText
        font.pixelSize: 9
        z: 9
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
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const point = mapToItem(canvas, mouse.x, mouse.y)
            const frame = Math.min(canvas.frameAtCanvasPixel(point.x),
                                   AudioEditorController.selectionEnd - 1)
            AudioEditorController.setSelection(
                frame, AudioEditorController.selectionEnd)
        }
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
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const point = mapToItem(canvas, mouse.x, mouse.y)
            const frame = Math.max(canvas.frameAtCanvasPixel(point.x),
                                   AudioEditorController.selectionStart + 1)
            AudioEditorController.setSelection(
                AudioEditorController.selectionStart, frame)
        }
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
        color: Theme.secondaryText
        font.family: Theme.fontPrimary
        font.pixelSize: 14
    }
}
