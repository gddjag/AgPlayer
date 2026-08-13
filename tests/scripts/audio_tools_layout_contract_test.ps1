param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$toolsRoot = Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools'
$audioEditor = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'AudioEditorPage.qml')
$formatPage = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'FormatConvertPage.qml')
$formatSettings = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'FormatSettingsPanel.qml')
$metadataPage = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'MetadataEditPage.qml')
$filenamePage = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'FilenameProcessPage.qml')
$miniControls = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/MiniPlayerControls.qml')
$toolsWindow = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/AudioToolsWindow.qml')

foreach ($control in @(
    'editorCommandBar', 'fileSummaryBar', 'editorWaveformCanvas',
    'overviewNavigator', 'editorTransportBar', 'editorInspector',
    'recordingInspector', 'timePitchInspector', 'editorStatusBar')) {
    if ($audioEditor -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The audio editor is missing $control."
    }
}
if ($filenamePage -notmatch 'id:\s*rulesColumn') {
    throw 'The filename workbench needs an explicit responsive rules column.'
}
foreach ($control in @(
    'filenameFilePanel', 'filenameRulesPanel', 'filenamePreviewPanel',
    'filenameValidationPanel', 'filenameBottomBar')) {
    if ($filenamePage -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The filename reference workbench is missing $control."
    }
}
if (($filenamePage -notmatch 'id:\s*numberPositionBox') -or
    ($filenamePage -notmatch 'id:\s*preserveExtensionCheck')) {
    throw 'The filename rule panel must expose extension preservation and all numbering positions.'
}
if ($filenamePage -match 'objectName:\s*"filenameValidationPanel"[\s\S]{0,180}Layout\.preferredWidth:\s*190') {
    throw 'The filename validation panel must not squeeze the preview table into an unusable narrow column.'
}
if ($toolsWindow -notmatch 'width:\s*1672' -or $toolsWindow -notmatch 'height:\s*942') {
    throw 'The tools window must open at the complete reference-workbench size.'
}
if ($audioEditor -match 'LightEditor|MultiTrack|trackLane') {
    throw 'The new single-track editor must not retain legacy multitrack concepts.'
}
foreach ($page in @($formatPage, $metadataPage, $filenamePage)) {
    if ($page -match 'text:\s*qsTr\("从播放器添加"\)[\s\S]{0,220}enabled:\s*false') {
        throw 'A visible player-import button must never be permanently disabled.'
    }
}

if ($metadataPage -notmatch 'text:\s*qsTr\("取消"\)[\s\S]{0,120}visible:\s*true[\s\S]{0,120}enabled:\s*MetadataEditor\.busy') {
    throw 'Metadata cancel must remain visibly discoverable and only activate while a write is running.'
}
if ($filenamePage -notmatch 'text:\s*qsTr\("取消"\)[\s\S]{0,120}visible:\s*true[\s\S]{0,120}enabled:\s*FilenameProcessor\.busy') {
    throw 'Rename cancel must remain visibly discoverable and only activate while a transaction is running.'
}
if ($miniControls -match 'Layout\.preferredWidth:\s*expanded\s*\?') {
    throw 'Mini-player controls must not reference an undefined expanded property.'
}
if ($formatPage -notmatch 'objectName:\s*"formatSettingsPanel"[\s\S]{0,160}Layout\.preferredWidth:\s*(Math\.max\(360, page\.width \* 0\.265\)|445)') {
    throw 'The format converter needs a reference-width settings workbench.'
}
if ($metadataPage -notmatch 'Layout\.preferredWidth:\s*Math\.max\(480, page\.width \* 0\.36\)') {
    throw 'The metadata editor needs a complete batch-edit workbench at desktop width.'
}
if ($formatPage -notmatch 'enabled:\s*!converter\.busy\s*&&\s*\(modelData\.action !== "playlist"\s*\|\|\s*PlaybackController\.currentTrackId\.length > 0\)') {
    throw 'The converter player-import action must only be enabled while its controller can accept work.'
}
foreach ($control in @(
    'formatToolbar', 'formatSearchField', 'formatStatusFilters',
    'formatTaskPanel', 'formatSettingsPanel', 'formatBottomBar',
    'formatTotalProgress')) {
    if ($formatPage -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The reference conversion workbench is missing $control."
    }
}
foreach ($control in @(
    'formatOutputFormatGroup', 'formatEncodingSettingsGroup',
    'formatOutputOptionsGroup')) {
    if ($formatSettings -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The conversion settings panel is missing $control."
    }
}
if ($formatPage -notmatch 'converter\.buildPreflight\(' -or $formatPage -notmatch 'FormatPreflightDialog') {
    throw 'The converter must show real preflight differences before starting a changed plan.'
}
if ($metadataPage -notmatch 'enabled:\s*!MetadataEditor\.busy\s*&&\s*PlaybackController\.currentTrackId\.length > 0') {
    throw 'The metadata player-import action must only be enabled while its controller can accept work.'
}
if ($filenamePage -notmatch 'enabled:\s*!FilenameProcessor\.busy\s*&&\s*PlaybackController\.currentTrackId\.length > 0') {
    throw 'The filename player-import action must only be enabled while its controller can accept work.'
}
foreach ($page in @($formatPage, $metadataPage, $filenamePage)) {
    if ($page -notmatch 'function addCurrentPlayerTrack\(\)') {
        throw 'Each file tool must import the active player file through its real controller.'
    }
    if ($page -notmatch 'PlaybackController\.queueTrackIds') {
        throw 'The player import action must accept the active playback queue, not only one track.'
    }
}
if ($formatPage -notmatch 'converter\.addPlaylistPaths\(paths\)') {
    throw 'The converter must receive canonical native paths from the active playback queue.'
}
foreach ($page in @($metadataPage, $filenamePage)) {
    if ($page -notmatch 'Qt\.resolvedUrl\("file:///"') {
        throw 'Metadata and filename controllers must receive explicit local-file URLs on Windows.'
    }
}
