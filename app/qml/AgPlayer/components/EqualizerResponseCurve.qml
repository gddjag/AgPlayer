import QtQuick
import QtQuick.Controls
import AgPlayer

Canvas {
    id: canvas
    objectName: "equalizerResponseCurve"
    antialiasing: true
    renderTarget: Canvas.FramebufferObject
    property real plotLeft: 84
    property real plotRight: 30
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
    property var points: EqualizerController.responseCurve(
                             Math.max(256, Math.round(width / 2)))

    function frequencyX(frequency) {
        return plotLeft + Math.log(frequency / 20) / Math.log(1000)
               * (width - plotLeft - plotRight)
    }

    function markerXForIndex(index) {
        var markerLeft = plotLeft + 33
        var markerRight = width - plotRight - 48
        return markerLeft + index / (bandFrequencies.length - 1)
               * (markerRight - markerLeft)
    }

    function gainY(gain) {
        var range = Math.max(0.1, gainRangeDb)
        return plotTop + (range - Math.max(-range, Math.min(range, gain)))
               / (range * 2) * (height - plotTop - plotBottom)
    }

    function refresh() {
        points = EqualizerController.responseCurve(
                    Math.max(256, Math.round(width / 2)))
        requestPaint()
    }

    onWidthChanged: refresh()
    onHeightChanged: requestPaint()
    onGainRevisionChanged: requestPaint()

    Connections {
        target: EqualizerController
        function onResponseCurveChanged() { canvas.refresh() }
        function onGainRangeDbChanged() { canvas.requestPaint() }
    }

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        var plotWidth = width - plotLeft - plotRight

        for (var row = 0; row < 7; ++row) {
            var rowGain = gainRangeDb - row * gainRangeDb / 3
            var gridY = gainY(rowGain)
            ctx.beginPath()
            ctx.setLineDash(row === 3 ? [] : [4, 5])
            ctx.strokeStyle = row === 3 ? "#DCE2E6" : "#363B40"
            ctx.globalAlpha = row === 3 ? 0.72 : 0.82
            ctx.lineWidth = row === 3 ? 1.2 : 1
            ctx.moveTo(plotLeft, gridY)
            ctx.lineTo(width - plotRight, gridY)
            ctx.stroke()
        }

        ctx.setLineDash([4, 5])
        ctx.strokeStyle = "#30373D"
        ctx.lineWidth = 1
        ctx.globalAlpha = 0.75
        for (var axis = 0; axis < bandFrequencies.length; ++axis) {
            var gridX = markerXForIndex(axis)
            ctx.beginPath()
            ctx.moveTo(gridX, plotTop)
            ctx.lineTo(gridX, height - plotBottom)
            ctx.stroke()
        }
        ctx.setLineDash([])
        ctx.globalAlpha = 1

        if (!points || points.length < 2)
            return

        var gradient = ctx.createLinearGradient(plotLeft, 0,
                                                width - plotRight, 0)
        gradient.addColorStop(0, "#09AED9")
        gradient.addColorStop(0.52, "#176CF0")
        gradient.addColorStop(1, "#E35BD6")

        ctx.beginPath()
        for (var fillPoint = 0; fillPoint < points.length; ++fillPoint) {
            var fillX = plotLeft + fillPoint * plotWidth / (points.length - 1)
            var fillY = gainY(points[fillPoint])
            if (fillPoint === 0)
                ctx.moveTo(fillX, fillY)
            else
                ctx.lineTo(fillX, fillY)
        }
        ctx.lineTo(width - plotRight, gainY(-gainRangeDb))
        ctx.lineTo(plotLeft, gainY(-gainRangeDb))
        ctx.closePath()
        var fillGradient = ctx.createLinearGradient(0, plotTop, 0,
                                                    height - plotBottom)
        fillGradient.addColorStop(0, "rgba(27,117,174,0.12)")
        fillGradient.addColorStop(1, "rgba(27,117,174,0.01)")
        ctx.fillStyle = fillGradient
        ctx.fill()

        ctx.beginPath()
        for (var glowPoint = 0; glowPoint < points.length; ++glowPoint) {
            var glowX = plotLeft + glowPoint * plotWidth / (points.length - 1)
            var glowY = gainY(points[glowPoint])
            if (glowPoint === 0)
                ctx.moveTo(glowX, glowY)
            else
                ctx.lineTo(glowX, glowY)
        }
        ctx.globalAlpha = 0.24
        ctx.strokeStyle = gradient
        ctx.lineWidth = 7
        ctx.stroke()

        ctx.beginPath()
        for (var point = 0; point < points.length; ++point) {
            var x = plotLeft + point * plotWidth / (points.length - 1)
            var y = gainY(points[point])
            if (point === 0)
                ctx.moveTo(x, y)
            else
                ctx.lineTo(x, y)
        }
        ctx.globalAlpha = 1
        ctx.strokeStyle = gradient
        ctx.lineWidth = 2.2
        ctx.stroke()

        gainRevision
        for (var band = 0; band < bandFrequencies.length; ++band) {
            var nodeX = markerXForIndex(band)
            var nodeY = gainY(EqualizerController.bandGain(band))
            ctx.beginPath()
            ctx.arc(nodeX, nodeY, 8.5, 0, Math.PI * 2)
            ctx.fillStyle = band < 9 ? "#087FB6" : "#8439A8"
            ctx.fill()
            ctx.strokeStyle = "#F7FAFC"
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
            width: canvas.plotLeft - 18
            text: modelData.text
            color: "#EFF3F7"
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
            x: Math.round(canvas.markerXForIndex(index) - width / 2)
            y: canvas.height - canvas.plotBottom + 12
            text: canvas.frequencyLabels[index]
            color: "#D6DCE1"
            font.family: "Microsoft YaHei UI"
            font.pixelSize: canvas.width < 1000 ? 14 : 18
        }
    }
}
