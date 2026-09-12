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

    // Every shell consumes the user's unplayed-region brightness directly.
    // Themes must never add an opacity floor: doing so hides progress in dark
    // modes and makes the same setting behave differently between windows.
    readonly property real nonImmersiveSpectralUnplayedOpacity:
        Math.max(0.0, Math.min(1.0,
            Number(Runtime.SettingsController
                   .frequencyColorWaveform.unplayedOpacity)))

    function waveformProgressFraction(positionMs, durationMs) {
        var duration = Number(durationMs || 0)
        if (duration <= 0)
            return 0
        return Math.max(0, Math.min(1, Number(positionMs || 0) / duration))
    }

    function waveformProgressClipWidth(availableWidth, positionMs, durationMs) {
        return Math.max(0, Number(availableWidth || 0))
                * waveformProgressFraction(positionMs, durationMs)
    }

    // Codex-like window hierarchy with WeChat-like neutral density. Pages use
    // semantic surfaces instead of inferring hierarchy from raw colours.
    readonly property color titleBarSurface: isLight ? "#E5E8ED" : "#202329"
    readonly property color navigationSurface: isLight ? "#ECEEF2" : "#24272D"
    readonly property color contentSurface: isLight ? "#FAFAFB" : "#181A1D"
    readonly property color surface: isLight ? "#FFFFFF" : "#1E2125"
    readonly property color surfaceElevated: isLight ? "#FFFFFF" : "#25282E"
    readonly property color surfaceHover: isLight ? "#E4E7EC" : "#2B2F35"
    readonly property color surfacePressed: isLight ? "#D9DDE4" : "#343941"
    readonly property color selectedSurface: isLight ? "#E8E8FB" : "#302A45"
    readonly property color background: contentSurface
    readonly property color textPrimary: isLight ? "#202328" : "#F2F3F5"
    readonly property color textSecondary: isLight ? "#5E646D" : "#B7BBC2"
    readonly property color textTertiary: isLight ? "#898F98" : "#858B95"
    readonly property color textDisabled: isLight ? "#ADB2BA" : "#666C75"
    readonly property color opaqueBorder: isLight ? "#D5D9E0" : "#353941"
    readonly property color borderStrong: isLight ? "#B9BEC7" : "#4A505A"
    readonly property color opaqueDivider: opaqueBorder
    readonly property color disabled: isLight ? "#E5E8ED" : "#2A2D32"

    // Both appearances use the same purple brand family. Light mode keeps a
    // deeper fill for contrast; dark mode uses a brighter violet. Interaction
    // variants stay in-family instead of falling back to the old blue.
    readonly property color accent: isLight ? "#6D28D9" : "#7657E8"
    readonly property color accentHover: "#7C3AED"
    readonly property color accentPressed: isLight ? "#5B21B6" : "#6D28D9"
    readonly property color accentSoft: isLight ? "#1F6D28D9" : "#4D7657E8"
    readonly property color accentText: "#FFFFFF"
    readonly property color accentBorder: accent
    readonly property color focus: accent
    readonly property color highlight: accent
    readonly property color highlightHover: accentHover
    readonly property color highlightPressed: accentPressed
    readonly property color highlightSoft: accentSoft
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
    readonly property color currentTrackSurface: accent
    readonly property color currentTrackInactiveSurface: currentTrackSurface
    readonly property color currentTrackSelection: currentTrackSurface
    readonly property color selectedTrackSelection: accentSoft
    readonly property color currentTrackSelectionInactive: currentTrackSurface
    readonly property color selectedTrackSelectionInactive: accentSoft
    readonly property color onCyanText: accentText
    readonly property color onBrandGradientText: "#FFFFFF"
    readonly property color iconPrimary: primaryText
    readonly property color iconSecondary: secondaryText
    readonly property color iconAccent: accent
    readonly property color playButtonBorder: isLight ? "#1B1B1B" : "#FFFFFF"

    // Shared compatibility tokens for the integrated, immersive and editor
    // surfaces. They are derived only from the fixed light/dark palette so
    // every shell changes together without reviving custom theme state.
    readonly property color controlSubtleBorder: opaqueBorder
    readonly property color controlHandle: accentText
    readonly property color controlHandleShadow: "#29000000"
    readonly property color selectionGlassFill: accent
    readonly property color selectionGlassHover: accentHover
    readonly property color selectionGlassPressed: accentPressed
    readonly property color selectionGlassBorder: accentBorder
    readonly property color subtleGlassFill: surfaceElevated
    readonly property color subtleGlassHover: surfaceHover
    readonly property color subtleGlassActive: selectedSurface
    readonly property color subtleGlassBorder: opaqueBorder
    readonly property color integratedSoftOutline: opaqueBorder
    readonly property color integratedSliderHandle: accentText
    readonly property color integratedGlassHighlight: surfaceHover
    readonly property color navigatorGlassTrack: opaqueDivider
    readonly property color navigatorGlassThumb: accentSoft
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
    readonly property color playbackGuide: "#8B5CF6"
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

    // The analysis workbench shares the same surfaces as every audio tool.
    readonly property color losslessWorkspaceSurface: background
    readonly property color losslessPanelSurface: surface
    readonly property color losslessPanelHeaderSurface: surfaceElevated
    readonly property color losslessTableAlternateSurface: surface
    readonly property color losslessGrid: opaqueDivider
    readonly property color losslessSpectrum: isLight ? "#0087B8" : "#19C4F1"
    readonly property color losslessSpectrumFill: isLight ? "#260087B8" : "#4219C4F1"
    readonly property color losslessVerdictCredible: "#35C759"
    readonly property color losslessVerdictTranscode: "#FF5757"
    readonly property color losslessVerdictUpsample: "#F3A51B"
    readonly property color losslessVerdictInconclusive: isLight ? "#647481" : "#9BACB8"
    readonly property int losslessTitleBarHeight: 50
    readonly property int losslessNavigationHeight: 56
    readonly property int losslessNavigationItemWidth: 160
    readonly property int losslessToolbarHeight: 60
    readonly property int losslessBottomBarHeight: 96
    readonly property int losslessControlHeight: 40
    readonly property int losslessProminentControlHeight: 60
    readonly property int losslessFontSizeMeta: 14
    readonly property int losslessFontSizeBody: 16
    readonly property int losslessFontSizeSection: 20
    readonly property int losslessFontSizeTitle: 24

    readonly property color favoriteRed: "#FF334D"
    readonly property color ratingGold: "#FF9800"
    readonly property color listWorkspaceSurface: contentSurface
    readonly property color listWorkspaceBorder: opaqueBorder
    readonly property color listDivider: opaqueDivider
    readonly property color listHeaderSurface: isLight ? "#F4F5F7" : "#1D2024"
    readonly property color listSelectedSurface: selectedSurface
    readonly property color tagAddSurface: selectedSurface
    readonly property color tagSecondaryText: textSecondary
    readonly property color listWaveformMono: isLight ? "#8A9099" : "#D5D8DE"
    readonly property color tagPillSurface: isLight ? "#B8FFFFFF" : "#D11A2B3B"
    readonly property color tagPillHoverSurface: isLight ? "#E0DEEDF7" : "#EB213D54"
    readonly property color tagPillSelectedSurface: isLight ? "#33407DB3" : "#66407DB3"
    readonly property color tagPillDropSurface: isLight ? "#45458CC2" : "#80428FC2"
    readonly property color tagPillBorder: isLight ? "#A5B8C6" : "#496477"
    readonly property color tagPillHighlightBorder: isLight ? "#5A89AA" : "#76A8CB"
    readonly property color tagPillText: isLight ? "#21313D" : "#EFF7FC"
    readonly property color tagPillSecondaryText: isLight ? "#526B7C" : "#B9CCDA"
    readonly property color tagPillShadow: isLight ? "#241A2E3D" : "#4D000000"
    // Shared by every player shell because TagManagementPanel is shared.
    readonly property int tagCapsuleHeight: 24
    readonly property int tagCapsuleRadius: 10
    readonly property real tagCapsuleBorderWidth: 1.4
    readonly property color tagCapsuleLightText: "#FFFFFF"
    readonly property color tagCapsuleDarkText: "#000000"
    function tagCapsuleFilledText(fill) {
        function linear(channel) {
            return channel <= 0.04045 ? channel / 12.92
                                     : Math.pow((channel + 0.055) / 1.055, 2.4)
        }
        var luminance = 0.2126 * linear(fill.r) + 0.7152 * linear(fill.g)
                + 0.0722 * linear(fill.b)
        return luminance > 0.179 ? tagCapsuleDarkText : tagCapsuleLightText
    }

    readonly property int radiusXs: 4
    readonly property int radiusSm: 6
    readonly property int radiusMd: 8
    readonly property int radiusLg: 8
    readonly property int windowRadius: Qt.platform.os === "osx" ? 10 : 8
    // Platform window controls; preserve the application's existing theme.
    readonly property color macWindowClose: "#ff5f57"
    readonly property color macWindowMinimize: "#febc2e"
    readonly property color macWindowFullScreen: "#28c840"
    readonly property color macWindowInactive: "#98989d"
    readonly property color macWindowGlyph: "#252525"
    readonly property int macWindowDotExtent: 12
    readonly property int macWindowGlyphExtent: 10
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24
    readonly property int spacing2Xl: 32
    readonly property int fontSizeCaption: 12
    readonly property int fontSizeTagCapsule: 12
    readonly property int fontSizeMeta: 12
    readonly property int fontSizeBody: 14
    readonly property int fontSizeBodyStrong: 14
    readonly property int fontSizeSection: 16
    readonly property int fontSizePageTitle: 20
    readonly property int titleBarHeight: 40
    readonly property int navigationWidthCompact: 192
    readonly property int navigationWidth: 216
    readonly property int navigationWidthExpanded: 240
    readonly property int playerInspectorWidth: 280
    readonly property int playerTagPanelWidth: 264
    readonly property int playerBottomBarHeight: 80
    readonly property int rollingOverviewHeight: 108
    readonly property int rollingOverviewHeightCompact: 92
    readonly property int rollingWaveformHeight: 136
    readonly property int rollingWaveformHeightCompact: 124
    readonly property int controlHeightCompact: 28
    readonly property int controlHeight: 32
    readonly property int controlHeightProminent: 36
    readonly property int navigationRowHeight: 36
    readonly property int listRowHeight: 38
    readonly property int mediaListRowHeight: 46
    readonly property int settingsRowHeight: 48
    readonly property int tableHeaderHeight: 36
    readonly property int sliderTrackHeight: 2
    readonly property int sliderHandleExtent: 10
    readonly property int minimumInteractionExtent: 28
    readonly property int iconSizeSm: 16
    readonly property int iconSizeMd: 18
    readonly property int iconSizeLg: 24
    readonly property int navigationIconVisualSize: 21
    readonly property int navigationActionExtent: 28
    readonly property string fontPrimary: Qt.platform.os === "windows"
                                          ? "Microsoft YaHei UI"
                                          : Qt.application.font.family
    readonly property string fontFallback: Qt.platform.os === "windows"
                                           ? "Segoe UI Variable"
                                           : Qt.application.font.family
    readonly property string iconPrefix: "qrc:/qt/qml/AgPlayer/assets/icons/"
    function icon(name) { return iconPrefix + name + ".svg" }
    function ratingColor(index) { return ratingGold }
}
