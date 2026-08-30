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
$controls = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/PlayerControls.qml') -Raw
$integratedControls = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/IntegratedPlayerControls.qml') -Raw
$experience = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/ExperienceActions.qml') -Raw
$selection = Get-Content -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/WaveSelectionOverlay.qml') -Raw

Assert-Match $main 'id:\s*shellLoader[\s\S]*sourceComponent:\s*mainWindow\.integratedShell' 'Main.qml must switch one shell through Loader.'
Assert-Match $main 'LibraryFilterModel\s*\{[\s\S]*id:\s*sharedFilterModel' 'Main.qml must own the shared filter model.'
Assert-Match $main 'WaveformSession\s*\{[\s\S]*id:\s*sharedWaveformSession' 'Main.qml must own the persistent waveform session.'
Assert-Match $shell 'property int topBarHeight:\s*52' 'Integrated top bar must be 52px.'
Assert-Match $shell 'property int leftColumnWidth:\s*248' 'Integrated left column must be 248px.'
Assert-Match $shell 'property int rightColumnWidth:\s*312' 'Integrated right column must be 312px.'
Assert-Match $shell 'property int waveformHeight:\s*120' 'Integrated waveform must be 120px.'
Assert-Match $shell 'property int bottomBarHeight:\s*91' 'Integrated bottom bar must be 91px.'
Assert-Match $shell 'SideNavigation\s*\{[\s\S]*showTagManagementEntry:\s*false' 'Integrated navigation must hide the tag-management entry.'
Assert-Match $shell 'TagManagementPanel\s*\{[\s\S]*compact:\s*true[\s\S]*collapsible:\s*false[\s\S]*showHeader:\s*false' 'Integrated must reuse the shared compact tag panel inside its tab container.'
Assert-Match $shell 'objectName:\s*"integratedTagTabButton"[\s\S]*objectName:\s*"integratedLyricsTabButton"[\s\S]*objectName:\s*"integratedSidePanelToggleButton"' 'Integrated must expose tag and lyrics tabs plus the side-panel toggle.'
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
Assert-Match $integratedControls 'objectName:\s*"integratedPlayerControls"[\s\S]*objectName:\s*"listWindowButton"[\s\S]*objectName:\s*"audioToolsButton"[\s\S]*TransportControls[\s\S]*ExperienceActions\s*\{\s*showImmersive:\s*false;\s*showLyrics:\s*true[\s\S]*ExperienceActions\s*\{\s*showImmersive:\s*true;\s*showLyrics:\s*false[\s\S]*objectName:\s*"windowLayoutButton"' 'Integrated controls must retain the dedicated ordered action layout.'
if ($integratedControls -match 'showListWindowButton|miniPlayerButton') {
    throw 'Integrated controls must not retain the removed Classic toggle or mini-player action.'
}
Assert-Match $controls 'objectName:\s*"playerSecondaryActions"[\s\S]*ExperienceActions\s*\{' 'Shared experience actions must remain in the right action group.'
if ($controls -match 'objectName:\s*"playerShellModeButton"') {
    throw 'Playback controls must not expose the removed shell-mode button.'
}
Assert-Match $experience 'Theme\.icon\("immersive-visual-mode"\)' 'Immersive mode must use the supplied icon asset.'
Assert-Match $experience 'Theme\.icon\("lyrics"\)' 'Lyrics must use the supplied icon asset.'

Write-Host 'Integrated shell source contract passed.'
