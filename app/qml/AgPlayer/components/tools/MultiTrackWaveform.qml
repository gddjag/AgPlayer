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
    property bool selected: false

    signal trackClicked()
    signal clipMoveRequested(int trackIndex, int timelineStartMs)
    signal clipTrimRequested(int trackIndex, int inMs, int outMs)
    signal seekRequested(int positionMs)

    readonly property bool hasFile: !!track.hasFile
    readonly property bool locked: !!track.locked
    readonly property int sourceDurationMs: track.durationMs || 0
    readonly property int clipInMs: track.inMs || 0
    readonly property int clipOutMs: track.outMs || sourceDurationMs
    readonly property int clipDurationMs: Math.max(0, clipOutMs - clipInMs)

    width: parent ? parent.width : 1000
    height: 88
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

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 214
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

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 8

                CheckBox {
                    checked: root.hasFile
                    enabled: root.hasFile
                    focusPolicy: Qt.NoFocus
                }

                Text {
                    text: root.trackIndex + 1
                    color: Theme.primaryText
                    font.family: Theme.fontPrimary
                    font.pixelSize: 13
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Text {
                        Layout.fillWidth: true
                        text: root.hasFile ? (root.track.name || "") : qsTr("空轨道")
                        color: root.hasFile ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Text {
                        text: root.hasFile
                              ? qsTr("原始 BPM：") + Math.round(root.track.originalBpm || 0)
                              : qsTr("拖入音频文件")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 10
                    }

                    RowLayout {
                        visible: root.hasFile
                        spacing: 4

                        Button {
                            text: "M"
                            checkable: true
                            checked: !!root.track.muted
                            implicitWidth: 26
                            implicitHeight: 22
                            onClicked: LightEditor.setTrackMuted(root.trackIndex, checked)
                        }
                        Button {
                            text: "S"
                            checkable: true
                            checked: !!root.track.solo
                            implicitWidth: 26
                            implicitHeight: 22
                            onClicked: LightEditor.setTrackSolo(root.trackIndex, checked)
                        }
                        ToolButton {
                            implicitWidth: 26
                            implicitHeight: 22
                            icon.source: Theme.icon(root.locked ? "lock-line" : "lock-unlock-line")
                            icon.color: root.locked ? root.trackColor : Theme.iconSecondary
                            onClicked: LightEditor.setTrackLocked(root.trackIndex, !root.locked)
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            text: root.track.aligned ? qsTr("已对齐") : ""
                            color: Theme.waveformGreen
                            font.family: Theme.fontPrimary
                            font.pixelSize: 10
                        }
                    }
                }
            }

            TapHandler {
                onTapped: root.trackClicked()
            }
        }

        Item {
            id: waveformViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Canvas {
                anchors.fill: parent
                opacity: 0.32
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = Theme.border
                    ctx.lineWidth = 1
                    for (let x = 0; x < width; x += 44) {
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, height)
                        ctx.stroke()
                    }
                }
            }

            Rectangle {
                id: clipBody
                objectName: "clipBody"
                visible: root.hasFile
                x: root.timeToX(root.track.timelineStartMs || 0)
                y: 8
                width: Math.max(40, root.clipDurationMs * root.pixelsPerMs * root.zoomScale)
                height: parent.height - 16
                color: Qt.rgba(root.trackColor.r, root.trackColor.g, root.trackColor.b, 0.08)
                border.color: root.trackColor
                border.width: 1
                radius: 3

                WaveformItem {
                    anchors.fill: parent
                    anchors.margins: 5
                    peaks: root.track.peaks || []
                    position: Math.max(0, root.playheadMs - (root.track.timelineStartMs || 0)
                                          + root.clipInMs)
                    duration: root.sourceDurationMs
                    waveformColor: root.trackColor
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: !root.locked
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    drag.target: clipBody
                    drag.axis: Drag.XAxis
                    drag.minimumX: 0
                    onPressed: root.trackClicked()
                    onReleased: {
                        root.clipMoveRequested(root.trackIndex, root.xToTime(clipBody.x))
                        clipBody.x = Qt.binding(function() {
                            return root.timeToX(root.track.timelineStartMs || 0)
                        })
                    }
                    onDoubleClicked: root.seekRequested(root.xToTime(mouse.x + clipBody.x))
                }

                Rectangle {
                    id: leftHandle
                    width: 8
                    height: parent.height
                    anchors.left: parent.left
                    color: Theme.primaryText
                    radius: 3

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -5
                        enabled: !root.locked
                        cursorShape: Qt.SizeHorCursor
                        property real pressX: 0
                        onPressed: pressX = mouse.x
                        onReleased: {
                            const deltaMs = Math.round((mouse.x - pressX)
                                / (root.pixelsPerMs * root.zoomScale))
                            root.clipTrimRequested(root.trackIndex,
                                                   root.clipInMs + deltaMs,
                                                   root.clipOutMs)
                        }
                    }
                }

                Rectangle {
                    id: rightHandle
                    width: 8
                    height: parent.height
                    anchors.right: parent.right
                    color: Theme.primaryText
                    radius: 3

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -5
                        enabled: !root.locked
                        cursorShape: Qt.SizeHorCursor
                        property real pressX: 0
                        onPressed: pressX = mouse.x
                        onReleased: {
                            const deltaMs = Math.round((mouse.x - pressX)
                                / (root.pixelsPerMs * root.zoomScale))
                            root.clipTrimRequested(root.trackIndex,
                                                   root.clipInMs,
                                                   root.clipOutMs + deltaMs)
                        }
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
