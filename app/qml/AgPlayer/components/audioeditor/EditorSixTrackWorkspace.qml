import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Item {
    id: workspace
    signal importRequested()
    signal saveProjectRequested()
    readonly property var recorder: AudioEditorController.recorder
    readonly property bool recordingActive: recorder.state === 1 || recorder.state === 2 || recorder.state === 3
    readonly property real headerWidth: width < 800 ? 180 : 220
    readonly property bool shortLayout: height < 500
    readonly property bool compactTransport: width < 1000
    readonly property bool tinyTransport: width < 620
    readonly property rect rulerGeometry: Qt.rect(ruler.x, ruler.y, ruler.width, ruler.height)
    readonly property real trackHeight: Math.max(86, trackScroller.height / 6)
    property int actionRevision: 0

    function timeText(frame, millis) {
        const rate = AudioEditorController.sampleRate > 0 ? AudioEditorController.sampleRate : 48000
        const ms = Math.max(0, Math.round(Number(frame) * 1000 / rate))
        const hours = Math.floor(ms / 3600000)
        return (hours > 0 ? String(hours).padStart(2, "0") + ":" : "")
            + String(Math.floor(ms / 60000) % 60).padStart(2, "0") + ":"
            + String(Math.floor(ms / 1000) % 60).padStart(2, "0")
            + (millis ? "." + String(ms % 1000).padStart(3, "0") : "")
    }
    function cancelGesture() {
        waveformCanvas.cancelGesture()
        rulerInteraction.scrubbing = false
        AudioEditorController.cancelScrub()
    }
    Connections {
        target: AudioEditorController.actions
        function onDataChanged() { workspace.actionRevision += 1 }
    }

    Row {
        id: commandBar
        objectName: "editorCommandBar"
        x: workspace.shortLayout ? 8 : 14; y: workspace.shortLayout ? 6 : 12
        width: parent.width - x * 2; height: workspace.shortLayout ? 48 : workspace.height < 600 ? 64 : 82
        spacing: workspace.width < 800 ? 6 : 12
        Repeater {
            model: [
                {name: "importAudio", label: qsTr("添加音频"), icon: "folder-open-line", weight: 176, action: ""},
                {name: "saveProject", label: qsTr("保存工程"), icon: "save-3-line", weight: 174, action: ""},
                {name: "undo", label: qsTr("撤销"), icon: "arrow-go-back-line", weight: 162, action: "editor.undo"},
                {name: "split", label: qsTr("分割"), icon: "scissors-cut-line", weight: 160, action: "editor.split"},
                {name: "denoise", label: qsTr("降噪"), icon: "equalizer-line", weight: 168, action: ""},
                {name: "delete", label: qsTr("删除"), icon: "delete-bin-line", weight: 168, action: "editor.deleteSelection"},
                {name: "clear", label: qsTr("清空"), icon: "brush-line", weight: 192, action: ""}
            ]
            Button {
                id: command
                required property var modelData
                objectName: "editorCommand_" + modelData.name
                width: (commandBar.width - 6 * commandBar.spacing) * modelData.weight / 1200
                height: commandBar.height
                focusPolicy: Qt.TabFocus
                Accessible.name: modelData.label
                enabled: !AudioEditorController.busy && !workspace.recordingActive
                    && (modelData.name === "importAudio" || (AudioEditorController.hasDocument
                        && (modelData.action.length === 0 || workspace.actionRevision >= 0
                            && AudioEditorController.actionEnabled(modelData.action))))
                contentItem: Column {
                    spacing: workspace.shortLayout ? 2 : 6
                    topPadding: command.height > 70 ? 12 : workspace.shortLayout ? 3 : 5
                    ThemedIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: workspace.shortLayout ? 24 : 28; height: width
                        source: Theme.icon(command.modelData.icon)
                        tint: command.modelData.name === "delete" ? Theme.error
                              : command.modelData.name === "importAudio" || command.modelData.name === "denoise"
                                ? Theme.textPrimary : Theme.lyricCurrentText
                        opacity: command.enabled ? 1 : 0.4
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: command.modelData.label
                        font.pixelSize: workspace.width < 650 ? 11 : workspace.width < 900 ? 13 : 16
                        color: command.enabled ? Theme.textPrimary : Theme.textDisabled
                    }
                }
                background: Rectangle {
                    radius: 5
                    color: command.down ? Theme.surfacePressed : command.hovered ? Theme.surfaceHover : Theme.surface
                    border.color: command.activeFocus ? Theme.focus : Theme.borderStrong
                }
                onClicked: {
                    if (modelData.name === "importAudio") workspace.importRequested()
                    else if (modelData.name === "saveProject") workspace.saveProjectRequested()
                    else if (modelData.name === "denoise") AudioEditorController.reduceNoise()
                    else if (modelData.name === "clear") AudioEditorController.clearTimeline()
                    else AudioEditorController.triggerAction(modelData.action)
                }
                ToolTip.visible: hovered
                ToolTip.text: modelData.label
            }
        }
    }

    Rectangle {
        id: ruler
        objectName: "editorTimeRuler"
        x: commandBar.x; y: commandBar.y + commandBar.height + (workspace.shortLayout ? 6 : 12)
        width: commandBar.width; height: workspace.shortLayout ? 36 : 54
        color: Theme.surface
        border.color: Theme.divider
        Item {
            id: rulerBody
            readonly property int labelIntervals: Math.max(2, Math.floor(width / 96))
            x: workspace.headerWidth
            width: parent.width - x
            height: parent.height
            clip: true
            Repeater {
                model: 49
                Rectangle {
                    required property int index
                    x: index * rulerBody.width / 48
                    anchors.bottom: parent.bottom
                    width: 1; height: index % 4 === 0 ? 14 : 6
                    color: Theme.borderStrong
                }
            }
            Repeater {
                model: rulerBody.labelIntervals + 1
                Label {
                    required property int index
                    x: index * rulerBody.width / rulerBody.labelIntervals + 2
                    y: workspace.shortLayout ? 7 : 16
                    text: workspace.timeText(AudioEditorController.viewport.visibleStartFrame
                        + index * AudioEditorController.viewport.visibleFrameCount / rulerBody.labelIntervals,
                        AudioEditorController.viewport.visibleFrameCount / Math.max(1, AudioEditorController.sampleRate) < 120)
                    font.pixelSize: workspace.shortLayout ? 10 : 12
                    color: Theme.textSecondary
                }
            }
            Rectangle {
                x: Math.max(0, Math.min(rulerBody.width - width, waveformCanvas.pixelAtFrame(AudioEditorController.playheadFrame) - width / 2))
                y: 1; height: 27; width: timeCapsule.implicitWidth + 14; radius: 4
                color: Theme.accent
                visible: AudioEditorController.hasDocument
                Label { id: timeCapsule; anchors.centerIn: parent; color: "white"; text: workspace.timeText(AudioEditorController.playheadFrame, true) }
            }
            MouseArea {
                id: rulerInteraction
                objectName: "editorRulerSelectionInteraction"
                anchors.fill: parent
                preventStealing: true
                enabled: AudioEditorController.hasDocument && !workspace.recordingActive
                cursorShape: Qt.SizeHorCursor
                property bool scrubbing: false
                onPressed: function(mouse) {
                    forceActiveFocus()
                    scrubbing = AudioEditorController.beginScrub()
                    if (scrubbing) AudioEditorController.previewScrub(waveformCanvas.frameAtCanvasPixel(mouse.x))
                }
                onPositionChanged: function(mouse) { if (pressed && scrubbing) AudioEditorController.previewScrub(waveformCanvas.frameAtCanvasPixel(mouse.x)) }
                onReleased: { if (scrubbing) AudioEditorController.endScrub(); scrubbing = false }
                onCanceled: { scrubbing = false; AudioEditorController.cancelScrub() }
                Keys.onEscapePressed: function(event) { workspace.cancelGesture(); event.accepted = true }
            }
        }
    }

    Flickable {
        id: trackScroller
        objectName: "editorTrackScroller"
        x: ruler.x; y: ruler.y + ruler.height
        width: ruler.width
        height: Math.max(86, recordingTransport.y - y - 12 - statusLabel.height)
        contentWidth: width
        contentHeight: workspace.trackHeight * 6
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { policy: trackScroller.contentHeight > trackScroller.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        Repeater {
            model: 6 // Stable identities: gain/clip updates must not recreate track controls.
            Rectangle {
                id: trackHeader
                required property int index
                readonly property var track: AudioEditorController.tracks[index]
                objectName: "editorTrackHeader" + index
                x: 0; y: index * workspace.trackHeight
                width: workspace.headerWidth; height: workspace.trackHeight
                color: AudioEditorController.selectedTrack === index ? Theme.surfaceElevated : Theme.surface
                border.color: Theme.divider
                MouseArea { anchors.fill: parent; onClicked: AudioEditorController.selectedTrack = trackHeader.index }
                Rectangle {
                    x: 11; y: 12; width: 40; height: 40; radius: 6
                    color: trackHeader.track ? trackHeader.track.color : Theme.accent
                    Label {
                        anchors.centerIn: parent; text: trackHeader.index + 1
                        color: trackHeader.index === 3 ? "#13202D" : "white"
                        font.pixelSize: 24; font.bold: true
                    }
                }
                Label {
                    x: 66; y: 9; width: parent.width - 80; height: 25
                    text: trackHeader.track ? trackHeader.track.name : qsTr("音轨 %1").arg(trackHeader.index + 1)
                    elide: Text.ElideMiddle; color: Theme.textPrimary; font.pixelSize: 15
                    ToolTip.visible: headerHover.hovered; ToolTip.text: text
                    HoverHandler { id: headerHover }
                }
                Button {
                    id: muteButton
                    objectName: "editorTrackMute" + trackHeader.index
                    x: 66; y: 36; width: 30; height: 28
                    text: "M"; checkable: true
                    checked: trackHeader.track ? trackHeader.track.muted : false
                    enabled: !AudioEditorController.busy && !workspace.recordingActive
                    Accessible.name: qsTr("音轨 %1 静音").arg(trackHeader.index + 1)
                    onClicked: AudioEditorController.setTrackMute(trackHeader.index, checked)
                    background: Rectangle { radius: 5; color: muteButton.checked ? Theme.accent : Theme.surfacePressed; border.color: muteButton.activeFocus ? Theme.focus : "transparent" }
                    contentItem: Label { text: "M"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 18; color: muteButton.checked ? "white" : Theme.textPrimary }
                }
                Label {
                    x: 108; y: 39; width: parent.width - 142
                    text: Math.round((trackHeader.track ? trackHeader.track.gain : 1) * 100) + "%"
                    color: trackHeader.track ? trackHeader.track.color : Theme.textPrimary
                    horizontalAlignment: Text.AlignRight; font.pixelSize: 15
                }
                EditorSlider {
                    id: trackGain
                    objectName: "editorTrackGain" + trackHeader.index
                    x: 60; y: 60; width: parent.width - 76; height: 26
                    from: 0; to: 2; stepSize: 0.01
                    value: trackHeader.track ? trackHeader.track.gain : 1
                    enabled: !AudioEditorController.busy && !workspace.recordingActive
                    editorAccentColor: trackHeader.track ? trackHeader.track.color : Theme.accent
                    thumbDiameter: 15; visibleGrooveThickness: 5
                    Accessible.name: qsTr("音轨 %1 音量，百分之 %2").arg(trackHeader.index + 1).arg(Math.round(value * 100))
                    onPressedChanged: {
                        if (pressed) AudioEditorController.beginTrackGainGesture(trackHeader.index)
                        else AudioEditorController.endTrackGainGesture()
                    }
                    onMoved: {
                        if (pressed) AudioEditorController.updateTrackGainGesture(value)
                        else AudioEditorController.setTrackGain(trackHeader.index, value)
                    }
                    Keys.onEscapePressed: function(event) { AudioEditorController.cancelTrackGainGesture(); event.accepted = true }
                }
            }
        }
        EditorWaveformCanvas {
            id: waveformCanvas
            objectName: "editorWaveformCanvas"
            x: workspace.headerWidth; y: 0
            width: trackScroller.width - x; height: workspace.trackHeight * 6
            rowHeight: workspace.trackHeight
            interactive: !workspace.recordingActive
        }
    }

    Label {
        id: statusLabel
        objectName: "editorStatusBar"
        x: 18; y: recordingTransport.y - height - 5
        width: parent.width - 36 - (cancelOperationButton.visible ? cancelOperationButton.width + 8 : 0)
        height: visible ? 24 : 0
        visible: AudioEditorController.errorMessage.length > 0 || workspace.recorder.error.length > 0
            || AudioEditorController.busy && !workspace.recordingActive
        text: AudioEditorController.errorMessage || workspace.recorder.error
              || qsTr("处理中… %1%").arg(Math.round(AudioEditorController.progress * 100))
        color: AudioEditorController.busy ? Theme.textSecondary : Theme.error
        elide: Text.ElideMiddle
        ToolTip.visible: statusHover.hovered; ToolTip.text: text
        HoverHandler { id: statusHover }
    }
    ThemedButton {
        id: cancelOperationButton
        objectName: "editorCancelOperation"
        x: workspace.width - width - 18; y: statusLabel.y - 3
        width: 72; height: 28
        visible: AudioEditorController.busy && !workspace.recordingActive
        text: qsTr("取消")
        compact: true
        onClicked: AudioEditorController.cancelOperation()
    }

    Rectangle {
        id: recordingTransport
        objectName: "editorRecordingTransport"
        x: commandBar.x; y: parent.height - height - (workspace.shortLayout ? 4 : 10)
        width: commandBar.width; height: workspace.compactTransport ? 104 : 82
        color: Theme.background
        Row {
            id: deviceControls
            x: 0; y: workspace.compactTransport ? 2 : 18
            spacing: 8; height: workspace.compactTransport ? 42 : 54
            Label {
                text: qsTr("输入设备"); anchors.verticalCenter: parent.verticalCenter
                color: Theme.textSecondary; font.pixelSize: workspace.tinyTransport ? 12 : 14
            }
            ComboBox {
                id: inputDevice
                objectName: "editorInputDevice"
                width: workspace.tinyTransport ? 120 : workspace.compactTransport ? 144 : 166
                height: 36; anchors.verticalCenter: parent.verticalCenter
                model: workspace.recorder.inputDevices
                textRole: "name"; valueRole: "id"
                enabled: !workspace.recordingActive
                currentIndex: {
                    for (let i = 0; i < model.length; ++i) if (model[i].id === workspace.recorder.selectedInputDeviceId) return i
                    return 0
                }
                onActivated: function(index) { workspace.recorder.selectedInputDeviceId = model[index].id }
                ToolTip.visible: hovered
                ToolTip.text: workspace.recorder.actualInputDeviceName || currentText
                Accessible.name: qsTr("录音输入设备")
            }
        }
        Rectangle {
            visible: !workspace.compactTransport
            x: deviceControls.x + deviceControls.width + 12; y: 24
            width: 1; height: 42; color: Theme.borderStrong
        }
        ThemedButton {
            id: recordButton
            objectName: "editorRecordButton"
            x: deviceControls.x + deviceControls.width + (workspace.compactTransport ? 10 : 26)
            y: deviceControls.y
            width: workspace.compactTransport ? 78 : 100; height: deviceControls.height
            text: qsTr("录音"); icon.source: Theme.icon("record-circle-fill")
            available: !workspace.recordingActive && !AudioEditorController.busy
            onClicked: AudioEditorController.startRecording()
            contentItem: RowLayout {
                spacing: 7
                Rectangle { width: 19; height: 19; radius: 10; color: Theme.error; opacity: recordButton.enabled ? 1 : 0.4 }
                Label { text: recordButton.text; color: recordButton.enabled ? Theme.textPrimary : Theme.textDisabled; font.pixelSize: workspace.compactTransport ? 13 : 15 }
            }
        }
        ThemedButton {
            id: pauseRecordingButton
            objectName: "editorPauseRecordingButton"
            x: recordButton.x + recordButton.width + 8; y: deviceControls.y
            width: workspace.compactTransport ? 106 : 134; height: deviceControls.height
            text: workspace.recorder.state === 2 ? qsTr("继续录音") : qsTr("暂停录音")
            icon.source: Theme.icon(workspace.recorder.state === 2 ? "record-circle-fill" : "pause-fill")
            available: workspace.recorder.state === 1 || workspace.recorder.state === 2
            onClicked: AudioEditorController.pauseResumeRecording()
            contentItem: RowLayout {
                spacing: 6
                ThemedIcon { Layout.preferredWidth: 19; Layout.preferredHeight: 19; source: pauseRecordingButton.icon.source; tint: pauseRecordingButton.enabled ? Theme.textPrimary : Theme.textDisabled }
                Label { text: pauseRecordingButton.text; color: pauseRecordingButton.enabled ? Theme.textPrimary : Theme.textDisabled; font.pixelSize: workspace.compactTransport ? 13 : 15 }
            }
        }
        Row {
            id: playbackButtons
            objectName: "editorPlaybackTransport"
            x: workspace.compactTransport ? 0 : pauseRecordingButton.x + pauseRecordingButton.width + 16
            y: workspace.compactTransport ? 56 : 18
            spacing: workspace.compactTransport ? 6 : 10
            height: workspace.compactTransport ? 44 : 64
            Repeater {
                model: ["skip-back-fill", "rewind-fill", "play-fill", "speed-fill", "skip-forward-fill", "stop-fill"]
                ThemedIconButton {
                    required property int index
                    required property string modelData
                    focusPolicy: Qt.TabFocus
                    objectName: ["editorPlaybackToStartButton", "editorPlaybackRewindButton", "editorPlaybackToggleButton", "editorPlaybackForwardButton", "editorPlaybackToEndButton", "editorPlaybackStopButton"][index]
                    width: workspace.compactTransport ? (index === 2 ? 44 : workspace.tinyTransport ? 34 : 42) : index === 2 ? 68 : 54
                    height: workspace.compactTransport ? (index === 2 ? 44 : 36) : index === 2 ? 68 : 54
                    y: index === 2 ? (workspace.compactTransport ? -4 : -7) : 0
                    iconSource: Theme.icon(index === 2 && AudioEditorController.playing ? "pause-fill" : modelData)
                    iconSize: workspace.compactTransport ? 21 : index === 2 ? 32 : 24
                    accessibleName: [qsTr("回到开始"), qsTr("后退五秒"), qsTr("播放或暂停"), qsTr("前进五秒"), qsTr("跳到结尾"), qsTr("停止")][index]
                    enabled: index === 5 && workspace.recordingActive
                        || !workspace.recordingActive && AudioEditorController.hasDocument && AudioEditorController.playbackSupported
                    background: Rectangle {
                        color: parent.down ? Theme.surfacePressed : parent.hovered ? Theme.surfaceHover : Theme.surfaceElevated
                        radius: parent.index === 2 ? width / 2 : 6
                        border.color: parent.index === 2 || parent.activeFocus ? Theme.accent : "transparent"
                        border.width: parent.index === 2 ? 3 : 1
                    }
                    onClicked: {
                        if (index === 0) AudioEditorController.seekFrame(0)
                        else if (index === 1) AudioEditorController.seekMs(Math.max(0, AudioEditorController.positionMs - 5000))
                        else if (index === 2) AudioEditorController.playPause()
                        else if (index === 3) AudioEditorController.seekMs(Math.min(AudioEditorController.durationMs, AudioEditorController.positionMs + 5000))
                        else if (index === 4) AudioEditorController.seekFrame(AudioEditorController.totalFrames)
                        else if (workspace.recordingActive) AudioEditorController.stopRecording()
                        else AudioEditorController.stopPlayback()
                    }
                }
            }
        }
        Column {
            x: playbackButtons.x + playbackButtons.width + 12
            y: workspace.compactTransport ? 53 : 21
            width: Math.max(0, recordingTransport.width - x)
            spacing: 0
            Label {
                objectName: "editorCurrentTime"
                text: workspace.timeText(workspace.recordingActive ? workspace.recorder.recordedFrames : AudioEditorController.playheadFrame, true)
                color: workspace.recordingActive ? Theme.error : Theme.lyricCurrentText
                font.pixelSize: workspace.compactTransport ? 19 : 24; font.bold: true
            }
            Label {
                text: workspace.recordingActive ? (workspace.recorder.state === 2 ? qsTr("录音已暂停") : qsTr("正在录音"))
                      : qsTr("总时长  %1").arg(workspace.timeText(AudioEditorController.totalFrames, true))
                color: Theme.textSecondary; font.pixelSize: workspace.tinyTransport ? 10 : 12
            }
        }
    }
}
