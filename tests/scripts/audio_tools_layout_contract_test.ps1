param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
function ConvertFrom-Utf8Base64([string]$Value) {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Value))
}
$toolsRoot = Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools'
$editorRoot = Join-Path $SourceRoot 'app/qml/AgPlayer/components/audioeditor'
$audioEditor = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'AudioEditorPage.qml')
$commandBar = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $editorRoot 'EditorCommandBar.qml')
$waveformCanvas = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $editorRoot 'EditorWaveformCanvas.qml')
$toolsWindow = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/AudioToolsWindow.qml')
$toolsNavigation = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'ToolSidebar.qml')
$formatPage = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'FormatConvertPage.qml')
$formatSettings = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'FormatSettingsPanel.qml')
$formatTable = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'FormatTaskTable.qml')
$formatSurface = $formatPage + "`n" + $formatSettings + "`n" + $formatTable
$metadataPage = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'MetadataEditPage.qml')
$filenamePage = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'FilenameProcessPage.qml')
$miniControls = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/components/MiniPlayerControls.qml')
$appCmake = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $SourceRoot 'app/CMakeLists.txt')
$controllerHeader = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'qt/src/audio_editor/audio_editor_controller.hpp')
$controllerSource = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'qt/src/audio_editor/audio_editor_controller.cpp')
$qaMatrix = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'scripts/qa-audio-tools-matrix.ps1')
$qaFinalMatrix = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'scripts/qa-final-ui-matrix.ps1')
$qaComparisonPath = Join-Path $SourceRoot `
    'scripts/qa-audio-editor-reference-compare.ps1'

if ($audioEditor -notmatch 'color:\s*Theme\.background' -or
    $formatPage -notmatch 'color:\s*Theme\.background' -or
    $metadataPage -notmatch 'color:\s*Theme\.background') {
    throw 'Every audio-tool page root must use the shared light/dark Theme background.'
}
if ($metadataPage -notmatch 'canvasColor:\s*Theme\.editorCanvas' -or
    $metadataPage -notmatch 'panelColor:\s*Theme\.panel' -or
    $metadataPage -notmatch 'inputColor:\s*Theme\.elevated') {
    throw 'Metadata surfaces must use the shared Theme tokens.'
}
if ($formatSurface -match '#(?:0f1820|101a21|0c1821|09141c|0a151d)' -or
    $audioEditor -match '#(?:031426|041628|071a2d|06182a|05172a)') {
    throw 'Audio editor and converter still contain fixed dark-only surfaces.'
}

foreach ($control in @(
    'editorMainColumn', 'editorInspector', 'editorCommandBar', 'fileSummaryBar',
    'editorTimelineWorkspace', 'editorTrackHeader', 'editorTimeRuler',
    'editorWaveformCanvas', 'editorTimelineScrollbar',
    'editorRecordingTransport', 'editorPlaybackTransport',
    'editorShortcutCard', 'editorStatusBar')) {
    if ($audioEditor -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The Phase 6 audio editor is missing $control."
    }
}

if ($toolsWindow -notmatch 'width:\s*1672' -or
    $toolsWindow -notmatch 'height:\s*941' -or
    $toolsWindow -notmatch 'Layout\.preferredHeight:\s*49' -or
    $toolsWindow -notmatch 'Layout\.preferredHeight:\s*43' -or
    $toolsWindow -notmatch 'title:\s*qsTr\("AgPlayer') {
    throw 'The tools shell must match the 1672x941 title/nav geometry and title.'
}
if ($toolsWindow -notmatch 'objectName:\s*"audioToolsContentStack"') {
    throw 'The tools content stack must expose the Phase 6 acceptance object name.'
}
if ($toolsWindow -match '#(?:031426|10283d|c42b35)' -or
    $toolsWindow -notmatch 'objectName:\s*"audioToolsTitleBar"[\s\S]{0,180}color:\s*Theme\.panel' -or
    $toolsWindow -notmatch 'objectName:\s*"audioToolsWindowTitle"[\s\S]{0,160}color:\s*Theme\.primaryText' -or
    $toolsWindow -notmatch 'parent\.hovered\s*\?\s*Theme\.hoverSurface' -or
    $toolsWindow -notmatch 'parent\.hovered\s*\?\s*Theme\.favoriteRed' -or
    $toolsWindow -notmatch 'icon\.color:\s*Theme\.iconPrimary') {
    throw 'The audio-tools title bar, title, hover surfaces and window icons must use Theme tokens.'
}
if ($formatPage -match 'tint:\s*"#d7e0e6"' -or
    $formatPage -notmatch 'objectName:\s*"formatToolbarIcon-"\s*\+\s*modelData\.action' -or
    $formatPage -notmatch 'tint:\s*Theme\.iconPrimary') {
    throw 'Format toolbar icons must expose stable objects and use the shared theme icon color.'
}
$toolSidebar = Get-Content -LiteralPath (Join-Path $SourceRoot `
    'app/qml/AgPlayer/components/tools/ToolSidebar.qml') -Raw
