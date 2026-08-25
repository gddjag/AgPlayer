.pragma library

function clamp(value, minimum, maximum) {
    var number = Number(value)
    if (!isFinite(number))
        number = minimum
    return Math.min(maximum, Math.max(minimum, number))
}

function normalizeHex(value) {
    var text = String(value || "").trim().replace(/^#/, "")
    if (/^[0-9a-fA-F]{3}$/.test(text))
        text = text.replace(/(.)/g, "$1$1")
    return /^[0-9a-fA-F]{6}$/.test(text) ? "#" + text.toUpperCase() : ""
}

function hexToRgb(value) {
    var hex = normalizeHex(value)
    if (!hex)
        return null

    return {
        r: parseInt(hex.slice(1, 3), 16),
        g: parseInt(hex.slice(3, 5), 16),
        b: parseInt(hex.slice(5, 7), 16)
    }
}

function rgbToHex(r, g, b) {
    function channel(value) {
        var component = Math.round(clamp(value, 0, 255)).toString(16)
        return component.length === 1 ? "0" + component : component
    }

    return ("#" + channel(r) + channel(g) + channel(b)).toUpperCase()
}

function rgbToHsl(rgb) {
    var r = rgb.r / 255
    var g = rgb.g / 255
    var b = rgb.b / 255
    var maximum = Math.max(r, g, b)
    var minimum = Math.min(r, g, b)
    var lightness = (maximum + minimum) / 2
    var saturation = 0
    var hue = 0
    var delta = maximum - minimum

    if (delta !== 0) {
        saturation = delta / (1 - Math.abs(2 * lightness - 1))
        if (maximum === r)
            hue = ((g - b) / delta + (g < b ? 6 : 0)) / 6
        else if (maximum === g)
            hue = ((b - r) / delta + 2) / 6
        else
            hue = ((r - g) / delta + 4) / 6
    }

    return { h: hue, s: saturation, l: lightness }
}

function hslToRgb(hue, saturation, lightness) {
    var h = ((Number(hue) % 1) + 1) % 1
    var s = clamp(saturation, 0, 1)
    var l = clamp(lightness, 0, 1)
    var chroma = (1 - Math.abs(2 * l - 1)) * s
    var sector = h * 6
    var x = chroma * (1 - Math.abs(sector % 2 - 1))
    var r = 0
    var g = 0
    var b = 0

    if (sector < 1) {
        r = chroma
        g = x
    } else if (sector < 2) {
        r = x
        g = chroma
    } else if (sector < 3) {
        g = chroma
        b = x
    } else if (sector < 4) {
        g = x
        b = chroma
    } else if (sector < 5) {
        r = x
        b = chroma
    } else {
        r = chroma
        b = x
    }

    var match = l - chroma / 2
    return {
        r: Math.round((r + match) * 255),
        g: Math.round((g + match) * 255),
        b: Math.round((b + match) * 255)
    }
}

function buildPalette(value) {
    var base = hexToRgb(value)
    if (!base)
        return []

    var hsl = rgbToHsl(base)
    var l = hsl.l
    var s = hsl.s
    var steps = [
        [l + (1 - l) * 0.93, Math.min(1, s * 1.55)],
        [l + (1 - l) * 0.82, Math.min(1, s * 1.10)],
        [l + (1 - l) * 0.70, Math.min(1, s * 0.86)],
        [l + (1 - l) * 0.56, Math.min(1, s * 0.72)],
        [l + (1 - l) * 0.36, Math.min(1, s * 0.82)],
        [l, s],
        [l * 0.84, Math.min(1, s * 0.88)],
        [l * 0.68, Math.min(1, s * 0.96)],
        [l * 0.52, Math.min(1, s * 1.05)],
        [l * 0.36, Math.min(1, s * 1.12)]
    ]
    var palette = []

    for (var index = 0; index < steps.length; ++index) {
        if (index === 5) {
            palette.push(rgbToHex(base.r, base.g, base.b))
            continue
        }

        var rgb = hslToRgb(hsl.h, steps[index][1],
                           clamp(steps[index][0], 0.035, 0.98))
        palette.push(rgbToHex(rgb.r, rgb.g, rgb.b))
    }

    return palette
}

function isLight(value) {
    var rgb = hexToRgb(value)
    if (!rgb)
        return false

    function linearChannel(channel) {
        var component = channel / 255
        return component <= 0.04045
                ? component / 12.92
                : Math.pow((component + 0.055) / 1.055, 2.4)
    }

    var luminance = 0.2126 * linearChannel(rgb.r)
            + 0.7152 * linearChannel(rgb.g)
            + 0.0722 * linearChannel(rgb.b)
    return luminance > 0.46
}
