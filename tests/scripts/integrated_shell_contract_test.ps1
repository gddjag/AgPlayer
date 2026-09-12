param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'

function Assert-Match {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Message
    )
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

$main = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/Main.qml') -Raw
$shell = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/IntegratedPlayerShell.qml') -Raw
$sidePanel = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/LibrarySidePanel.qml') -Raw
$controls = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/PlayerControls.qml') -Raw
$integratedControls = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/IntegratedPlayerControls.qml') -Raw
$experience = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/ExperienceActions.qml') -Raw
$selection = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/WaveSelectionOverlay.qml') -Raw

Assert-Match $main 'id:\s*shellLoader[\s\S]*sourceComponent:\s*mainWindow\.rollingShell\s*\?\s*rollingShellComponent[\s\S]*mainWindow\.integratedShell[\s\S]*integratedShellComponent\s*:\s*classicShellComponent' 'Main.qml must switch Classic, Integrated, and Rolling through one Loader.'
Assert-Match $main 'LibraryFilterModel\s*\{[\s\S]*id:\s*sharedFilterModel' 'Main.qml must own the shared filter model.'
Assert-Match $main 'WaveformSession\s*\{[\s\S]*id:\s*sharedWaveformSession' 'Main.qml must own the persistent waveform session.'
Assert-Match $shell 'property int topBarHeight:\s*Theme\.titleBarHeight' 'Integrated top bar must use the shared title-bar height.'
Assert-Match $shell 'property int leftColumnWidth:\s*width < 1300\s*\?\s*Theme\.navigationWidthCompact\s*:\s*Theme\.navigationWidth' 'Integrated left column must use the shared responsive navigation widths.'
Assert-Match $shell 'property int rightColumnWidth:\s*Theme\.playerInspectorWidth' 'Integrated right column must use the shared inspector width.'
Assert-Match $shell 'property int waveformHeight:\s*120' 'Integrated waveform must be 120px.'
Assert-Match $shell 'property int bottomBarHeight:\s*Theme\.playerBottomBarHeight' 'Integrated bottom bar must use the shared player-bar height.'
Assert-Match $shell 'SideNavigation\s*\{[\s\S]*showTagManagementEntry:\s*false' 'Integrated navigation must hide the tag-management entry.'
Assert-Match $shell 'LibrarySidePanel\s*\{[\s\S]*objectNamePrefix:\s*"integrated"[\s\S]*expandedWidth:\s*root\.rightColumnWidth' 'Integrated must reuse the shared library side panel.'
Assert-Match $sidePanel 'TagManagementPanel\s*\{[\s\S]*compact:\s*true[\s\S]*collapsible:\s*false[\s\S]*showHeader:\s*false' 'The shared side panel must reuse the compact tag panel.'
Assert-Match $sidePanel 'objectName:\s*root\.objectNamePrefix \+ "TagTabButton"[\s\S]*objectName:\s*root\.objectNamePrefix \+ "LyricsTabButton"[\s\S]*objectName:\s*root\.objectNamePrefix \+ "SidePanelToggleButton"' 'The shared side panel must expose tag and lyrics tabs plus the side-panel toggle.'
Assert-Match $shell 'WaveSelectionOverlay\s*\{[\s\S]*dragAdapter:\s*PlaybackClipDragAdapter' 'Integrated selection must use the shared drag adapter.'
Assert-Match $selection 'function\s+forwardZoom\(wheel, source\)[\s\S]*zoomRequested' 'The top selection overlay must forward Ctrl+wheel zoom.'
$zoomForwardCount = ([regex]::Matches(
    $selection, 'onWheel:\s*function\(wheel\)\s*\{\s*root\.forwardZoom\(wheel,')).Count
