import QtQuick
import QtTest
import "../../app/qml/AgPlayer/components/ColorScale.js" as ColorScale

TestCase {
    name: "AgColorPicker"
    when: windowShown

    function test_normalization_and_rgb_round_trip() {
        compare(ColorScale.normalizeHex("63316b"), "#63316B")
        compare(ColorScale.normalizeHex("#abc"), "#AABBCC")
        compare(ColorScale.normalizeHex("not-a-color"), "")
        compare(ColorScale.rgbToHex(99, 49, 107), "#63316B")
        var rgb = ColorScale.hexToRgb("#63316B")
        compare(rgb.r, 99)
        compare(rgb.g, 49)
        compare(rgb.b, 107)
    }

    function test_reference_palette_is_exact() {
        var expected = [
            "#F8EBFA", "#E9D2EC", "#D6B9DB", "#C09CC6", "#A76BB0",
            "#63316B", "#512C57", "#432248", "#341938", "#251028"
        ]
        var actual = ColorScale.buildPalette("#63316B")
        compare(actual.length, expected.length)
        compare(actual.join(","), expected.join(","))
    }
}
