pragma Singleton
import QtQuick

QtObject {
    readonly property color background: "#0A0A0F"
    readonly property color panel: "#0E1118"
    readonly property color border: "#253140"
    readonly property color cyan: "#00D4FF"
    readonly property color violet: "#7B2FF7"
    readonly property color favoriteRed: "#FF334D"
    readonly property color primaryText: "#F5F7FA"
    readonly property color secondaryText: "#9AA4B2"

    readonly property int radiusSm: 8
    readonly property int radiusMd: 12
    readonly property int radiusLg: 18

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24
    readonly property int spacing2Xl: 32

    readonly property string fontPrimary: "Microsoft YaHei UI"
    readonly property string fontFallback: "Segoe UI"

    readonly property color waveformCyan: "#00D4FF"
    readonly property color waveformBlue: "#1688FF"
    readonly property color waveformGreen: "#00E676"
    readonly property color waveformViolet: "#7B2FF7"
    readonly property color waveformMagenta: "#E62E9B"
    readonly property color waveformRed: "#FF4057"

    readonly property string iconPrefix: "qrc:/qt/qml/AgPlayer/assets/icons/"
    function icon(name) { return iconPrefix + name + ".svg" }
}
