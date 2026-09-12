import QtQuick
import AgPlayer

Canvas {
    id: root
    objectName: "beatGridOverlay"

    property real viewStartMs: 0
    property real viewEndMs: 0
    property real firstBeatMs: 0
    property real bpm: 0
    property int grouping: 4
    property color lineColor: Qt.rgba(Theme.primaryText.r, Theme.primaryText.g,
                                     Theme.primaryText.b, 0.3)
    property color fourBeatColor: Theme.success
    property color eightBeatColor: Theme.danger
    property int paintRequestCount: 0
    property int paintPassCount: 0

    readonly property real beatIntervalMs: bpm > 0 ? 60000 / bpm : 0

    function firstVisibleBeatIndex() {
        if (!(beatIntervalMs > 0) || !(viewEndMs >= viewStartMs))
            return 0
        return Math.ceil((viewStartMs - firstBeatMs) / beatIntervalMs
                         - 0.0000001)
    }

    function lastVisibleBeatIndex() {
        if (!(beatIntervalMs > 0) || !(viewEndMs >= viewStartMs))
            return -1
        return Math.floor((viewEndMs - firstBeatMs) / beatIntervalMs
                          + 0.0000001)
    }

    function visibleBeatCount() {
        return Math.max(0, lastVisibleBeatIndex() - firstVisibleBeatIndex() + 1)
    }

    function normalizedModulo(value, divisor) {
        return ((value % divisor) + divisor) % divisor
    }

    function isFourBeat(beatIndex) {
        return normalizedModulo(beatIndex, 4) === 0
    }

    function isEightBeat(beatIndex) {
        return normalizedModulo(beatIndex, 8) === 0
    }

    function isDownbeat(beatIndex) {
        return isFourBeat(beatIndex)
    }

    function xForBeat(beatIndex) {
        var span = viewEndMs - viewStartMs
        if (!(span > 0))
            return 0
        var timeMs = firstBeatMs + beatIndex * beatIntervalMs
        return (timeMs - viewStartMs) / span * width
    }

    function requestGridPaint() {
        if (!visible)
            return
        ++paintRequestCount
        requestPaint()
    }

    onViewStartMsChanged: requestGridPaint()
    onViewEndMsChanged: requestGridPaint()
    onFirstBeatMsChanged: requestGridPaint()
    onBpmChanged: requestGridPaint()
    onGroupingChanged: requestGridPaint()
    onLineColorChanged: requestGridPaint()
    onFourBeatColorChanged: requestGridPaint()
    onEightBeatColorChanged: requestGridPaint()
    onWidthChanged: requestGridPaint()
    onHeightChanged: requestGridPaint()
    onVisibleChanged: requestGridPaint()

    onPaint: {
        if (!visible || !(beatIntervalMs > 0) || !(viewEndMs > viewStartMs)
                || width <= 0 || height <= 0)
            return
        ++paintPassCount
        var context = getContext("2d")
        context.reset()

        var first = firstVisibleBeatIndex()
        var last = lastVisibleBeatIndex()

        function strokeMatching(predicate, color, widthValue) {
            context.beginPath()
            for (var beat = first; beat <= last; ++beat) {
                if (!predicate(beat))
                    continue
                var x = Math.round(xForBeat(beat)) + 0.5
                context.moveTo(x, 0)
                context.lineTo(x, root.height)
            }
            context.strokeStyle = color
            context.lineWidth = widthValue
            context.stroke()
        }

        strokeMatching(function(beat) { return !isFourBeat(beat) },
                       lineColor, 1)
        strokeMatching(function(beat) {
            return isFourBeat(beat)
                    && (grouping !== 8 || !isEightBeat(beat))
        }, fourBeatColor, 1.5)
        if (grouping === 8) {
            strokeMatching(function(beat) { return isEightBeat(beat) },
                           eightBeatColor, 2.5)
        }
    }
}