if ($toolSidebar -notmatch 'activeLabelColor:\s*Theme\.primaryText' -or
    $toolSidebar -notmatch 'parent\.checked\s*\?\s*navigation\.activeLabelColor') {
    throw 'Selected audio-tool navigation labels must remain readable on the panel.'
}
if ($audioEditor -notmatch 'sequence:\s*"Space"' -or
    $audioEditor -notmatch 'onActivated:\s*AudioEditorController\.playPause\(\)') {
    throw 'The composed tools shell is missing its real Space playback shortcut.'
}
if ($toolsWindow -match 'Layout\.(left|right|bottom)Margin:\s*[1-9]') {
    throw 'The tools content stack must occupy the complete 0,92,1672,849 area.'
}
if ($qaMatrix -notmatch '"1672x941"' -or $qaMatrix -match '"1672x942"') {
    throw 'The audio-tools QA matrix must capture the exact 1672x941 reference size.'
}
if ($qaFinalMatrix -match 'Width\s*=\s*1672;\s*Height\s*=\s*942') {
    throw 'The final UI matrix still expects the obsolete editor height.'
}
if (-not (Test-Path -LiteralPath $qaComparisonPath)) {
    throw 'The Phase 6 source/candidate comparison and difference-mask script is missing.'
}
if ($toolsNavigation -notmatch 'anchors\.leftMargin:\s*49' -or
    $toolsNavigation -notmatch 'height:\s*3' -or
    $toolsNavigation -match 'radius:\s*Theme\.radiusMd' -or
    $toolsNavigation -match 'ThemedIcon') {
    throw 'Audio tool tabs must be left-aligned text with a blue underline, not pills.'
}
$navOrder = @(
    (ConvertFrom-Utf8Base64 '6Z+z6aKR57yW6L6R'),
    (ConvertFrom-Utf8Base64 '5qC85byP6L2s5o2i'),
    (ConvertFrom-Utf8Base64 '5YWD5pWw5o2u5L+u5pS5'),
    (ConvertFrom-Utf8Base64 '5paH5Lu25ZCN5aSE55CG'))
$previous = -1
foreach ($label in $navOrder) {
    $position = $toolsNavigation.IndexOf($label)
    if ($position -le $previous) {
        throw "The tool navigation is missing or out of order: $label"
    }
    $previous = $position
}

$commandOrder = @(
    'importAudio', 'saveProject', 'select', 'split', 'delete', 'crop',
    'copy', 'paste', 'fadeIn', 'fadeOut', 'mute', 'noiseReduction', 'clear')
