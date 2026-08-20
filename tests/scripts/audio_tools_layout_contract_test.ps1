param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$toolsRoot = Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools'
$audioEditor = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'AudioEditorPage.qml')
$formatPage = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'FormatConvertPage.qml')
$formatSettings = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'FormatSettingsPanel.qml')
$formatTable = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'FormatTaskTable.qml')
$formatSurface = $formatPage + "`n" + $formatSettings + "`n" + $formatTable
$metadataPage = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'MetadataEditPage.qml')
$filenamePage = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'FilenameProcessPage.qml')
$miniControls = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/MiniPlayerControls.qml')
$toolsWindow = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/AudioToolsWindow.qml')
$toolsNavigation = Get-Content -Raw -LiteralPath (Join-Path $toolsRoot 'ToolSidebar.qml')
$settingsPage = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/SettingsPage.qml')
$equalizerWindow = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/EqualizerWindow.qml')
$recordingInspector = Get-Content -Raw -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/components/audioeditor/RecordingInspectorSection.qml')
$transportBar = Get-Content -Raw -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/components/audioeditor/EditorTransportBar.qml')
$commandBar = Get-Content -Raw -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml')
$waveformCanvas = Get-Content -Raw -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/components/audioeditor/EditorWaveformCanvas.qml')

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
if ($toolsNavigation -notmatch 'objectName:\s*"audioToolsTopNav"' -or
    $toolsNavigation -notmatch 'RowLayout' -or
    $toolsNavigation -notmatch 'radius:\s*Theme\.radiusMd') {
    throw 'The four audio tools must remain in the selected top horizontal pill navigation.'
}
if ($settingsPage -notmatch 'designRole:\s*"settingsCategoryRail"' -or
    $settingsPage -notmatch 'designRole:\s*"settingsContentSurface"' -or
    $settingsPage -match 'Segoe UI Symbol') {
    throw 'Settings must use the selected left-category and right-content layout.'
}
foreach ($control in @(
    'equalizerHeaderPanel', 'equalizerResponsePanel',
    'equalizerBandsPanel', 'equalizerFooterPanel')) {
    if ($equalizerWindow -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The scheme-3 EQ layout is missing $control."
    }
}
if ($audioEditor -match 'LightEditor|MultiTrack|trackLane') {
    throw 'The new single-track editor must not retain legacy multitrack concepts.'
}
if ($recordingInspector -match 'text:\s*.*qsTr\("开始录音"\)') {
    throw 'The recording inspector configures recording but must not contain a hidden start button.'
}
foreach ($behavior in @('recordingRequested', 'pauseRecording', 'resumeRecording')) {
    if ($transportBar -notmatch $behavior) {
        throw "The transport bar is missing recording behavior: $behavior."
    }
}
foreach ($obsoleteMarkerControl in @(
    'transportAddMarker', 'transportPreviousMarker', 'transportNextMarker')) {
    if ($transportBar -match ('objectName:\s*"?' + $obsoleteMarkerControl + '"?')) {
        throw "The editor must not retain obsolete marker control: $obsoleteMarkerControl."
    }
}
foreach ($duplicate in @('volume-up-fill', 'setVolume\(', '波形缩放')) {
    if ($transportBar -match $duplicate) {
        throw "The transport bar still contains a duplicate player/overview control: $duplicate."
    }
}
foreach ($shortcut in @('Ctrl\+T', 'Ctrl\+L', 'Ctrl\+Alt\+I', 'Ctrl\+Alt\+O')) {
    if ($commandBar -notmatch $shortcut) {
        throw "The editor command bar is missing the shortcut: $shortcut."
    }
}
if ($commandBar -notmatch 'ToolTip\.text:\s*hoverText') {
    throw 'Editor command buttons must show function help and shortcut on hover.'
}
if ($waveformCanvas -notmatch 'onWheel:' -or
    $waveformCanvas -notmatch 'Qt\.ControlModifier') {
    throw 'The waveform canvas must support conventional Ctrl+wheel zoom.'
}
if ($transportBar -match 'Slider\s*\{\s*Layout\.preferredWidth:\s*90;\s*value:\s*0\.5;\s*enabled:\s*false') {
    throw 'The transport zoom control must be connected to the live editor viewport.'
}
if ($audioEditor -notmatch 'AudioEditorController\.cancelRecording\(\)') {
    throw 'The recording state machine cancel action must be reachable from the editor UI.'
}
if ((($audioEditor + "`n" + $recordingInspector) | Select-String -Pattern 'text:\s*qsTr\("取消录音"\)' -AllMatches).Matches.Count -ne 1) {
    throw 'The editor must expose exactly one visible cancel-recording action.'
}
foreach ($exportControl in @(
    'exportSettingsDialog', 'exportRangeBox', 'exportFormatBox',
    'exportSampleRateBox', 'exportChannelBox', 'exportQualityBox')) {
    if ($audioEditor -notmatch ('id:\s*' + $exportControl)) {
        throw "The independent export dialog is missing $exportControl."
    }
}
if ($audioEditor -notmatch 'AudioEditorController\.exportTo\([\s\S]{0,520}exportQualityBox\.value') {
    throw 'The independent export parameters must be passed to DocumentWriter through the controller.'
}
if ($audioEditor -notmatch 'AudioEditorController\.exportFormats') {
    throw 'Export formats must come from the current FFmpeg build capabilities.'
}
$timePitchInspector = Get-Content -Raw -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/components/audioeditor/TimePitchInspectorSection.qml')
if ($timePitchInspector -notmatch 'id:\s*originalBpmControl' -or
    $timePitchInspector -notmatch 'AudioEditorController\.setOriginalBpm\(value\)') {
    throw 'Original BPM must support validated manual input.'
}
foreach ($preference in @(
    'recordingDeviceId', 'recordingSampleRate',
    'recordingChannels', 'recordingMonitor')) {
    if ($recordingInspector -notmatch ('AudioEditorController\.' + $preference)) {
        throw "The recording inspector is not restoring $preference."
    }
}
$audioEditorComponents = Get-Content -Raw -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/components/audioeditor/EditorCommandBar.qml')
if ($audioEditorComponents -notmatch 'Accessible\.name:\s*label' -or
    $audioEditorComponents -notmatch 'ToolTip\.text:\s*hoverText') {
    throw 'Audio editor command actions need accessible names and tooltips.'
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
if ($formatPage -notmatch 'objectName:\s*"formatSettingsPanel"[\s\S]{0,320}Layout\.preferredWidth:\s*settingsPanel\.expanded[\s\S]{0,80}\?\s*\(page\.compactLayout\s*\?\s*360\s*:\s*445\)\s*:\s*40') {
    throw 'The format converter needs a reference-width settings workbench.'
}
if ($metadataPage -notmatch 'Layout\.preferredWidth:\s*page\.compactLayout[\s\S]{0,180}Math\.max\(480, page\.width \* 0\.36\)') {
    throw 'The metadata editor needs a complete batch-edit workbench at desktop width.'
}
if ($formatPage -notmatch 'enabled:\s*!converter\.busy[\s\S]{0,140}PlaybackController\.currentTrackId\.length > 0') {
    throw 'The converter player-import action must only be enabled while its controller can accept work.'
}
foreach ($control in @(
    'formatToolbar', 'formatStatusFilters',
    'formatTaskPanel', 'formatSettingsPanel', 'formatBottomBar',
    'formatOutputFormatGroup', 'formatEncodingSettingsGroup',
    'formatOutputOptionsGroup', 'formatTotalProgress')) {
    if ($formatSurface -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The reference conversion workbench is missing $control."
    }
}
if ($formatPage -notmatch 'converter\.(previewSelected|buildPreflight)\(' -or $formatPage -notmatch '(formatPreflightDialog|FormatPreflightDialog)') {
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
    if ($page -notmatch 'file:///') {
        throw 'Player file paths must be converted to explicit local-file URLs on Windows.'
    }
    if ($page -notmatch 'PlaybackController\.queueTrackIds') {
        throw 'The player import action must accept the active playback queue, not only one track.'
    }
    if ($page -notmatch 'Qt\.resolvedUrl\("file:///"') {
        throw 'The player import action must hand the native controller QUrl values, not bare file strings.'
    }
}
