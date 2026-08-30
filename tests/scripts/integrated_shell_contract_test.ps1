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
Assert-Match $shell 'function\s+applyWaveformMode\(\)[\s\S]*waveform\.peaks\s*=\s*spectrum[\s\S]*waveform\.layers\s*=\s*layers' 'Integrated must restore exactly one waveform data source when switching display modes.'
Assert-Match $shell 'objectName:\s*"integratedWaveformNavigator"[\s\S]*waveform\.setVisibleRange' 'Integrated must provide a draggable zoom-position navigator.'
Assert-Match $main 'showListWindowButton:\s*false' 'Integrated controls must not expose the Classic list-window button.'
Assert-Match $controls 'objectName:\s*"playerSecondaryActions"[\s\S]*objectName:\s*"playerShellModeButton"[\s\S]*Theme\.icon\("player-shell-mode"\)' 'Both shells must share the uploaded theme-switch icon in the right action group.'
Assert-Match $controls 'objectName:\s*"modeButton"[\s\S]*ExperienceActions\s*\{' 'Shared experience actions must follow the playback-mode button.'

Write-Host 'Integrated shell source contract passed.'
