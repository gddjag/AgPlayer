import QtQuick
import QtQuick.Controls
import AgPlayer

Canvas {
    id: canvas
    objectName: "equalizerResponseCurve"
    antialiasing: true
    renderTarget: Canvas.FramebufferObject
    property real plotLeft: 62
    property real plotRight: 18
    property real plotTop: 18
    property real plotBottom: 34
    property int gainRevision: 0
    readonly property var bandFrequencies: [20, 31.5, 50, 80, 125, 200, 315,
                                            500, 800, 1250, 2000, 3150, 5000,
                                            8000, 12500, 16000, 20000]
    readonly property var axisFrequencies: [20, 50, 100, 200, 500, 1000,
                                            2000, 5000, 10000, 20000]
    property var points: EqualizerController.responseCurve(
                             Math.max(128, Math.round(width / 3)))

    function frequencyX(frequency) {
        return plotLeft + Math.log(frequency / 20) / Math.log(1000)
               * (width - plotLeft - plotRight)
    }

    function gainY(gain) {
        return plotTop + (12 - Math.max(-12, Math.min(12, gain))) / 24
               * (height - plotTop - plotBottom)
    }

    function refresh() {
        ++gainRevision
        points = EqualizerController.responseCurve(
                    Math.max(128, Math.round(width / 3)))
        requestPaint()
    }

    onWidthChanged: refresh()
    onHeightChanged: requestPaint()

    Connections {
        target: EqualizerController
        function onResponseCurveChanged() { canvas.refresh() }
    }

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        var plotWidth = width - plotLeft - plotRight

        ctx.strokeStyle = Theme.divider
        ctx.lineWidth = 1
        for (var row = 0; row < 5; ++row) {
            var gridY = gainY(12 - row * 6)
            ctx.beginPath()
            ctx.moveTo(plotLeft, gridY)
            ctx.lineTo(width - plotRight, gridY)
            ctx.stroke()
        }
        for (var axis = 0; axis < axisFrequencies.length; ++axis) {
            var gridX = frequencyX(axisFrequencies[axis])
            ctx.beginPath()
            ctx.moveTo(gridX, plotTop)
            ctx.lineTo(gridX, height - plotBottom)
            ctx.stroke()
        }

        if (!points || points.length < 2)
            return
        var gradient = ctx.createLinearGradient(plotLeft, 0,
                                                width - plotRight, 0)
        gradient.addColorStop(0, Theme.waveformCyan)
        gradient.addColorStop(0.52, Theme.waveformBlue)
        gradient.addColorStop(1, Theme.waveformMagenta)

        ctx.beginPath()
        for (var fillPoint = 0; fillPoint < points.length; ++fillPoint) {
            var fillX = plotLeft + fillPoint * plotWidth / (points.length - 1)
            var fillY = gainY(points[fillPoint])
            if (fillPoint === 0)
                ctx.moveTo(fillX, fillY)
            else
                ctx.lineTo(fillX, fillY)
        }
        ctx.lineTo(width - plotRight, gainY(-12))
        ctx.lineTo(plotLeft, gainY(-12))
        ctx.closePath()
        ctx.fillStyle = Theme.isLight ? "rgba(22,136,255,0.08)"
                                      : "rgba(80,110,255,0.12)"
        ctx.fill()

        ctx.strokeStyle = gradient
        ctx.lineWidth = 2.5
        ctx.beginPath()
        for (var point = 0; point < points.length; ++point) {
            var x = plotLeft + point * plotWidth / (points.length - 1)
            var y = gainY(points[point])
            if (point === 0)
                ctx.moveTo(x, y)
            else
                ctx.lineTo(x, y)
        }
        ctx.stroke()

        gainRevision
        for (var band = 0; band < bandFrequencies.length; ++band) {
            var nodeX = frequencyX(bandFrequencies[band])
            var responseIndex = Math.round(
                        Math.log(bandFrequencies[band] / 20) / Math.log(1000)
                        * (points.length - 1))
            var nodeY = gainY(points[Math.max(0, Math.min(
                                                   points.length - 1,
                                                   responseIndex))])
            ctx.beginPath()
            ctx.arc(nodeX, nodeY, 4.5, 0, Math.PI * 2)
            ctx.fillStyle = band < 9 ? Theme.waveformBlue : Theme.waveformViolet
            ctx.fill()
            ctx.strokeStyle = Theme.primaryText
            ctx.lineWidth = 1.5
            ctx.stroke()
        }
    }

    Repeater {
        model: [{"text": "+12 dB", "gain": 12},
                {"text": "0 dB", "gain": 0},
                {"text": "−12 dB", "gain": -12}]
        Label {
            required property var modelData
            x: 4
            y: Math.round(canvas.gainY(modelData.gain) - height / 2)
            width: canvas.plotLeft - 10
            text: modelData.text
            color: Theme.secondaryText
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
        }
    }

    Repeater {
        model: canvas.axisFrequencies
        Label {
            required property real modelData
            x: Math.max(canvas.plotLeft,
                        Math.min(canvas.width - canvas.plotRight - width,
                                 canvas.frequencyX(modelData) - width / 2))
            y: canvas.height - canvas.plotBottom + 7
            text: modelData >= 1000
                  ? (modelData / 1000).toLocaleString(Qt.locale(), 'f', 0)
                    + " kHz"
                  : modelData.toLocaleString(Qt.locale(), 'f', 0) + " Hz"
            color: Theme.secondaryText
            font.pixelSize: 11
        }
    }
}
