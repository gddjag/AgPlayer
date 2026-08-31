import QtQuick
import QtQuick.Controls
import AgPlayer

Canvas {
    id: canvas
    objectName: "equalizerResponseCurve"
    antialiasing: true
    renderTarget: Canvas.FramebufferObject

    // The 18-band control plot leaves room for the dB and frequency labels.
    // The wider grid bounds are decorative extensions only.
    property real plotLeft: 117
    property real plotRight: 79
    property real gridLeft: 84
    property real gridRight: 30
    property real plotTop: 27
    property real plotBottom: 50
    property int gainRevision: 0
    readonly property real gainRangeDb: EqualizerController.gainRangeDb
    readonly property var bandFrequencies: [20, 31.5, 50, 80, 125, 200, 315,
                                            500, 800, 1250, 2000, 3150, 5000,
                                            8000, 10000, 12500, 16000, 20000]
    readonly property var frequencyLabels: ["20", "31.5", "50", "80", "125",
                                            "200", "315", "500", "800", "1.25k",
                                            "2k", "3.15k", "5k", "8k", "10k",
                                            "12.5k", "16k", "20k"]

    function bandX(index) {
        return plotLeft + index / (bandFrequencies.length - 1)
               * (width - plotLeft - plotRight)
    }

    function gainY(gain) {
        var range = Math.max(0.1, gainRangeDb)
        var clamped = Math.max(-range, Math.min(range, gain))
        return plotTop + (range - clamped) / (range * 2)
               * (height - plotTop - plotBottom)
    }

    function envelopePoints() {
        var result = []
        for (var index = 0; index < bandFrequencies.length; ++index) {
            result.push({"x": bandX(index),
                         "y": gainY(EqualizerController.bandGain(index))})
        }
        return result
    }

    function traceEnvelope(context, envelope) {
        context.moveTo(envelope[0].x, envelope[0].y)
        for (var index = 1; index < envelope.length; ++index) {
            var previous = envelope[index - 1]
            var current = envelope[index]
            var midpoint = (previous.x + current.x) / 2
            context.bezierCurveTo(midpoint, previous.y,
                                  midpoint, current.y,
                                  current.x, current.y)
        }
    }

    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onGainRevisionChanged: requestPaint()

    Connections {
        target: EqualizerController
        function onResponseCurveChanged() { canvas.requestPaint() }
        function onGainRangeDbChanged() { canvas.requestPaint() }
    }

    Connections {
        target: Theme
        function onEffectiveModeChanged() { canvas.requestPaint() }
    }

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)

        for (var row = 0; row < 7; ++row) {
            var rowGain = gainRangeDb - row * gainRangeDb / 3
            var gridY = gainY(rowGain)
            ctx.beginPath()
            ctx.setLineDash(row === 3 ? [] : [4, 5])
            ctx.strokeStyle = (row === 3 ? Theme.textSecondary
                                         : Theme.opaqueDivider).toString()
            ctx.globalAlpha = row === 3 ? 0.72 : 0.82
            ctx.lineWidth = row === 3 ? 1.2 : 1
            ctx.moveTo(gridLeft, gridY)
            ctx.lineTo(width - gridRight, gridY)
            ctx.stroke()
        }

        ctx.setLineDash([4, 5])
        ctx.strokeStyle = Theme.opaqueDivider.toString()
        ctx.lineWidth = 1
        ctx.globalAlpha = 0.75
        for (var axis = 0; axis < bandFrequencies.length; ++axis) {
            var gridX = bandX(axis)
            ctx.beginPath()
            ctx.moveTo(gridX, plotTop)
            ctx.lineTo(gridX, height - plotBottom)
            ctx.stroke()
        }
        ctx.setLineDash([])
        ctx.globalAlpha = 1

        gainRevision
        var envelope = envelopePoints()
        if (envelope.length < 2)
            return

        var gradient = ctx.createLinearGradient(plotLeft, 0,
                                                width - plotRight, 0)
        gradient.addColorStop(0, "#09AED9")
        gradient.addColorStop(0.52, "#176CF0")
        gradient.addColorStop(1, "#E35BD6")

        ctx.beginPath()
        traceEnvelope(ctx, envelope)
        ctx.lineTo(envelope[envelope.length - 1].x, gainY(-gainRangeDb))
        ctx.lineTo(envelope[0].x, gainY(-gainRangeDb))
        ctx.closePath()
        var fillGradient = ctx.createLinearGradient(0, plotTop, 0,
                                                    height - plotBottom)
        fillGradient.addColorStop(0, "rgba(27,117,174,0.12)")
        fillGradient.addColorStop(1, "rgba(27,117,174,0.01)")
        ctx.fillStyle = fillGradient
        ctx.fill()

        ctx.beginPath()
        traceEnvelope(ctx, envelope)
        ctx.globalAlpha = 0.24
        ctx.strokeStyle = gradient
        ctx.lineWidth = 7
        ctx.stroke()

        ctx.beginPath()
        traceEnvelope(ctx, envelope)
        ctx.globalAlpha = 1
        ctx.strokeStyle = gradient
        ctx.lineWidth = 2.2
        ctx.stroke()

        for (var band = 0; band < envelope.length; ++band) {
            ctx.beginPath()
            ctx.arc(envelope[band].x, envelope[band].y, 8.5, 0, Math.PI * 2)
            ctx.fillStyle = band < 9 ? "#087FB6" : "#8439A8"
            ctx.fill()
            ctx.strokeStyle = Theme.controlHandle.toString()
            ctx.lineWidth = 2
            ctx.stroke()
        }
    }

    Repeater {
        model: [{"text": "+" + canvas.gainRangeDb.toFixed(0) + " dB",
                 "gain": canvas.gainRangeDb},
                {"text": "0 dB", "gain": 0},
                {"text": "−" + canvas.gainRangeDb.toFixed(0) + " dB",
                 "gain": -canvas.gainRangeDb}]
        Label {
            required property var modelData
            x: 12
            y: Math.round(canvas.gainY(modelData.gain) - height / 2)
            width: canvas.gridLeft - 18
            text: modelData.text
            color: Theme.textPrimary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: canvas.width < 1000 ? 16 : 18
            horizontalAlignment: Text.AlignRight
        }
    }

    Repeater {
        model: canvas.bandFrequencies
        Label {
            required property int index
            required property real modelData
            objectName: "equalizerResponseFrequency-" + index
            x: canvas.bandX(index) - width / 2
            y: canvas.height - canvas.plotBottom
               + (canvas.width < 1300
                  ? (index >= 13 ? -4 + (index - 13) % 3 * 18 : 0)
                  : 12)
            text: canvas.frequencyLabels[index]
            color: Theme.textSecondary
            font.family: "Microsoft YaHei UI"
            font.pixelSize: canvas.width < 1300 ? 14 : 18
        }
    }
}
