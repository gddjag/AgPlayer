pragma Singleton
import QtQuick

QtObject {
    id: root

    // Main.qml synchronizes this value from the runtime-registered settings
    // singleton. Keeping the palette state here makes every window update at once.
    property int mode: 0

    readonly property bool followsSystem: mode === 2
    readonly property bool systemIsLight: Application.styleHints.colorScheme === Qt.Light
                                          || (Application.styleHints.colorScheme === Qt.Unknown
                                              && systemPalette.window.hslLightness > 0.5)
    readonly property bool isLight: mode === 1
                                    || (followsSystem && systemIsLight)
    readonly property SystemPalette systemPalette: SystemPalette {
        colorGroup: SystemPalette.Active
    }

    readonly property color background: followsSystem
                                        ? systemPalette.window
                                        : isLight ? "#F3F3F3" : "#050914"
    readonly property color panel: followsSystem
                                   ? systemPalette.base
                                   : isLight ? "#FFFFFF" : "#07101F"
    readonly property color elevated: followsSystem
                                      ? systemPalette.button
                                      : isLight ? "#F9FAFC" : "#0B1627"
    readonly property color border: followsSystem
                                    ? systemPalette.mid
                                    : isLight ? "#C7CDD7" : "#454A55"
    readonly property color hoverSurface: followsSystem
                                          ? systemPalette.alternateBase
                                          : isLight ? "#E7EBF1" : "#152238"
    readonly property color cyan: followsSystem
                                  ? systemPalette.highlight
                                  : "#00D4FF"
    readonly property color violet: "#7B2FF7"
    readonly property color favoriteRed: "#FF334D"
    readonly property color primaryText: followsSystem
                                         ? systemPalette.windowText
                                         : isLight ? "#17181A" : "#F5F7FA"
    readonly property color secondaryText: followsSystem
                                           ? systemPalette.placeholderText
                                           : isLight ? "#5E6570" : "#9AA4B2"
    readonly property color accentText: followsSystem
                                        ? systemPalette.highlightedText
                                        : "#FFFFFF"
    readonly property color iconPrimary: primaryText
    readonly property color iconSecondary: secondaryText
    readonly property color iconAccent: cyan

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
