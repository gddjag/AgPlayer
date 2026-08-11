import QtQuick
import AgPlayer

Canvas {
    id: canvas
    objectName: "equalizerResponseCurve"
    antialiasing: true
    renderTarget: Canvas.FramebufferObject
    property var points: EqualizerController.responseCurve(Math.max(64,
                                           Math.round(width / 4)))

    onWidthChanged: {
        points = EqualizerController.responseCurve(Math.max(64,
                                            Math.round(width / 4)))
        requestPaint()
    }
    onHeightChanged: requestPaint()

    Connections {
        target: EqualizerController
        function onResponseCurveChanged() {
            canvas.points = EqualizerController.responseCurve(
                        Math.max(64, Math.round(canvas.width / 4)))
            canvas.requestPaint()
        }
    }

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        ctx.strokeStyle = Theme.border
        ctx.lineWidth = 1
        for (var i = 0; i < 3; ++i) {
            var gridY = 8 + i * (height - 16) / 2
            ctx.beginPath()
            ctx.moveTo(0, gridY)
            ctx.lineTo(width, gridY)
            ctx.stroke()
        }
        if (!points || points.length < 2)
            return
        var gradient = ctx.createLinearGradient(0, 0, width, 0)
        gradient.addColorStop(0, Theme.waveformCyan)
        gradient.addColorStop(0.55, Theme.waveformBlue)
        gradient.addColorStop(1, Theme.waveformMagenta)
        ctx.strokeStyle = gradient
        ctx.lineWidth = 2
        ctx.beginPath()
        for (var point = 0; point < points.length; ++point) {
            var x = point * width / (points.length - 1)
            var bounded = Math.max(-12, Math.min(12, points[point]))
            var y = 8 + (12 - bounded) / 24 * (height - 16)
            if (point === 0)
                ctx.moveTo(x, y)
            else
                ctx.lineTo(x, y)
        }
        ctx.stroke()
    }
}