$previous = -1
foreach ($command in $commandOrder) {
    $position = $commandBar.IndexOf(('commandName: "' + $command + '"'))
    if ($position -le $previous) {
        throw "The reference toolbar is missing or out of order: $command"
    }
    $previous = $position
}
foreach ($laterPhaseAction in @('cropToSelection', 'fadeIn', 'fadeOut', 'silenceSelection')) {
    if ($commandBar -notmatch ('actionEnabled\(\s*"editor\.' + $laterPhaseAction + '"\)')) {
        throw "The later-phase command must retain its honest disabled state: $laterPhaseAction"
    }
}
if ($commandBar -notmatch 'clearDocument\(\)' -or
    $commandBar -match 'clearTransientState\(\)') {
    throw 'Clear must clear the loaded timeline document.'
}
foreach ($obsolete in @(
    'insertSilence', 'exportMenu',
    'gainRequested', 'addMarker')) {
    if ($commandBar -match $obsolete) {
        throw "The Phase 6 toolbar still contains obsolete UI: $obsolete"
    }
}

foreach ($group in @(
    'inspectorRecordingGroup', 'inspectorTempoGroup', 'inspectorPitchGroup',
    'inspectorPreservePitchGroup', 'inspectorExportGroup')) {
    if ($audioEditor -notmatch ('objectName:\s*"' + $group + '"')) {
        throw "The Phase 6 inspector is missing $group."
    }
}
foreach ($control in @(
    'inspectorRecordingDevice', 'inspectorInputMeter', 'inspectorMonitorSwitch',
    'inspectorRecordingFormat', 'inspectorBpmInput', 'inspectorDetectBpmButton',
    'inspectorSpeedSlider', 'inspectorSpeedValue', 'inspectorSpeedResetButton',
    'inspectorPitchMinus', 'inspectorPitchSlider', 'inspectorPitchPlus',
    'inspectorPitchValue', 'inspectorPreservePitchSwitch',
    'inspectorFormantRow', 'inspectorFormantSwitch', 'editorExportBrowseButton')) {
    if ($audioEditor -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The reference inspector is missing $control."
    }
}
if ($audioEditor -match 'active:\s*AudioEditorController\.formantPreservationSupported') {
    throw 'The reference Formant row must remain visible.'
}
foreach ($obsolete in @(
    'overviewNavigator', 'recordingInspector', 'timePitchInspector',
    'editorTransportBar', 'audioEditorGainDialog')) {
    if ($audioEditor -match ('objectName:\s*"' + $obsolete + '"')) {
        throw "The Phase 6 page still instantiates obsolete UI: $obsolete"
    }
}
foreach ($deletedQml in @(
    'OverviewNavigator.qml', 'RecordingInspectorSection.qml',
    'TimePitchInspectorSection.qml', 'EditorTransportBar.qml')) {
    if (Test-Path -LiteralPath (Join-Path $editorRoot $deletedQml)) {
        throw "Obsolete component still exists: $deletedQml"
    }
    if ($appCmake -match [regex]::Escape($deletedQml)) {
        throw "Obsolete component remains registered: $deletedQml"
    }
}

foreach ($mappingCall in @(
    'viewport\.frameAtPixel', 'viewport\.pixelAtFrame',
    'viewport\.zoomAt', 'viewport\.panByPixels')) {
    if ($waveformCanvas -notmatch $mappingCall) {
        throw "Waveform direct manipulation is missing shared mapping: $mappingCall"
    }
}
if ($audioEditor -notmatch 'frameAtPixel\(\s*index\s*\*\s*ruler\.width\s*/\s*8\)' -or
    $audioEditor -notmatch 'pixelAtFrame\(0\)' -or
    $audioEditor -notmatch 'onMoved:\s*AudioEditorController\.viewport\.panByPixels' -or
    $audioEditor -match 'visibleStartFrame\s*\+\s*AudioEditorController\.viewport\.visibleFrameCount') {
    throw 'Ruler and scrollbar must use the shared viewport mapper without QML frame arithmetic.'
}
if ($waveformCanvas -notmatch 'viewportChannelPeaks' -or
    $waveformCanvas -match 'visibleStartRatio|visibleEndRatio|renderMode' -or
    $waveformCanvas -match 'positionMs\s*\*\s*AudioEditorController\.sampleRate') {
    throw 'Waveform QML must consume visible peaks and exact playheadFrame without a second crop/time path.'
}
foreach ($shortcut in @('Ctrl\+1', 'Ctrl\+2', 'Ctrl\+B', 'Ctrl\+C', 'Ctrl\+X', 'Ctrl\+V')) {
    if ($audioEditor -notmatch $shortcut) {
        throw "The editor is missing the interaction shortcut: $shortcut"
    }
}
foreach ($responsiveHook in @('referenceLayout', 'narrowLayout',
    'editorInspectorScroller', 'editorInspectorAccess')) {
    if ($audioEditor -notmatch $responsiveHook) {
        throw "The responsive editor is missing $responsiveHook."
    }
}

