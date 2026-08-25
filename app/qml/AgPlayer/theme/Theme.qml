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
    readonly property color editorCanvas: isLight ? "#F7FAFA" : "#11191B"
    readonly property color editorRuler: isLight ? "#EEF3F3" : "#151F21"
    readonly property color editorOverview: isLight ? "#EAF2F2" : "#132124"
    readonly property color editorWaveform: isLight ? "#169B97" : "#39C7C0"
    readonly property color editorOverviewWaveform: isLight ? "#2B9692" : "#297E7B"
    readonly property color editorSelection: Qt.rgba(
        editorWaveform.r, editorWaveform.g, editorWaveform.b, 0.15)
    readonly property color editorOverviewSelection: Qt.rgba(
        editorWaveform.r, editorWaveform.g, editorWaveform.b, 0.07)
    readonly property color accent: systemPalette.highlight
    readonly property color activeSelection: systemPalette.highlight
    readonly property color activeSelectionText: systemPalette.highlightedText
    readonly property color inactiveSelection: inactiveSystemPalette.highlight
    readonly property color inactiveSelectionText: inactiveSystemPalette.highlightedText
    readonly property color currentTrackSurface: isLight
        ? Qt.rgba(0.47, 0.25, 0.67, 0.18) : Qt.rgba(0.56, 0.34, 0.79, 0.34)
    readonly property color currentTrackInactiveSurface: isLight
        ? Qt.rgba(0.47, 0.25, 0.67, 0.11) : Qt.rgba(0.56, 0.34, 0.79, 0.20)
    readonly property color currentTrackSelection: currentTrackSurface
    readonly property color selectedTrackSelection: Qt.rgba(
        activeSelection.r, activeSelection.g, activeSelection.b, 0.18)
    readonly property color currentTrackSelectionInactive: currentTrackInactiveSurface
    readonly property color selectedTrackSelectionInactive: Qt.rgba(
        inactiveSelection.r, inactiveSelection.g, inactiveSelection.b, 0.18)
    readonly property color cyan: accent
    readonly property color violet: accent
    readonly property color favoriteRed: "#FF334D"
    readonly property color ratingGold: "#FF9800"
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

    // Shared list/tag workspace semantics. These keep the reference's near-black
    // blue and muted purple hierarchy while retaining light-theme contrast.
    readonly property color listWorkspaceSurface: isLight ? "#FCFAFD" : "#06101F"
    readonly property color listWorkspaceBorder: isLight ? "#B8A9BC" : "#60475F"
    readonly property color listDivider: isLight ? "#D8CFDC" : "#33283D"
    readonly property color listHeaderSurface: isLight ? "#F4EFF6" : "#091728"
    readonly property color listSelectedSurface: isLight ? "#E7DCEF" : "#231238"
    readonly property color tagAddSurface: isLight ? "#EEE2F3" : "#241039"
    readonly property color tagSecondaryText: isLight ? "#745B43" : "#C8A77D"
    readonly property color listWaveformMono: isLight ? "#6B5A70" : "#C7B8CB"

    // Tag capsules use opacity, highlights and a one-pixel border instead of
    // per-item blur effects, keeping the dense tag column cheap to render.
    readonly property color tagPillSurface: isLight
                                           ? Qt.rgba(1.0, 1.0, 1.0, 0.72)
                                           : Qt.rgba(0.10, 0.17, 0.23, 0.82)
    readonly property color tagPillHoverSurface: isLight
                                                ? Qt.rgba(0.87, 0.93, 0.97, 0.88)
                                                : Qt.rgba(0.13, 0.24, 0.33, 0.92)
    readonly property color tagPillSelectedSurface: isLight
                                                   ? Qt.rgba(0.25, 0.49, 0.70, 0.20)
                                                   : Qt.rgba(0.25, 0.49, 0.70, 0.40)
    readonly property color tagPillDropSurface: isLight
                                               ? Qt.rgba(0.27, 0.55, 0.76, 0.27)
                                               : Qt.rgba(0.26, 0.56, 0.76, 0.50)
    readonly property color tagPillBorder: isLight ? "#A5B8C6" : "#496477"
    readonly property color tagPillHighlightBorder: isLight ? "#5A89AA" : "#76A8CB"
    readonly property color tagPillText: isLight ? "#21313D" : "#EFF7FC"
    readonly property color tagPillSecondaryText: isLight ? "#526B7C" : "#B9CCDA"
    readonly property color tagPillShadow: isLight
                                          ? Qt.rgba(0.10, 0.18, 0.24, 0.14)
                                          : Qt.rgba(0.0, 0.0, 0.0, 0.30)

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
        return ratingGold
    }
}
