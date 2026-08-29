pragma Singleton
import QtQuick
import AgPlayer

QtObject {
    id: root

    // Canonical complete-palette tokens. The C++ singleton owns all adaptive
    // color decisions and the native QPalette; this façade keeps existing QML
    // consumers source-compatible while migration proceeds page by page.
    readonly property bool isLight: ThemeManager.isLight
    readonly property color background: ThemeManager.background
    readonly property color surface: ThemeManager.surface
    readonly property color surfaceElevated: ThemeManager.surfaceElevated
    readonly property color surfaceHover: ThemeManager.surfaceHover
    readonly property color surfacePressed: ThemeManager.surfacePressed
    readonly property color textPrimary: ThemeManager.textPrimary
    readonly property color textSecondary: ThemeManager.textSecondary
    readonly property color textTertiary: ThemeManager.textTertiary
    readonly property color textDisabled: ThemeManager.textDisabled
    readonly property color border: ThemeManager.border
    readonly property color borderStrong: ThemeManager.borderStrong
    readonly property color divider: ThemeManager.divider
    readonly property color disabled: ThemeManager.disabled
    readonly property color accent: ThemeManager.accent
    readonly property color accentHover: ThemeManager.accentHover
    readonly property color accentPressed: ThemeManager.accentPressed
    readonly property color accentSoft: ThemeManager.accentSoft
    readonly property color accentText: ThemeManager.accentText
    readonly property color accentBorder: ThemeManager.accent
    readonly property color focus: ThemeManager.focus
    readonly property color highlight: ThemeManager.highlight
    readonly property color highlightHover: ThemeManager.highlightHover
    readonly property color highlightPressed: ThemeManager.highlightPressed
    readonly property color highlightSoft: ThemeManager.highlightSoft
    readonly property color highlightText: ThemeManager.highlightText
    readonly property color highlightBorder: ThemeManager.highlight
    readonly property color selectionGlassFill: Qt.rgba(
                                                      highlight.r,
                                                      highlight.g,
                                                      highlight.b, 0.68)
    readonly property color selectionGlassHover: Qt.rgba(
                                                       highlightHover.r,
                                                       highlightHover.g,
                                                       highlightHover.b, 0.68)
    readonly property color selectionGlassPressed: Qt.rgba(
                                                         highlightPressed.r,
                                                         highlightPressed.g,
                                                         highlightPressed.b, 0.68)
    readonly property color selectionGlassBorder: Qt.rgba(1, 1, 1, 0.28)
    readonly property color subtleGlassFill: isLight
                                             ? Qt.rgba(1, 1, 1, 0.28)
                                             : Qt.rgba(1, 1, 1, 0.05)
    readonly property color subtleGlassHover: isLight
                                              ? Qt.rgba(1, 1, 1, 0.42)
                                              : Qt.rgba(1, 1, 1, 0.09)
    readonly property color subtleGlassActive: Qt.rgba(
                                                    highlight.r,
                                                    highlight.g,
                                                    highlight.b,
                                                    isLight ? 0.38 : 0.18)
    readonly property color subtleGlassBorder: isLight
                                               ? Qt.rgba(0, 0, 0, 0.16)
                                               : Qt.rgba(1, 1, 1, 0.20)
    readonly property color navigatorGlassTrack: isLight
                                                 ? Qt.rgba(0, 0, 0, 0.14)
                                                 : Qt.rgba(1, 1, 1, 0.15)
    readonly property color navigatorGlassThumb: Qt.rgba(
                                                     highlight.r,
                                                     highlight.g,
                                                     highlight.b, 0.54)
    readonly property color success: ThemeManager.success
    readonly property color warning: ThemeManager.warning
    readonly property color error: ThemeManager.error
    readonly property color danger: ThemeManager.danger
    readonly property color recording: ThemeManager.recording
    readonly property color critical: ThemeManager.critical

    // Compatibility aliases for existing QML pages.
    readonly property int mode: SettingsController.themeMode
    readonly property int requestedMode: mode
    readonly property bool followsSystem: mode === 2
    readonly property bool systemIsLight: ThemeManager.isLight
    readonly property int effectiveMode: isLight ? 1 : 0
    readonly property color panel: surface
    readonly property color elevated: surfaceElevated
    readonly property color hoverSurface: surfaceHover
    readonly property color primaryText: textPrimary
    readonly property color secondaryText: textSecondary
    readonly property color cyan: accent
    readonly property color violet: accent
    readonly property color activeSelection: highlight
    readonly property color activeSelectionText: highlightText
    readonly property color inactiveSelection: highlight
    readonly property color inactiveSelectionText: highlightText
    readonly property color currentTrackSurface: ThemeManager.currentTrackSurface
    readonly property color currentTrackInactiveSurface: currentTrackSurface
    readonly property color currentTrackSelection: currentTrackSurface
    readonly property color selectedTrackSelection: highlightSoft
    readonly property color currentTrackSelectionInactive: currentTrackSurface
    readonly property color selectedTrackSelectionInactive: highlightSoft
    readonly property color onCyanText: accentText
    readonly property color onBrandGradientText: "#FFFFFF"
    readonly property color iconPrimary: primaryText
    readonly property color iconSecondary: secondaryText
    readonly property color iconAccent: accent
    readonly property color playButtonBorder: borderStrong

    // Visualizer, waveform, editor and equalizer colors intentionally remain
    // media-domain constants; they never derive from Accent or Highlight.
    readonly property color waveformCyan: "#00D4FF"
    readonly property color waveformBlue: "#1688FF"
    readonly property color waveformGreen: "#00E676"
    readonly property color waveformViolet: "#7B2FF7"
    readonly property color waveformMagenta: "#E62E9B"
    readonly property color waveformRed: "#FF4057"
    readonly property color editorCanvas: surface
    readonly property color editorRuler: surfaceElevated
    readonly property color editorOverview: surfaceHover
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
    readonly property color listWorkspaceSurface: surface
    readonly property color listWorkspaceBorder: borderStrong
    readonly property color listDivider: divider
    readonly property color listHeaderSurface: surfaceElevated
    readonly property color listSelectedSurface: highlightSoft
    readonly property color tagAddSurface: accentSoft
    readonly property color tagSecondaryText: textSecondary
    readonly property color listWaveformMono: isLight ? "#6B5A70" : "#C7B8CB"
    readonly property color tagPillSurface: surface
    readonly property color tagPillHoverSurface: surfaceHover
    readonly property color tagPillSelectedSurface: highlightSoft
    readonly property color tagPillDropSurface: accentSoft
    readonly property color tagPillBorder: border
    readonly property color tagPillHighlightBorder: highlightBorder
    readonly property color tagPillText: textPrimary
    readonly property color tagPillSecondaryText: textSecondary
    readonly property color tagPillShadow: "#30000000"

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
    readonly property string fontPrimary: Qt.application.font.family
    readonly property string fontFallback: Qt.platform.os === "windows"
                                           ? "Microsoft YaHei UI"
                                           : Qt.application.font.family
    readonly property string iconPrefix: "qrc:/qt/qml/AgPlayer/assets/icons/"
    function icon(name) { return iconPrefix + name + ".svg" }
    function ratingColor(index) { return ratingGold }
}