if ($zoomForwardCount -lt 4) {
    throw 'Every selection interaction surface must forward Ctrl+wheel zoom.'
}
Assert-Match $shell 'onZoomRequested:\s*function\(x, factor\)[\s\S]*waveform\.zoomAt\(x, factor\)' 'Integrated must apply zoom forwarded by the selection overlay.'
Assert-Match $shell 'id:\s*waveform[\s\S]*objectName:\s*"integratedWaveform"[\s\S]*position:\s*0[\s\S]*cursorPosition:\s*root\.playbackPositionMs' 'Integrated base waveform must remain unplayed while its cursor follows playback.'
if ($shell -match 'id:\s*waveform[\s\S]*?(?<![A-Za-z])position:\s*root\.playbackPositionMs') {
    throw 'Integrated playback progress must not be painted by a single position-coloured waveform.'
}
Assert-Match $shell 'objectName:\s*"integratedWaveformPlayedClip"[\s\S]*width:\s*waveform\.waveformCursorX[\s\S]*clip:\s*true[\s\S]*objectName:\s*"integratedPlayedWaveform"[\s\S]*width:\s*waveform\.width[\s\S]*position:\s*duration' 'Integrated playback progress must use a full played waveform clipped to the exact cursor pixel.'
Assert-Match $shell 'function\s+applyWaveformMode\(\)[\s\S]*waveform\.peaks\s*=\s*spectrum[\s\S]*playedWaveform\.peaks\s*=\s*spectrum[\s\S]*waveform\.layers\s*=\s*layers[\s\S]*playedWaveform\.layers\s*=\s*layers' 'Integrated display-mode changes must assign exactly one matching data source to both waveform layers.'
Assert-Match $shell 'objectName:\s*"integratedWaveformNavigator"[\s\S]*waveform\.setVisibleRange' 'Integrated must provide a draggable zoom-position navigator.'
Assert-Match $main 'id:\s*integratedBottomBarComponent[\s\S]*IntegratedPlayerControls\s*\{' 'Integrated shell must inject its dedicated control layout.'
Assert-Match $shell 'item\.leftReservedWidth\s*=\s*Qt\.binding[\s\S]*return\s+trackSummary\.width' 'Integrated controls must reserve the complete track-summary region.'
Assert-Match $integratedControls 'objectName:\s*"integratedCenterControls"[\s\S]*TransportControls[\s\S]*PlayerVolumeControl[\s\S]*objectName:\s*"integratedRightActions"' 'Integrated controls must keep transport and volume together in the centered control group.'
Assert-Match $integratedControls '(?s)objectName:\s*"integratedCenterControls".*objectName:\s*"audioToolsButton".*objectName:\s*"experienceActions".*allowLyrics:\s*false.*PlayerVolumeControl' 'Integrated center actions must keep audio tools, transport, and volume around the centered play control without duplicating the side-panel lyrics control.'
Assert-Match $integratedControls '(?s)objectName:\s*"integratedRightActions".*objectName:\s*"themeModeButton".*objectName:\s*"immersiveExperienceActions".*objectName:\s*"miniPlayerButton"' 'Integrated right actions must keep theme, immersive, and mini-player ordered as specified.'
if ($integratedControls -match 'objectName:\s*"windowLayoutButton"') {
    throw 'Integrated controls must expose one combined theme/shell entry'
}
Assert-Match $integratedControls 'objectName:\s*"classicShellMenuItem"[\s\S]*objectName:\s*"integratedShellMenuItem"[\s\S]*objectName:\s*"rollingShellMenuItem"' 'Integrated layout selection must retain all three player shells.'
if ($integratedControls -match 'showListWindowButton|objectName:\s*"playerShellModeButton"') {
    throw 'Integrated controls must not retain obsolete Classic-only layout toggles.'
}
Assert-Match $controls 'objectName:\s*"playerSecondaryActions"[\s\S]*ExperienceActions\s*\{' 'Shared experience actions must remain in the right action group.'
if ($controls -match 'objectName:\s*"playerShellModeButton"') {
    throw 'Playback controls must not expose the removed shell-mode button.'
}
Assert-Match $experience 'AnimatedImmersiveIcon\s*\{' 'Immersive mode must use the shared animated vector icon.'
Assert-Match $experience 'Theme\.icon\("lyrics"\)' 'Lyrics must use the supplied icon asset.'

Write-Host 'Integrated shell source contract passed.'