foreach ($capability in @('recordingSupported', 'bpmDetectionSupported',
    'timePitchSupported', 'playbackSupported', 'exportSupported')) {
    if ($controllerHeader -notmatch ('Q_PROPERTY\(bool\s+' + $capability) -or
        $audioEditor -notmatch ('AudioEditorController\.' + $capability)) {
        throw "The Phase 6 future backend is missing an explicit capability gate: $capability"
    }
}
foreach ($field in @('editorExportCodec', 'editorExportSampleRate',
    'editorExportBitDepth', 'editorExportChannels', 'editorExportBitRate',
    'editorExportDirectory')) {
    if ($audioEditor -notmatch ('objectName:\s*"' + $field + '"')) {
        throw "The editable E group is missing $field."
    }
}
$shortcutPlay = [regex]::Escape((ConvertFrom-Utf8Base64 '56m65qC8ID0g5pKt5pS+IC8g5pqC5YGc'))
$shortcutFade = [regex]::Escape((ConvertFrom-Utf8Base64 '5ouW5ou95Y+z5LiK6KeSID0g6LCD5pW05reh5Ye6'))
$shortcutEnvelope = [regex]::Escape((ConvertFrom-Utf8Base64 '5Y+M5Ye76Z+z6YeP57q/ID0g5re75Yqg5o6n5Yi254K5'))
$recordingReady = [regex]::Escape((ConvertFrom-Utf8Base64 '5YeG5aSH5b2V6Z+z'))
if (($audioEditor + "`n" + $commandBar) -match 'Phase\s*[0-9]' -or
    $audioEditor -notmatch $shortcutPlay -or
    $audioEditor -notmatch $shortcutFade -or
    $audioEditor -notmatch $shortcutEnvelope -or
    $audioEditor -notmatch ('qsTr\("' + $recordingReady + '"\)')) {
    throw 'The editor still contains phased placeholder copy or is missing reference instructions.'
}
if ($audioEditor -notmatch 'objectName:\s*"recordingTimeText"[\s\S]{0,220}00:00:00' -or
    $audioEditor -notmatch 'objectName:\s*"editorStatusBar"[\s\S]{0,160}visible:\s*false') {
    throw 'Recording time or the visually absent reference status bar does not match the source image.'
}
foreach ($accessibleObject in @('audioToolsMinimizeButton',
    'audioToolsMaximizeButton', 'audioToolsCloseButton',
    'recordingMicrophoneButton', 'recordingToggleButton',
    'editorPrimaryPlayButton', 'editorPlayheadHandle',
    'editorSelectionStartHandle', 'editorSelectionEndHandle',
    'editorEventLeftTrimHandle', 'editorEventRightTrimHandle')) {
    $surface = $toolsWindow + "`n" + $audioEditor + "`n" + $waveformCanvas
    if ($surface -notmatch ('objectName:\s*"' + $accessibleObject + '"')) {
        throw "Accessible control is missing a stable object name: $accessibleObject"
    }
}
if (($toolsWindow + "`n" + $audioEditor + "`n" + $waveformCanvas) -notmatch 'Accessible\.role' -or
    ($toolsWindow + "`n" + $audioEditor + "`n" + $waveformCanvas) -notmatch 'Accessible\.name') {
    throw 'Icon-only editor/window controls must publish accessible names and roles.'
}

