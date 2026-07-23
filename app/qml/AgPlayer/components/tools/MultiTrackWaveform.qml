import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgPlayer

// Multi-track waveform lane: track header, optional time ruler, and waveform.
// This component is reusable for an active audio track and for empty slots.
Rectangle {
    id: root

    color: Theme.panel
    radius: Theme.radiusSm
    border.color: root.isSelected ? Theme.cyan : Theme.border
    border.width: root.isSelected ? 2 : 1
    height: 78
    Layout.fillWidth: true

    property int trackIndex: 0
    property string trackName: ""
    property int trackDurationMs: 0
    property var trackPeaks: []
    property color trackColor: Theme.cyan
    property bool isActive: false
    property bool isSelected: false
    property bool showTimeRuler: false
    property real zoomScale: 1.0
    property int viewportOffsetMs: 0
    property int playheadMs: 0
    property bool hasFile: false

    signal trackClicked()
    signal seekRequested(int positionMs)

    readonly property real pixelsPerMs: 0.03

    function formatTime(ms) {
        if (ms <= 0) return "00:00"
        const totalSec = Math.floor(ms / 1000)
        const m = Math.floor(totalSec / 60)
        const s = totalSec % 60
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    function timeToX(ms) {
        return (ms - root.viewportOffsetMs) * root.pixelsPerMs * root.zoomScale
    }

    function xToTime(x) {
        return Math.round(x / (root.pixelsPerMs * root.zoomScale) + root.viewportOffsetMs)
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.trackClicked()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Track header: number, icon, name, duration.
        Rectangle {
            Layout.preferredWidth: 170
            Layout.fillHeight: true
            color: root.isActive
                   ? Qt.rgba(root.trackColor.r, root.trackColor.g, root.trackColor.b, 0.12)
                   : "transparent"
            radius: Theme.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingSm

                Rectangle {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    radius: 12
                    color: root.trackColor

                    Text {
                        anchors.centerIn: parent
                        text: root.trackIndex + 1
                        color: Theme.background
                        font.pixelSize: 11
                        font.family: Theme.fontPrimary
                        font.weight: Font.Medium
                    }
                }

                Text {
                    text: "\u266A"
                    color: root.hasFile ? root.trackColor : Theme.secondaryText
                    font.pixelSize: 18
                    font.family: Theme.fontFallback
                    visible: root.hasFile
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: root.hasFile ? root.trackName : qsTr("Empty track")
                        color: root.hasFile ? Theme.primaryText : Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        text: root.hasFile ? root.formatTime(root.trackDurationMs) : ""
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 10
                        visible: root.hasFile
                    }
                }
            }
        }

        // Waveform viewport.
        Item {
            id: waveformArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Shared time ruler, visible only on the first track.
                Rectangle {
                    id: rulerContainer
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.showTimeRuler ? 22 : 0
                    visible: root.showTimeRuler
                    color: "transparent"
                    clip: true

                    Canvas {
                        id: timeRuler
                        anchors.fill: parent

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            ctx.fillStyle = Theme.secondaryText
                            ctx.strokeStyle = Theme.border
                            ctx.lineWidth = 1
                            ctx.font = "10px '" + Theme.fontFallback + "'"

                            const pps = root.pixelsPerMs * root.zoomScale
                            if (pps <= 0) return

                            const msPerPixel = 1.0 / pps
                            let intervalMs = 1000
                            if (msPerPixel > 5000) intervalMs = 60000
                            else if (msPerPixel > 2000) intervalMs = 30000
                            else if (msPerPixel > 500) intervalMs = 10000
                            else if (msPerPixel > 100) intervalMs = 5000
                            else intervalMs = 1000

                            const endMs = root.viewportOffsetMs + (width / pps)
                            const startMs = Math.floor(root.viewportOffsetMs / intervalMs) * intervalMs

                            for (let t = startMs; t <= endMs; t += intervalMs) {
                                const x = (t - root.viewportOffsetMs) * pps
                                ctx.beginPath()
                                ctx.moveTo(x, height - 4)
                                ctx.lineTo(x, height)
                                ctx.stroke()

                                const totalSec = Math.floor(t / 1000)
                                const m = Math.floor(totalSec / 60)
                                const s = totalSec % 60
                                const label = (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
                                ctx.fillText(label, x + 3, height - 6)
                            }
                        }

                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        Connections {
                            target: root
                            function onZoomScaleChanged() { timeRuler.requestPaint() }
                            function onViewportOffsetMsChanged() { timeRuler.requestPaint() }
                        }
                    }
                }

                // Waveform content and playhead.
                Item {
                    id: waveformContainer
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    WaveformItem {
                        id: waveform
                        height: parent.height
                        width: root.hasFile && root.trackDurationMs > 0
                               ? root.trackDurationMs * root.pixelsPerMs * root.zoomScale
                               : parent.width
                        x: root.hasFile && root.trackDurationMs > 0
                           ? -root.viewportOffsetMs * root.pixelsPerMs * root.zoomScale
                           : 0
                        peaks: root.trackPeaks
                        position: root.playheadMs
                        duration: root.trackDurationMs
                        waveformColor: root.trackColor
                        visible: root.hasFile
                        enabled: root.hasFile
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Empty track")
                        color: Theme.secondaryText
                        font.family: Theme.fontPrimary
                        font.pixelSize: 12
                        visible: !root.hasFile
                    }

                    // Playhead indicator.
                    Rectangle {
                        x: root.timeToX(root.playheadMs)
                        y: 0
                        width: 2
                        height: parent.height
                        color: Theme.primaryText
                        visible: root.hasFile
                                 && root.playheadMs >= root.viewportOffsetMs
                                 && x >= -1 && x <= parent.width
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: root.hasFile
                        onPressed: function(mouse) {
                            root.seekRequested(root.xToTime(mouse.x))
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed) root.seekRequested(root.xToTime(mouse.x))
                        }
                    }
                }
            }
        }
    }
}
