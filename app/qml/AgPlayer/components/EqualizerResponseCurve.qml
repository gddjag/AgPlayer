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
    readonly property bool compactFrequencyLabels: width < 1300
    readonly property int frequencyLabelRows: 1
    readonly property real frequencyLabelRowHeight:
        frequencyLabelProbe.implicitHeight + Theme.spacingXs
    readonly property real frequencyLabelReservedHeight:
        frequencyLabelRows * frequencyLabelRowHeight + Theme.spacingSm
    property real plotBottom: Math.max(50, frequencyLabelReservedHeight)
    property int gainRevision: 0
    readonly property real gainRangeDb: EqualizerController.gainRangeDb
    property var cachedResponse: []
    property real visibleRangeDb: gainRangeDb
    readonly property var bandFrequencies: [20, 31.5, 50, 80, 125, 200, 315,
                                            500, 800, 1250, 2000, 3150, 5000,
                                            8000, 10000, 12500, 16000, 20000]
    readonly property var frequencyLabels: ["20", "31.5", "50", "80", "125",
                                            "200", "315", "500", "800", "1.25k",
                                            "2k", "3.15k", "5k", "8k", "10k",
                                            "12.5k", "16k", "20k"]
    readonly property var wideFrequencyLabelIndices: [0, 2, 4, 6, 8, 10, 12,
                                                       14, 17]
    readonly property var compactFrequencyLabelIndices: [0, 3, 6, 9, 12, 14,
                                                          17]

    function bandX(index) {
        return frequencyX(bandFrequencies[index])
    }

    function frequencyX(frequency) {
        var ratio = Math.max(1, frequency) / 20
        return plotLeft + Math.log(ratio) / Math.log(1000)
               * (width - plotLeft - plotRight)
    }

    function showFrequencyLabel(index) {
        var indices = compactFrequencyLabels
                ? compactFrequencyLabelIndices : wideFrequencyLabelIndices
        return indices.indexOf(index) >= 0
    }

    function gainY(gain) {
        var range = Math.max(0.1, visibleRangeDb)
        var clamped = Math.max(-range, Math.min(range, gain))
        return plotTop + (range - clamped) / (range * 2)
               * (height - plotTop - plotBottom)
    }

    function responsePoints() {
        var result = []
        for (var index = 0; index < cachedResponse.length; ++index) {
            result.push({"x": plotLeft + index
                         / Math.max(1, cachedResponse.length - 1)
                         * (width - plotLeft - plotRight),
                         "y": gainY(cachedResponse[index])})
        }
        return result
    }

    function refreshResponse() {
        cachedResponse = EqualizerController.responseCurve(160)
        var range = gainRangeDb
        for (var index = 0; index < cachedResponse.length; ++index)
            range = Math.max(range, Math.ceil(Math.abs(cachedResponse[index]) / 3) * 3)
        visibleRangeDb = range
        requestPaint()
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
    onGainRevisionChanged: refreshResponse()
    Component.onCompleted: refreshResponse()

    Connections {
        target: EqualizerController
        function onResponseCurveChanged() { canvas.refreshResponse() }
        function onGainRangeDbChanged() { canvas.refreshResponse() }
        function onSampleRateChanged() { canvas.refreshResponse() }
        function onSampleRateSupportedChanged() { canvas.refreshResponse() }
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
            var rowGain = visibleRangeDb - row * visibleRangeDb / 3
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
        var response = responsePoints()
        if (response.length < 2)
            return

        var gradient = ctx.createLinearGradient(plotLeft, 0,
                                                width - plotRight, 0)
        gradient.addColorStop(0, "#09AED9") // theme-color-allow: equalizer response visualization
        gradient.addColorStop(0.52, "#176CF0") // theme-color-allow: equalizer response visualization
        gradient.addColorStop(1, "#E35BD6") // theme-color-allow: equalizer response visualization

        ctx.beginPath()
        traceEnvelope(ctx, response)
        ctx.lineTo(response[response.length - 1].x, gainY(-visibleRangeDb))
        ctx.lineTo(response[0].x, gainY(-visibleRangeDb))
        ctx.closePath()
        var fillGradient = ctx.createLinearGradient(0, plotTop, 0,
                                                    height - plotBottom)
        fillGradient.addColorStop(0, "rgba(27,117,174,0.12)")
        fillGradient.addColorStop(1, "rgba(27,117,174,0.01)")
        ctx.fillStyle = fillGradient
        ctx.fill()

        ctx.beginPath()
        traceEnvelope(ctx, response)
        ctx.globalAlpha = 0.24
        ctx.strokeStyle = gradient
        ctx.lineWidth = 7
        ctx.stroke()

        ctx.beginPath()
        traceEnvelope(ctx, response)
        ctx.globalAlpha = 1
        ctx.strokeStyle = gradient
        ctx.lineWidth = 2.2
        ctx.stroke()

    }

    Repeater {
        model: [{"text": "+" + canvas.visibleRangeDb.toFixed(0) + " dB",
                 "gain": canvas.visibleRangeDb},
                {"text": "0 dB", "gain": 0},
                {"text": "−" + canvas.visibleRangeDb.toFixed(0) + " dB",
                 "gain": -canvas.visibleRangeDb}]
        Label {
            required property var modelData
            x: 12
            y: Math.round(canvas.gainY(modelData.gain) - height / 2)
            width: canvas.gridLeft - 18
            text: modelData.text
            color: Theme.textPrimary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeBody
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
            y: canvas.height - canvas.plotBottom + Theme.spacingXs
            text: canvas.frequencyLabels[index]
            visible: canvas.showFrequencyLabel(index)
            color: Theme.textSecondary
            font.family: Theme.fontPrimary
            font.pixelSize: Theme.fontSizeCaption
        }
    }

    Label {
        id: frequencyLabelProbe
        visible: false
        text: "20 kHz"
        font.family: Theme.fontPrimary
        font.pixelSize: Theme.fontSizeCaption
    }
}