foreach ($translation in Get-ChildItem -LiteralPath (Join-Path $SourceRoot 'translations') -Filter 'agplayer_*.ts') {
    $translationText = Get-Content -Raw -Encoding UTF8 -LiteralPath $translation.FullName
    foreach ($obsoleteContext in @('EditorTransportBar', 'OverviewNavigator',
        'RecordingInspectorSection', 'TimePitchInspectorSection')) {
        if ($translationText -match ('<name>' + $obsoleteContext + '</name>')) {
            throw "Obsolete translation context remains in $($translation.Name): $obsoleteContext"
        }
    }
}

if ($metadataPage -notmatch 'objectName:\s*"metadataCancelButton"[\s\S]{0,180}visible:\s*true[\s\S]{0,120}enabled:\s*MetadataEditor\.busy') {
    throw 'Metadata cancel must remain visibly discoverable and only activate while a write is running.'
}
if ($filenamePage -notmatch 'objectName:\s*"filenameCancelButton"[\s\S]{0,220}visible:\s*true[\s\S]{0,120}enabled:\s*FilenameProcessor\.busy') {
    throw 'Rename cancel must remain visibly discoverable and only activate while a transaction is running.'
}
if ($miniControls -match 'Layout\.preferredWidth:\s*expanded\s*\?') {
    throw 'Mini-player controls must not reference an undefined expanded property.'
}
if ($formatPage -notmatch 'objectName:\s*"formatSettingsPanel"[\s\S]{0,320}Layout\.preferredWidth:\s*settingsPanel\.expanded[\s\S]{0,80}\?\s*\(page\.compactLayout\s*\?\s*360\s*:\s*445\)\s*:\s*40') {
    throw 'The format converter needs a reference-width settings workbench.'
}
if ($metadataPage -notmatch 'desktopMinimumWidth:\s*1206' -or
    $metadataPage -notmatch 'inspectorRatio:\s*0\.44' -or
    $metadataPage -notmatch 'Layout\.preferredWidth:\s*page\.compactLayout[\s\S]{0,220}page\.width \* page\.inspectorRatio - 12') {
    throw 'The metadata editor needs a complete batch-edit workbench at desktop width.'
}
if ($filenamePage -notmatch 'id:\s*numberPositionBox' -or
    $filenamePage -notmatch 'id:\s*preserveExtensionCheck') {
    throw 'The filename rule panel lost extension or numbering controls.'
}
foreach ($control in @(
    'formatToolbar', 'formatStatusFilters', 'formatTaskPanel',
    'formatSettingsPanel', 'formatBottomBar', 'formatOutputFormatGroup',
    'formatEncodingSettingsGroup', 'formatOutputOptionsGroup',
    'formatTotalProgress')) {
    if ($formatSurface -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The conversion workbench is missing $control."
    }
}
if ($formatPage -notmatch 'converter\.(previewSelected|buildPreflight)\(' -or
    $formatPage -notmatch '(formatPreflightDialog|FormatPreflightDialog)') {
    throw 'The converter lost its real preflight path.'
}
foreach ($toolPage in @($formatPage, $metadataPage, $filenamePage)) {
    if ($toolPage -notmatch 'function addCurrentPlayerTrack\(\)' -or
        $toolPage -notmatch 'PlaybackController\.queueTrackIds' -or
        $toolPage -notmatch 'Qt\.resolvedUrl\("file:///"') {
        throw 'An unaffected file tool lost its active-player queue import path.'
    }
}
if ($metadataPage -notmatch 'enabled:\s*!MetadataEditor\.busy\s*&&\s*PlaybackController\.currentTrackId\.length > 0') {
    throw 'Metadata player import availability regressed.'
}
if ($filenamePage -notmatch 'enabled:\s*!FilenameProcessor\.busy\s*&&\s*PlaybackController\.currentTrackId\.length > 0') {
    throw 'Filename player import availability regressed.'
}
if ($miniControls -match 'Layout\.preferredWidth:\s*expanded\s*\?') {
    throw 'Mini-player controls reference an undefined expanded property.'
}
