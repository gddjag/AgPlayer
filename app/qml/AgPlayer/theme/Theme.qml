pragma Singleton
import QtQuick
import AgPlayer as Runtime

QtObject {
    id: root

    readonly property int mode: Runtime.SettingsController.themeMode
    readonly property int requestedMode: mode
    readonly property bool followsSystem: mode === 2
    readonly property bool systemIsLight:
        Application.styleHints.colorScheme === Qt.Light
    readonly property int effectiveMode: followsSystem
                                         ? (systemIsLight ? 1 : 0)
                                         : (mode === 1 ? 1 : 0)
    readonly property bool isLight: effectiveMode === 1

    // Fixed three-mode palette. Custom seeds, generated gradients and glass
    // derivation are intentionally absent.
    readonly property color background: isLight ? "#F3F3F3" : "#071018"
    readonly property color surface: isLight ? "#FFFFFF" : "#0B1721"
    readonly property color surfaceElevated: isLight ? "#F9F9F9" : "#101E28"
    readonly property color surfaceHover: isLight ? "#EAEAEA" : "#172A37"
    readonly property color surfacePressed: isLight ? "#DEDEDE" : "#1D3443"
    readonly property color textPrimary: isLight ? "#1B1B1B" : "#FFFFFF"
    readonly property color textSecondary: isLight ? "#5D5D5D" : "#CFCFCF"
    readonly property color textTertiary: isLight ? "#767676" : "#9EABB5"
    readonly property color textDisabled: isLight ? "#9A9A9A" : "#73808A"
    readonly property color opaqueBorder: isLight ? "#D1D1D1" : "#203340"
    readonly property color borderStrong: isLight ? "#AFAFAF" : "#385064"
    readonly property color opaqueDivider: isLight ? "#DEDEDE" : "#1A2A35"
    readonly property color disabled: isLight ? "#E4E4E4" : "#15232D"

    readonly property color accent: "#007AFF"
    readonly property color accentHover: "#1A86FF"
    readonly property color accentPressed: "#0068D9"
    readonly property color accentSoft: isLight ? "#1F007AFF" : "#33007AFF"
    readonly property color accentText: "#FFFFFF"
    readonly property color accentBorder: accent
    readonly property color focus: accent
    readonly property color highlight: accent
    readonly property color highlightHover: accentHover
    readonly property color highlightPressed: accentPressed
    readonly property color highlightSoft: isLight ? "#26007AFF" : "#3D007AFF"
    readonly property color highlightText: "#FFFFFF"
    readonly property color highlightBorder: accent

    readonly property color success: "#22C55E"
    readonly property color warning: "#F59E0B"
    readonly property color error: "#EF4444"
    readonly property color danger: error
    readonly property color recording: "#FF4057"
    readonly property color critical: error

    // Compatibility aliases retained for existing controls.
    readonly property color panel: surface
    readonly property color elevated: surfaceElevated
    readonly property color hoverSurface: surfaceHover
    readonly property color pressedSurface: surfacePressed
    readonly property color border: opaqueBorder
    readonly property color divider: opaqueDivider
    readonly property color primaryText: textPrimary
    readonly property color secondaryText: textSecondary
    readonly property color cyan: accent
    readonly property color violet: accent
    readonly property color activeSelection: highlight
    readonly property color activeSelectionText: highlightText
    readonly property color inactiveSelection: highlight
    readonly property color inactiveSelectionText: highlightText
    readonly property color currentTrackSurface: "#578F57C9"
    readonly property color currentTrackInactiveSurface: currentTrackSurface
    readonly property color currentTrackSelection: currentTrackSurface
    readonly property color selectedTrackSelection: "#2E007AFF"
    readonly property color currentTrackSelectionInactive: currentTrackSurface
    readonly property color selectedTrackSelectionInactive: "#2E007AFF"
    readonly property color onCyanText: accentText
    readonly property color onBrandGradientText: "#FFFFFF"
    readonly property color iconPrimary: primaryText
    readonly property color iconSecondary: secondaryText
    readonly property color iconAccent: accent
    readonly property color playButtonBorder: isLight ? "#1B1B1B" : "#FFFFFF"

    // Shared compatibility tokens for the integrated, immersive and editor
    // surfaces. They are derived only from the fixed light/dark palette so
    // every shell changes together without reviving custom theme state.
    readonly property color controlSubtleBorder: isLight ? "#73D1D1D1"
                                                         : "#A6203340"
    readonly property color controlHandle: accentText
    readonly property color controlHandleShadow: "#29000000"
    readonly property color selectionGlassFill: "#AD007AFF"
    readonly property color selectionGlassHover: "#AD1A86FF"
    readonly property color selectionGlassPressed: "#AD0068D9"
    readonly property color selectionGlassBorder: "#47FFFFFF"
    readonly property color subtleGlassFill: isLight ? "#47FFFFFF" : "#0DFFFFFF"
    readonly property color subtleGlassHover: isLight ? "#6BFFFFFF" : "#17FFFFFF"
    readonly property color subtleGlassActive: isLight ? "#61007AFF" : "#2E007AFF"
    readonly property color subtleGlassBorder: isLight ? "#29000000" : "#33FFFFFF"
    readonly property color integratedSoftOutline: isLight ? "#1A000000"
                                                           : "#1AFFFFFF"
    readonly property color integratedSliderHandle: accentText
    readonly property color integratedGlassHighlight: "#29FFFFFF"
    readonly property color navigatorGlassTrack: isLight ? "#12000000"
                                                         : "#14FFFFFF"
    readonly property color navigatorGlassThumb: "#47007AFF"
    readonly property color glassSurface: surface
    readonly property color glassSurfaceElevated: surfaceElevated
    readonly property color glassSurfaceHover: surfaceHover
    readonly property color glassSurfacePressed: surfacePressed
    readonly property color glassBorder: border
    readonly property color glassDivider: divider

    // Media-domain colors stay independent of the appearance mode.
    readonly property color waveformCyan: "#00D4FF"
    readonly property color waveformBlue: "#1688FF"
    readonly property color waveformGreen: "#00E676"
    readonly property color waveformViolet: "#7B2FF7"
    readonly property color waveformMagenta: "#E62E9B"
    readonly property color waveformRed: "#FF4057"
    readonly property color editorCanvas: isLight ? "#F7FAFA" : "#11191B"
    readonly property color editorRuler: isLight ? "#EEF3F3" : "#151F21"
    readonly property color editorOverview: isLight ? "#EAF2F2" : "#132124"
    readonly property color editorWaveform: isLight ? "#169B97" : "#39C7C0"
    readonly property color editorOverviewWaveform: isLight ? "#2B9692" : "#297E7B"
    readonly property color editorSelection: isLight ? "#26169B97" : "#2639C7C0"
    readonly property color editorOverviewSelection: isLight ? "#122B9692" : "#12297E7B"
    readonly property color editorSelectionLabel: "#FF8A00"
    readonly property color editorPlayhead: "#FFAF00"
    readonly property color playRingPlaying: waveformGreen
    readonly property color playRingPaused: "#FFB020"

    readonly property color favoriteRed: "#FF334D"
    readonly property color ratingGold: "#FF9800"
    readonly property color listWorkspaceSurface: isLight ? "#FCFAFD" : "#06101F"
    readonly property color listWorkspaceBorder: isLight ? "#B8A9BC" : "#60475F"
    readonly property color listDivider: isLight ? "#D8CFDC" : "#33283D"
    readonly property color listHeaderSurface: isLight ? "#F4EFF6" : "#091728"
    readonly property color listSelectedSurface: isLight ? "#E7DCEF" : "#231238"
    readonly property color tagAddSurface: isLight ? "#EEE2F3" : "#241039"
    readonly property color tagSecondaryText: isLight ? "#745B43" : "#C8A77D"
    readonly property color listWaveformMono: isLight ? "#6B5A70" : "#C7B8CB"
    readonly property color tagPillSurface: isLight ? "#B8FFFFFF" : "#D11A2B3B"
    readonly property color tagPillHoverSurface: isLight ? "#E0DEEDF7" : "#EB213D54"
    readonly property color tagPillSelectedSurface: isLight ? "#33407DB3" : "#66407DB3"
    readonly property color tagPillDropSurface: isLight ? "#45458CC2" : "#80428FC2"
    readonly property color tagPillBorder: isLight ? "#A5B8C6" : "#496477"
    readonly property color tagPillHighlightBorder: isLight ? "#5A89AA" : "#76A8CB"
    readonly property color tagPillText: isLight ? "#21313D" : "#EFF7FC"
    readonly property color tagPillSecondaryText: isLight ? "#526B7C" : "#B9CCDA"
    readonly property color tagPillShadow: isLight ? "#241A2E3D" : "#4D000000"

    readonly property int radiusSm: 8
    readonly property int radiusMd: 12
    readonly property int radiusLg: 18
    readonly property int windowRadius: Qt.platform.os === "osx" ? 10 : 8
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24
    readonly property int spacing2Xl: 32
    readonly property int navigationIconVisualSize: 18
    readonly property int navigationActionExtent: 28
    readonly property string fontPrimary: Qt.application.font.family
    readonly property string fontFallback: Qt.platform.os === "windows"
                                           ? "Microsoft YaHei UI"
                                           : Qt.application.font.family
    readonly property string iconPrefix: "qrc:/qt/qml/AgPlayer/assets/icons/"
    function icon(name) { return iconPrefix + name + ".svg" }
    function ratingColor(index) { return ratingGold }
}
