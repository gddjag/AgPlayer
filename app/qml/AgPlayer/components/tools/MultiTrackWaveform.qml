import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

Rectangle {
    id: root
    objectName: "trackLane"

    property int trackIndex: 0
    property var track: ({})
    property color trackColor: Theme.waveformRed
    property real pixelsPerMs: 0.035
    property real zoomScale: 1.0
    property int viewportOffsetMs: 0
    property int playheadMs: 0
    property int gridStepMs: 125
    property real beatDurationMs: 500
    property real barDurationMs: 2000
    property bool gridVisible: true
    property bool selected: false
    property int controlWidth: 206
    property int laneSpacing: 4
    property real idealLaneHeight: 76
    property bool renaming: false
    property string filterText: ""

    signal trackClicked(int modifiers)
    signal clipClicked(string clipId, int modifiers)
    signal clipMoveRequested(string clipId, int targetTrackIndex,
                             int timelineStartMs, int modifiers)
    signal clipTrimRequested(string clipId, int inMs, int outMs, bool trimLeft,
                             int modifiers)
    signal seekRequested(int positionMs)

    readonly property bool hasFile: !!track.hasFile
    readonly property bool visibleLane: hasFile || trackIndex < 6
    readonly property bool locked: !!track.locked
    readonly property int sourceDurationMs: track.durationMs || 0
    readonly property int clipInMs: track.inMs || 0
    readonly property int clipOutMs: track.outMs || sourceDurationMs
    readonly property int clipDurationMs: Math.max(0, clipOutMs - clipInMs)

    width: parent ? parent.width : 1000
    // Every lane stays operable before media is dropped into it.
    visible: visibleLane
    height: !visibleLane ? 0 : (!root.hasFile ? idealLaneHeight
                                               : (track.collapsed ? 34 : idealLaneHeight))
    color: selected ? Qt.rgba(trackColor.r, trackColor.g, trackColor.b, 0.06)
                    : Theme.panel
    border.width: selected ? 1 : 0
    border.color: selected ? trackColor : "transparent"
    radius: Theme.radiusSm

    function timeToX(ms) {
        return (ms - viewportOffsetMs) * pixelsPerMs * zoomScale
    }

    function xToTime(x) {
        return Math.round(x / (pixelsPerMs * zoomScale) + viewportOffsetMs)
    }

    function gainDb(value) {
        const gain = Math.max(0.000001, Number(value === undefined ? 1 : value))
        return (20 * Math.log(gain) / Math.LN10).toFixed(1)
    }

    function panLabel(value) {
        const pan = Math.max(-1, Math.min(1, Number(value || 0)))
        if (Math.abs(pan) < 0.01)
            return "C"
        return pan < 0 ? "L" + Math.round(-pan * 100) : "R" + Math.round(pan * 100)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: root.controlWidth
            Layout.fillHeight: true
            color: Theme.elevated
            radius: Theme.radiusSm
            border.width: 1
            border.color: Theme.border

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 3
                radius: 2
                color: root.trackColor
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 7
                anchors.topMargin: 3
                anchors.bottomMargin: 3
                spacing: 1

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    spacing: 4

                    ToolButton {
                        objectName: "trackEnabledIndicator"
                        implicitWidth: 18
                        implicitHeight: 18
                        enabled: false
                        icon.source: Theme.icon(root.hasFile
                                                ? "checkbox-blank-circle-fill"
                                                : "add-line")
                        icon.color: root.hasFile ? root.trackColor : Theme.iconSecondary
                    }

                    Text {
                        text: root.trackIndex + 1
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 10
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.renaming
                        text: root.track.trackName || qsTr("轨道 %1").arg(root.trackIndex + 1)
                        color: root.hasFile ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight

                        TapHandler {
                            onDoubleTapped: {
                                root.renaming = true
                                trackNameEditor.forceActiveFocus()
                                trackNameEditor.selectAll()
                            }
                        }
                    }

                    TextField {
                        id: trackNameEditor
                        Layout.fillWidth: true
                        visible: root.renaming
                        text: root.track.trackName || qsTr("轨道 %1").arg(root.trackIndex + 1)
                        font.pixelSize: 10
                        selectByMouse: true
                        onEditingFinished: {
                            LightEditor.setTrackName(root.trackIndex, text)
                            root.renaming = false
                        }
                        Keys.onEscapePressed: root.renaming = false
                    }

                    Text {
                        visible: root.track.aligned
                        text: qsTr("已对齐")
                        color: Theme.waveformGreen
                        font.pixelSize: 9
                    }
                }

                RowLayout {
                    visible: !root.track.collapsed
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 18 : 0
                    spacing: 3

                    Text {
                        Layout.fillWidth: true
                        text: root.hasFile ? (root.track.name || "") : qsTr("拖入音频文件")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                    }

                    Button {
                        text: "M"
                        checkable: true
                        checked: !!root.track.muted
                        enabled: true
                        implicitWidth: 22
                        implicitHeight: 18
                        onClicked: LightEditor.setTrackMuted(root.trackIndex, checked)
                    }
                    Button {
                        text: "S"
                        checkable: true
                        checked: !!root.track.solo
                        enabled: true
                        implicitWidth: 22
                        implicitHeight: 18
                        onClicked: LightEditor.setTrackSolo(root.trackIndex, checked)
                    }
                    ToolButton {
                        implicitWidth: 22
                        implicitHeight: 18
                        enabled: true
                        icon.source: Theme.icon(root.locked ? "lock-line" : "lock-unlock-line")
                        icon.color: root.locked ? root.trackColor : Theme.iconSecondary
                        onClicked: LightEditor.setTrackLocked(root.trackIndex, !root.locked)
                    }
                }

                RowLayout {
                    objectName: "trackMixerRow"
                    visible: !root.track.collapsed
                    Layout.fillWidth: true
                    Layout.preferredHeight: 20
                    spacing: 3

                    Text { text: qsTr("音量"); color: Theme.secondaryText; font.pixelSize: 9 }
                    Slider {
                        objectName: "trackVolumeSlider"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 18
                        enabled: true
                        from: 0; to: 2
                        value: root.track.volume === undefined ? 1 : root.track.volume
                        onMoved: LightEditor.setTrackVolume(root.trackIndex, value)
                    }
                    Text {
                        objectName: "trackGainDbLabel"
                        text: root.gainDb(root.track.volume) + " dB"
                        color: Theme.secondaryText
                        font.pixelSize: 9
                    }
                    Text { text: qsTr("声像"); color: Theme.secondaryText; font.pixelSize: 9 }
                    Slider {
                        objectName: "trackPanSlider"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 18
                        enabled: true
                        from: -1; to: 1
                        value: root.track.pan || 0
                        onMoved: LightEditor.setTrackPan(root.trackIndex, value)
                    }
                    Text {
                        objectName: "trackPanLabel"
                        text: root.panLabel(root.track.pan)
                        color: Theme.secondaryText
                        font.pixelSize: 9
                    }
                    Text {
                        objectName: "trackOutputLabel"
                        text: "Master"
                        color: Theme.secondaryText
                        font.pixelSize: 9
                    }
                }
            }

            TapHandler {
                onTapped: root.trackClicked(Qt.NoModifier)
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: trackContextMenu.popup()
            }

            Menu {
                id: trackContextMenu
                MenuItem {
                    text: qsTr("重命名轨道")
                    onTriggered: {
                        root.renaming = true
                        trackNameEditor.forceActiveFocus()
                        trackNameEditor.selectAll()
                    }
                }
                MenuItem {
                    text: root.track.collapsed ? qsTr("展开轨道") : qsTr("折叠轨道")
                    onTriggered: LightEditor.setTrackCollapsed(
                        root.trackIndex, !root.track.collapsed)
                }
                Menu {
                    title: qsTr("轨道颜色")
                    Repeater {
                        model: ["#E4007F", "#0078D4", "#00B4A0", "#D27722",
                                "#8B5CF6", "#EC4899", "#22C55E", "#F59E0B"]
                        MenuItem {
                            text: modelData
                            onTriggered: LightEditor.setTrackColor(
                                root.trackIndex, modelData)
                        }
                    }
                }
                MenuSeparator { }
                MenuItem {
                    text: qsTr("上移轨道")
                    enabled: root.trackIndex > 0
                    onTriggered: LightEditor.moveTrack(
                        root.trackIndex, root.trackIndex - 1)
                }
                MenuItem {
                    text: qsTr("下移轨道")
                    enabled: root.trackIndex < LightEditor.trackCount - 1
                    onTriggered: LightEditor.moveTrack(
                        root.trackIndex, root.trackIndex + 1)
                }
                MenuItem {
                    text: qsTr("清空轨道")
                    enabled: root.hasFile
                    onTriggered: LightEditor.clearTrack(root.trackIndex)
                }
            }
        }

        Item {
            id: waveformViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Canvas {
                id: timelineGrid
                anchors.fill: parent
                visible: root.gridVisible
                opacity: 0.48
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const scale = Math.max(0.0001, root.pixelsPerMs * root.zoomScale)
                    // The ruler represents musical time, while the controller still
                    // snaps at the selected subdivision during a drag or trim.
                    const interval = Math.max(1, root.gridStepMs, root.beatDurationMs)
                    const start = Math.floor(root.viewportOffsetMs / interval) * interval
                    const end = root.viewportOffsetMs + width / scale
                    for (let time = start; time <= end; time += interval) {
                        const x = (time - root.viewportOffsetMs) * scale
                        const isBar = Math.abs(time
                            - Math.round(time / root.barDurationMs)
                              * root.barDurationMs) < interval / 2
                        const isBeat = Math.abs(time
                            - Math.round(time / root.beatDurationMs)
                              * root.beatDurationMs) < interval / 2
                        ctx.strokeStyle = isBar ? Theme.secondaryText
                                        : (isBeat ? Theme.border : Theme.border)
                        ctx.lineWidth = isBar ? 1.2 : 1
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, height)
                        ctx.stroke()
                    }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Connections {
                    target: root
                    function onViewportOffsetMsChanged() { timelineGrid.requestPaint() }
                    function onZoomScaleChanged() { timelineGrid.requestPaint() }
                    function onPixelsPerMsChanged() { timelineGrid.requestPaint() }
                    function onGridStepMsChanged() { timelineGrid.requestPaint() }
                    function onBeatDurationMsChanged() { timelineGrid.requestPaint() }
                    function onBarDurationMsChanged() { timelineGrid.requestPaint() }
                }
            }

            Repeater {
                model: root.track.clips || []

                Rectangle {
                    id: clipBody
                    required property var modelData
                    objectName: "clipBody"
                    readonly property int clipInMs: modelData.inMs || 0
                    readonly property int clipOutMs:
                        modelData.outMs || modelData.durationMs || 0
                    readonly property int clipDurationMs:
                        Math.max(0, modelData.timelineDurationMs
                                    || (clipOutMs - clipInMs))
                    readonly property bool clipLocked: !!modelData.locked
                    property var livePeaks:
                        LightEditor.waveformPeaksForClip(modelData.clipId)
                    visible: root.filterText.trim().length === 0
                             || (modelData.name || "").toLocaleLowerCase()
                                .indexOf(root.filterText.trim().toLocaleLowerCase()) >= 0
                    x: root.timeToX(modelData.timelineStartMs || 0)
                       + bodyMoveArea.dragOffsetX
                    y: 5
                    width: Math.max(40, clipDurationMs * root.pixelsPerMs
                                    * root.zoomScale)
                    height: waveformViewport.height - 10
                    color: Qt.rgba(root.trackColor.r, root.trackColor.g,
                                   root.trackColor.b,
                                   LightEditor.selectedClipIds.indexOf(modelData.clipId) >= 0
                                   ? 0.18 : 0.08)
                    border.color: root.trackColor
                    border.width: LightEditor.selectedClipIds.indexOf(modelData.clipId) >= 0
                                  ? 2 : 1
                    radius: 3

                    WaveformItem {
                        objectName: "editorClipWaveform"
                        anchors.fill: parent
                        anchors.margins: 5
                        peaks: clipBody.livePeaks || []
                        position: Math.max(0, root.playheadMs
                                           - (clipBody.modelData.timelineStartMs || 0)
                                           + clipBody.clipInMs)
                        duration: clipBody.modelData.durationMs || 0
                        visualMode: 0
                        baseColor: Qt.rgba(root.trackColor.r,
                                           root.trackColor.g,
                                           root.trackColor.b, 0.62)
                        progressColor: root.trackColor
                        waveformColor: root.trackColor
                    }

                    Connections {
                        target: LightEditor
                        function onWaveformAnalysisCompleted(clipId) {
                            if (clipId === clipBody.modelData.clipId)
                                clipBody.livePeaks =
                                    LightEditor.waveformPeaksForClip(clipId)
                        }
                    }

                    MouseArea {
                        id: bodyMoveArea
                        objectName: "clipMoveHandler"
                        z: 10
                        anchors.fill: parent
                        enabled: !clipBody.clipLocked
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: dragging ? Qt.ClosedHandCursor
                                              : Qt.OpenHandCursor
                        property real pressSceneX: 0
                        property real pressSceneY: 0
                        property real dragOffsetX: 0
                        property real dragOffsetY: 0
                        property bool dragging: false

                        onPressed: function(mouse) {
                            const scenePoint = mapToItem(null, mouse.x, mouse.y)
                            pressSceneX = scenePoint.x
                            pressSceneY = scenePoint.y
                            dragOffsetX = 0
                            dragOffsetY = 0
                            dragging = false
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed)
                                return
                            const scenePoint = mapToItem(null, mouse.x, mouse.y)
                            const deltaX = scenePoint.x - pressSceneX
                            const deltaY = scenePoint.y - pressSceneY
                            if (!dragging
                                    && Math.max(Math.abs(deltaX),
                                                Math.abs(deltaY)) >= 4)
                                dragging = true
                            if (dragging) {
                                const minimumOffset = -root.timeToX(
                                    clipBody.modelData.timelineStartMs || 0)
                                dragOffsetX = Math.max(minimumOffset, deltaX)
                                dragOffsetY = deltaY
                            }
                        }
                        onReleased: function(mouse) {
                            if (!dragging) {
                                root.clipClicked(clipBody.modelData.clipId,
                                                 mouse.modifiers)
                                return
                            }
                            const clipId = clipBody.modelData.clipId
                            const startMs = clipBody.modelData.timelineStartMs || 0
                            const deltaMs = Math.round(dragOffsetX
                                / (root.pixelsPerMs * root.zoomScale))
                            const targetTrack = Math.max(
                                0, Math.min(LightEditor.trackCount - 1,
                                    root.trackIndex
                                    + Math.round(dragOffsetY
                                                 / (root.height + root.laneSpacing))))
                            dragOffsetX = 0
                            dragOffsetY = 0
                            dragging = false
                            root.clipMoveRequested(
                                clipId,
                                targetTrack,
                                Math.max(0, startMs + deltaMs),
                                mouse.modifiers)
                        }
                        onDoubleClicked: function(mouse) {
                            root.seekRequested(root.xToTime(
                                mouse.x + clipBody.x))
                        }
                    }

                    Rectangle {
                        objectName: "clipTrimLeftHandle"
                        z: 20
                        width: 8
                        height: parent.height
                        anchors.left: parent.left
                        color: Theme.primaryText
                        radius: 3

                        DragHandler {
                            objectName: "clipTrimLeftHandler"
                            property real pendingDeltaX: 0
                            enabled: !clipBody.clipLocked
                            acceptedButtons: Qt.LeftButton
                            target: null
                            xAxis.enabled: true
                            yAxis.enabled: false
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            onTranslationChanged: if (active) {
                                pendingDeltaX = translation.x
                            }
                            onActiveChanged: if (active) {
                                pendingDeltaX = 0
                            } else {
                                const deltaMs = Math.round(pendingDeltaX
                                    / (root.pixelsPerMs * root.zoomScale))
                                 root.clipTrimRequested(
                                     clipBody.modelData.clipId,
                                     clipBody.clipInMs + deltaMs,
                                    clipBody.clipOutMs, true, point.modifiers)
                            }
                        }

                        HoverHandler { cursorShape: Qt.SizeHorCursor }
                    }

                    Rectangle {
                        objectName: "clipTrimRightHandle"
                        z: 20
                        width: 8
                        height: parent.height
                        anchors.right: parent.right
                        color: Theme.primaryText
                        radius: 3

                        DragHandler {
                            objectName: "clipTrimRightHandler"
                            property real pendingDeltaX: 0
                            enabled: !clipBody.clipLocked
                            acceptedButtons: Qt.LeftButton
                            target: null
                            xAxis.enabled: true
                            yAxis.enabled: false
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            onTranslationChanged: if (active) {
                                pendingDeltaX = translation.x
                            }
                            onActiveChanged: if (active) {
                                pendingDeltaX = 0
                            } else {
                                const deltaMs = Math.round(pendingDeltaX
                                    / (root.pixelsPerMs * root.zoomScale))
                                 root.clipTrimRequested(
                                     clipBody.modelData.clipId,
                                     clipBody.clipInMs,
                                    clipBody.clipOutMs + deltaMs, false, point.modifiers)
                            }
                        }


                        HoverHandler { cursorShape: Qt.SizeHorCursor }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: !root.hasFile
                text: root.trackIndex === 0 ? qsTr("添加或拖入音频文件") : qsTr("空轨道")
                color: Theme.secondaryText
                font.family: Theme.fontPrimary
                font.pixelSize: 11
            }

            Rectangle {
                x: root.timeToX(root.playheadMs)
                width: 1
                height: parent.height
                color: Theme.cyan
                visible: x >= 0 && x <= parent.width
            }
        }
    }
}
