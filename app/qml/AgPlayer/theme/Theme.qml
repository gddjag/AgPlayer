pragma Singleton
import QtQuick

QtObject {
    id: root

    property SystemPalette systemPalette: SystemPalette {
        colorGroup: SystemPalette.Active
    }
    property SystemPalette inactiveSystemPalette: SystemPalette {
        colorGroup: SystemPalette.Inactive
    }

    // Main.qml synchronizes this value from the runtime-registered settings
    // singleton. Keeping the palette state here makes every window update at once.
    property int mode: 0

    readonly property int requestedMode: mode
    readonly property bool followsSystem: mode === 2
    readonly property bool systemIsLight:
        Application.styleHints.colorScheme === Qt.Light
    readonly property int effectiveMode: followsSystem
                                         ? (systemIsLight ? 1 : 0)
                                         : (mode === 1 ? 1 : 0)
    readonly property bool isLight: effectiveMode === 1

    readonly property color background: isLight ? "#F3F3F3" : "#071018"
    readonly property color panel: isLight ? "#FFFFFF" : "#0B1721"
    readonly property color elevated: isLight ? "#F9F9F9" : "#101E28"
    readonly property color border: isLight ? "#D1D1D1" : "#203340"
    readonly property color hoverSurface: isLight ? "#EAEAEA" : "#172A37"
    readonly property color accent: systemPalette.highlight
    readonly property color activeSelection: systemPalette.highlight
    readonly property color activeSelectionText: systemPalette.highlightedText
    readonly property color inactiveSelection: inactiveSystemPalette.highlight
    readonly property color inactiveSelectionText: inactiveSystemPalette.highlightedText
    readonly property color cyan: accent
    readonly property color violet: accent
    readonly property color favoriteRed: "#FF334D"
    readonly property color ratingGold: "#FFD700"
    readonly property color primaryText: isLight ? "#1B1B1B" : "#FFFFFF"
    readonly property color secondaryText: isLight ? "#5D5D5D" : "#CFCFCF"
    readonly property color onCyanText: systemPalette.highlightedText
    readonly property color onBrandGradientText: "#FFFFFF"
    readonly property color accentText: systemPalette.highlightedText
    readonly property color iconPrimary: primaryText
    readonly property color iconSecondary: secondaryText
    readonly property color iconAccent: cyan
    readonly property color playButtonBorder: isLight ? "#1B1B1B" : "#FFFFFF"
    readonly property color playRingPlaying: waveformGreen
    readonly property color playRingPaused: "#FFB020"

    readonly property int radiusSm: 8
    readonly property int radiusMd: 12
    readonly property int radiusLg: 18
    readonly property int windowRadius: Qt.platform.os === "osx" ? 10
                                         : Qt.platform.os === "windows" ? 8
                                         : 8

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24
    readonly property int spacing2Xl: 32

    readonly property string fontPrimary: Qt.application.font.family
    readonly property string fontFallback: Qt.platform.os === "windows"
                                           ? "Microsoft YaHei UI"
                                           : Qt.application.font.family

    readonly property color waveformCyan: "#00D4FF"
    readonly property color waveformBlue: "#1688FF"
    readonly property color waveformGreen: "#00E676"
    readonly property color waveformViolet: "#7B2FF7"
    readonly property color waveformMagenta: "#E62E9B"
    readonly property color waveformRed: "#FF4057"

    readonly property string iconPrefix: "qrc:/qt/qml/AgPlayer/assets/icons/"
    function icon(name) { return iconPrefix + name + ".svg" }
    function ratingColor(index) {
        return ["#FFF4B8", "#FFE98A", "#FFE05C", "#FFD62E", "#FFCC00"][
                    Math.max(0, Math.min(4, index))]
    }
}
