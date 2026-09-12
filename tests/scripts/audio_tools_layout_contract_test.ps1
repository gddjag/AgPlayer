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
$equalizerWindow = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/EqualizerWindow.qml')
$appCmake = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $SourceRoot 'app/CMakeLists.txt')
$appMain = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $SourceRoot 'app/main.cpp')
$testsCmake = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $SourceRoot 'tests/CMakeLists.txt')
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
$separationPage = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $toolsRoot 'VocalSeparationPage.qml')
$settingsPage = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/SettingsPage.qml')

$forbidden = @('RecordingSession', 'Finalizing', 'recordingSupported', 'startRecording',
               'editor.newRecording', 'editorRecordingTransport',
               'inspectorRecordingGroup')
$productionRoots = @(
    (Join-Path $SourceRoot 'core/src'),
    (Join-Path $SourceRoot 'qt/src'),
    (Join-Path $SourceRoot 'app/qml'))
$productionFiles = foreach ($root in $productionRoots) {
    Get-ChildItem -LiteralPath $root -File -Recurse
}
$productionFiles += @(
    (Join-Path $SourceRoot 'CMakeLists.txt'),
    (Join-Path $SourceRoot 'core/CMakeLists.txt')) | Where-Object {
    Test-Path -LiteralPath $_
}
foreach ($symbol in $forbidden) {
    $matches = $productionFiles | Select-String -SimpleMatch -Pattern $symbol
    if ($matches) {
        throw "Recording production symbol remains: $symbol ($($matches[0].Path):$($matches[0].LineNumber))"
    }
}

$recordingTranslationSources = @(
    '6K+36YCJ5oup5b2V6Z+z5L+d5a2Y5L2N572u',
    '5peg5rOV5ZCv5Yqo5b2V6Z+z6K6+5aSH77yM6K+35qOA5p+l6K6+5aSH5LiO5p2D6ZmQ',
    '5b2V6Z+z5a6M5oiQ77yM5L2G5peg5rOV5o+S5YWl5b2T5YmN5paH5qGj',
    '5b2V6Z+z5bey5Y+W5raI', '5b2V6Z+z',
    '6YCJ5oup5b2V6Z+z6K6+5aSH77yIQWx0K1LvvIk=',
    '5pqC5YGcIC8g57un57ut5b2V6Z+z77yIU2hpZnQrUu+8iQ==',
    '5byA5aeLIC8g57un57ut5b2V6Z+z77yIUu+8iQ==',
    '5YGc5q2i5bm25L+d5a2Y5b2V6Z+z77yIQ3RybCtS77yJ',
    '56m65qC8ID0g5pKt5pS+IC8g5pqC5YGcICAgICAgIFIgPSDlvIDlp4vlvZXpn7MgICAgICAgU2hpZnQrUiA9IOaaguWBnCAvIOe7p+e7reW9lemfsyAgICAgICBDdHJsK1IgPSDlgZzmraLlubbkv53lrZggICAgICAgUyA9IOWcqOaSreaUvuWktOWkhOWIhuWJsiAgICAgICBEZWxldGUgPSDliKDpmaTniYfmrrUgICAgICAgQ3RybCtDIC8gWCAvIFYgPSDlpI3liLYgLyDliarliIcgLyDnspjotLQgICAgICAgQ3RybCtaIC8gWSA9IOaSpOmUgCAvIOmHjeWBmg==',
    '5pyq5qOA5rWL5Yiw6L6T5YWl6K6+5aSH', '5Y+W5raI5b2V6Z+z',
    '5b2V6Z+z5o6n5Yi2', '5byA5aeL5b2V6Z+z',
    '5pqC5YGc5oiW57un57ut5b2V6Z+z', '5b2V6Z+z5bey5pqC5YGc',
    '5q2j5Zyo5b2V6Z+z', '5YeG5aSH5b2V6Z+z', 'QS4g5b2V6Z+z',
    '6L6T5YWl6K6+5aSH', '6L6T5YWl55S15bmz', '55uR5ZCs',
    '5b2V6Z+z5qC85byP', 'UGhhc2UgOSDliY3kuI3lj6/nlKg=',
    '57un57ut5b2V6Z+z', '6YCJ5oup5b2V6Z+z6K6+5aSH',
    '5pqC5YGc5b2V6Z+z', '5YGc5q2i5bm25L+d5a2Y5b2V6Z+z',
    '5bGV5byA5b2V6Z+z6K6+572u', '5oqY5Y+g5b2V6Z+z6K6+572u',
    '5YGc5q2i5b2V6Z+z', '5Yi35paw6L6T5YWl6K6+5aSH',
    '5paw5bu65b2V6Z+z', '5omT5byA6Z+z6aKR5oiW5paw5bu65b2V6Z+z5Lul5byA5aeL57yW6L6R',
    '5pKt5pS+77yaUGhhc2UgMTIg5o6l5YWlIMK3IFLvvJpQaGFzZSA5IOaOpeWFpSDCtyBDdHJsK1NoaWZ0K0Eg5Y+W5raI6YCJ5Yy6IMK3IEN0cmwrVyDmuIXnqbo=') |
    ForEach-Object { ConvertFrom-Utf8Base64 $_ }
$translationFiles = @('agplayer_zh.ts', 'agplayer_en.ts') | ForEach-Object {
    Join-Path $SourceRoot "translations/$_"
}
foreach ($source in $recordingTranslationSources) {
    $matches = Select-String -Path $translationFiles -SimpleMatch -Pattern "<source>$source</source>"
    if ($matches) {
        throw "Recording translation remains: $source ($($matches[0].Path):$($matches[0].LineNumber))"
    }
}

if ($appMain -notmatch 'audioEditor\.setPlaybackController\(&playback\)') {
    throw 'The production audio editor is not wired to the shared playback controller.'
}
if ($appMain -match '\[&audioEditor,[^\]]*\bpollFunc\b') {
    throw 'The editor waveform QA readiness callback must not strongly capture its own shared function.'
}
if ($appMain -notmatch 'waveformReadyTimer\s*=\s*new QTimer\(&app\)' -or
    $appMain -notmatch 'QObject::connect\(waveformReadyTimer,[\s\S]{0,160}&app') {
    throw 'The editor waveform QA readiness poll must use an application-owned timer and QObject context.'
}
if ($testsCmake -match 'qml_audio_editor_test[\s\S]{0,500}junitxml') {
    throw 'qml_audio_editor_test must not write a fixed per-test JUnit file.'
}
if ($testsCmake -notmatch 'ctest --output-junit <run-unique-path>') {
    throw 'The audio editor CTest registration must document run-unique JUnit output.'
}
if ($controllerHeader -notmatch 'Q_INVOKABLE\s+bool\s+relinkProjectSource\(const QString&amp;|Q_INVOKABLE\s+bool\s+relinkProjectSource\(const QString&') {
    throw 'Relink must expose a decimal string Source ID to QML.'
}
foreach ($control in @('editorOfflineSourceBanner', 'editorRelinkSourceButton')) {
    if ($audioEditor -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The offline project recovery flow is missing $control."
    }
}
if ($appMain -notmatch 'ensureAudioToolsWindow' -or
    $appMain -notmatch 'audioToolsVisibleChanged') {
    throw 'The audio tools window must be created on first use, not during application startup.'
}
$ensureToolsPosition = $appMain.IndexOf('ensureAudioToolsWindow')
$loadToolsPosition = $appMain.IndexOf(
    'audioToolsComponent.loadFromModule("AgPlayer", "AudioToolsWindow")')
if ($loadToolsPosition -lt $ensureToolsPosition) {
    throw 'AudioToolsWindow is still loaded before the first-use factory.'
}
foreach ($toolIndex in @(0, 4, 1, 2, 3)) {
    if ($toolsWindow -notmatch (
            'active:\s*AudioToolsController\.currentTool\s*===\s*' + $toolIndex)) {
        throw "Audio tool page $toolIndex must be instantiated only while selected."
    }
}
if ($toolsWindow -notmatch 'onVisibleChanged:[\s\S]{0,220}AudioEditorController\.activate\(\)' -or
    $toolsWindow -notmatch 'onVisibleChanged:[\s\S]{0,300}AudioEditorController\.deactivate\(\)') {
    throw 'Showing or hiding the lazy tools window must activate or release editor resources.'
}

foreach ($control in @(
    'editorMainColumn', 'editorInspector', 'editorCommandBar', 'fileSummaryBar',
    'editorTimelineWorkspace', 'editorTimeRuler',
    'editorWaveformCanvas', 'editorPlaybackTransport',
    'editorShortcutCard', 'editorStatusBar')) {
    if ($audioEditor -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The Phase 6 audio editor is missing $control."
    }
}
if ($audioEditor -match 'objectName:\s*"editorTrackHeader"') {
    throw 'The audio-editor timeline must not restore the removed track-header rail.'
}

if ($toolsWindow -notmatch 'width:\s*1672' -or
    $toolsWindow -notmatch 'height:\s*941' -or
    $toolsWindow -notmatch 'objectName:\s*"audioToolsTitleBar"[\s\S]{0,180}Layout\.preferredHeight:\s*Theme\.titleBarHeight' -or
    $toolsWindow -notmatch 'ToolSidebar\s*\{' -or
    $toolsWindow -notmatch 'title:\s*qsTr\("AgPlayer') {
    throw 'The tools shell must preserve 1672x941 geometry with fixed shared chrome.'
}
if ($toolsNavigation -notmatch 'implicitHeight:\s*Theme\.settingsRowHeight' -or
    $toolsNavigation -match 'referenceWorkbench\s*\?\s*52|separationWorkbench\s*\?\s*44') {
    throw 'All five tool pages must use one fixed navigation height.'
}
foreach ($button in @('audioToolsMinimizeButton', 'audioToolsMaximizeButton',
                      'audioToolsCloseButton')) {
    $pattern = 'objectName:\s*"' + $button +
        '"[\s\S]{0,180}Layout\.preferredWidth:\s*Theme\.navigationActionExtent' +
        '[\s\S]{0,100}Layout\.preferredHeight:\s*Theme\.navigationActionExtent' +
        '[\s\S]{0,100}iconSize:\s*14'
    if ($toolsWindow -notmatch $pattern) {
        throw "Audio-tools window button does not use the shared action extent: $button"
    }
}
foreach ($button in @('equalizerMinimizeButton', 'equalizerMaximizeButton',
                      'equalizerCloseButton')) {
    $pattern = 'objectName:\s*"' + $button +
        '"[\s\S]{0,100}width:\s*Theme\.navigationActionExtent' +
        '[\s\S]{0,80}height:\s*Theme\.navigationActionExtent'
    if ($equalizerWindow -notmatch $pattern) {
        throw "Equalizer window button geometry differs: $button"
    }
}
if ($toolsWindow -notmatch 'objectName:\s*"audioToolsContentStack"') {
    throw 'The tools content stack must expose the Phase 6 acceptance object name.'
}
if ($toolsWindow -notmatch 'objectName:\s*"audioToolsLogo"' -or
    $toolsWindow -match 'color:\s*"#0867ed"') {
    throw 'The latest reference uses the transparent waveform brand mark.'
}
$audioToolsTitle = [regex]::Escape((ConvertFrom-Utf8Base64 `
    'QWdQbGF5ZXIgwrcg6Z+z6aKR5bel5YW3'))
if ($toolsWindow -notmatch ('title:\s*qsTr\("' + $audioToolsTitle + '"\)') -or
    $toolsWindow -notmatch ('text:\s*qsTr\("' + $audioToolsTitle + '"\)')) {
    throw 'The native and custom title bars must use AgPlayer · 音频工具.'
}
if ($toolsWindow -notmatch 'objectName:\s*"audioToolsSpaceShortcut"' -or
    $toolsWindow -notmatch 'sequence:\s*"Space"' -or
    $toolsWindow -notmatch 'context:\s*Qt\.WindowShortcut' -or
    $audioEditor -match 'objectName:\s*"editorSpaceShortcut"') {
    throw 'The tools shell must own the only global Space playback shortcut.'
}
if ($toolsWindow -notmatch 'window\.startSystemMove\(\)' -or
    $toolsWindow -match 'window\.(x|y)\s*\+=') {
    throw 'The audio tools title bar must use native movement without manual coordinates.'
}
if ($toolsWindow -match 'Layout\.(left|right|bottom)Margin:\s*[1-9]') {
    throw 'The tools content stack must occupy the complete 0,119,1672,822 area.'
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
if ($toolsNavigation -notmatch 'visibleToolOrder:\s*\[0,\s*4,\s*1,\s*2,\s*3,\s*5\]' -or
    $toolsNavigation -notmatch 'objectName:\s*"audioToolNav_"\s*\+\s*modelData\.toolId' -or
    $toolsNavigation -notmatch 'RowLayout' -or
    $toolsNavigation -notmatch 'selected:\s*navigation\.currentTool' -or
    $toolsNavigation -match '#[0-9A-Fa-f]{6}') {
    throw 'The six audio tools must retain stable IDs and use shared theme tokens.'
}
$repeaterPosition = $toolsNavigation.IndexOf('Repeater {')
$firstFillSpacerPosition = $toolsNavigation.IndexOf('Item { Layout.fillWidth:')
if ($firstFillSpacerPosition -ge 0 -and $firstFillSpacerPosition -lt $repeaterPosition) {
    throw 'The shared tool navigation must not use a leading fill spacer.'
}
$trailingFillSpacers = [regex]::Matches(
    $toolsNavigation, '(?m)^        Item \{ Layout\.fillWidth: true \}\r?$')
if ($trailingFillSpacers.Count -ne 1 -or
    $trailingFillSpacers[0].Index -lt $repeaterPosition) {
    throw 'The shared tool navigation must have exactly one trailing fill spacer.'
}
$navOrder = @(
    (ConvertFrom-Utf8Base64 '6Z+z6aKR57yW6L6R'),
    (ConvertFrom-Utf8Base64 '5Lq65aOw5Ly05aWP5YiG56a7'),
    (ConvertFrom-Utf8Base64 '5qC85byP6L2s5o2i'),
    (ConvertFrom-Utf8Base64 '5YWD5pWw5o2u57yW6L6R'),
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
if ($commandBar -notmatch 'clearTimeline\(\)' -or
    $commandBar -match 'clearDocument\(') {
    throw 'Clear must remove all timeline events without closing the document.'
}
foreach ($obsolete in @(
    'insertSilence', 'clearDocument', 'exportMenu',
    'gainRequested', 'addMarker')) {
    if ($commandBar -match $obsolete) {
        throw "The Phase 6 toolbar still contains obsolete UI: $obsolete"
    }
}

foreach ($group in @(
    'inspectorTempoGroup', 'inspectorPitchGroup',
    'inspectorPreservePitchGroup', 'inspectorExportGroup')) {
    if ($audioEditor -notmatch ('objectName:\s*"' + $group + '"')) {
        throw "The Phase 6 inspector is missing $group."
    }
}
foreach ($control in @(
    'inspectorBpmInput', 'inspectorDetectBpmButton',
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
    $audioEditor -notmatch 'visibleEndFrame' -or
    $audioEditor -match 'visibleStartFrame\s*\+\s*AudioEditorController\.viewport\.visibleFrameCount') {
    throw 'Ruler and timeline interactions must use the shared viewport mapper without duplicate frame arithmetic.'
}
foreach ($removedControl in @('editorTrackGain', 'editorTrackGainLabel')) {
    if ($audioEditor -match ('objectName:\s*"' + $removedControl + '"')) {
        throw "Removed editor control remains: $removedControl"
    }
}
if ($audioEditor -notmatch 'ThemedRangeSlider\s*\{[\s\S]{0,120}objectName:\s*"editorTimelineZoomRange"' -or
    $audioEditor -notmatch 'Qt\.rgba\(Theme\.borderStrong\.r,[\s\S]{0,120}0\.30\)' -or
    $audioEditor -notmatch 'objectName:\s*"editorTimelineZoomStartHandle"[\s\S]{0,100}radius:\s*0' -or
    $audioEditor -notmatch 'objectName:\s*"editorTimelineZoomEndHandle"[\s\S]{0,100}radius:\s*0') {
    throw 'The restored editor zoom strip must use the themed range control, square handles, and 30% track.'
}
if ($waveformCanvas -notmatch 'viewportChannelPeaks' -or
    $waveformCanvas -match 'visibleStartRatio|visibleEndRatio|renderMode' -or
    $waveformCanvas -match 'positionMs\s*\*\s*AudioEditorController\.sampleRate') {
    throw 'Waveform QML must consume visible peaks and exact playheadFrame without a second crop/time path.'
}
if ($waveformCanvas -notmatch 'onReleased:\s*AudioEditorController\.releaseSelectionHandoff\(\)' -or
    $waveformCanvas -notmatch 'onCanceled:\s*AudioEditorController\.cancelSelectionHandoff\(\)') {
    throw 'Selection handoff release must preserve a triggered native drag while cancellation aborts it.'
}
if ($waveformCanvas -notmatch 'SettingsController\.waveformDensity' -or
    $waveformCanvas -notmatch 'SettingsController\.waveformThickness' -or
    $waveformCanvas -notmatch 'SettingsController\.waveformSolidBaseColor' -or
    $waveformCanvas -notmatch 'SettingsController\.spectrumSolidColor' -or
    $waveformCanvas -notmatch 'waveformMode\s*===\s*2\s*\?\s*1\.0' -or
    $waveformCanvas -notmatch 'waveformMode\s*===\s*2\s*\?\s*3\.0' -or
    $waveformCanvas -match 'waveformColor:\s*"#2587ff"') {
    throw 'Editor and player waveforms must use the same configurable style inputs.'
}
foreach ($shortcut in @('Ctrl\+1', 'Ctrl\+2', 'Ctrl\+B', 'Ctrl\+C', 'Ctrl\+X', 'Ctrl\+V')) {
    if ($audioEditor -notmatch $shortcut) {
        throw "The editor is missing the interaction shortcut: $shortcut"
    }
}
foreach ($responsiveHook in @('inspectorWidth', 'mainWidth',
    'timelineWorkspaceTop', 'trackRegionTop', 'trackRegionHeight',
    'narrowLayout', 'editorInspectorScroller',
    'editorInspectorAccess')) {
    if ($audioEditor -notmatch $responsiveHook) {
        throw "The responsive editor is missing $responsiveHook."
    }
}

foreach ($capability in @('bpmDetectionSupported',
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
        throw "The D. export settings group is missing $field."
    }
}
$shortcutPlay = [regex]::Escape((ConvertFrom-Utf8Base64 '56m65qC8ID0g5pKt5pS+IC8g5pqC5YGc'))
$shortcutEnvelope = [regex]::Escape((ConvertFrom-Utf8Base64 '5Y+M5Ye76Z+z6YeP57q/ID0g5re75Yqg5o6n5Yi254K5'))
if (($audioEditor + "`n" + $commandBar) -match 'Phase\s*[0-9]' -or
    $audioEditor -notmatch $shortcutPlay -or
    $audioEditor -notmatch $shortcutEnvelope) {
    throw 'The editor still contains phased placeholder copy or is missing reference instructions.'
}
$statusOverlay = 'objectName:\s*"editorStatusBar"[\s\S]{0,240}' +
    'visible:\s*AudioEditorController\.busy\s*\|\|\s*' +
    'AudioEditorController\.errorMessage\.length\s*>\s*0\s*\|\|\s*' +
    'statusSuccessTimer\.running'
if ($audioEditor -notmatch $statusOverlay -or
    $audioEditor -notmatch 'objectName:\s*"editorStatusBar"[\s\S]{0,360}y:\s*mainSurface\.height\s*-\s*height' -or
    $audioEditor -notmatch 'objectName:\s*"editorStatusBar"[\s\S]{0,420}width:\s*mainSurface\.width' -or
    $audioEditor -notmatch 'objectName:\s*"editorStatusBar"[\s\S]{0,460}height:\s*visible\s*\?\s*25\s*:\s*0') {
    throw 'Status feedback must be a 25 px bottom overlay shown only for processing, errors, or export success.'
}
foreach ($accessibleObject in @('audioToolsMinimizeButton',
    'audioToolsMaximizeButton', 'audioToolsCloseButton',
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

# Keep the three unaffected tools and shared controls under their existing
# production contracts while replacing only the obsolete editor assertions.
$formatPage = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'FormatConvertPage.qml')
$formatSettings = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'FormatSettingsPanel.qml')
$formatTable = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'FormatTaskTable.qml')
$formatSurface = $formatPage + "`n" + $formatSettings + "`n" + $formatTable
$metadataPage = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'MetadataEditPage.qml')
$filenamePage = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $toolsRoot 'FilenameProcessPage.qml')
$miniControls = Get-Content -Raw -Encoding UTF8 -LiteralPath (
    Join-Path $SourceRoot 'app/qml/AgPlayer/components/MiniPlayerControls.qml')

foreach ($control in @(
    'filenameFilePanel', 'filenameRulesPanel', 'filenamePreviewPanel',
    'filenameValidationPanel', 'filenameBottomBar')) {
    if ($filenamePage -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The filename reference workbench is missing $control."
    }
}
if ($filenamePage -notmatch 'id:\s*numberPositionBox' -or
    $filenamePage -notmatch 'id:\s*preserveExtensionCheck') {
    throw 'The filename rule panel lost extension or numbering controls.'
}
if ($filenamePage -match 'objectName:\s*"filenameValidationPanel"[\s\S]{0,180}Layout\.preferredWidth:\s*190') {
    throw 'The filename validation panel must not squeeze the preview table.'
}
foreach ($control in @(
    'vocalSeparationPage', 'separationInputPanel', 'separationInputWaveform',
    'separationModelDeck', 'separationSettingsPanel', 'separationStemSelector',
    'separationTimeline', 'separationHistoryPanel', 'separationBottomBar',
    'separationPrimaryAction', 'separationErrorPanel')) {
    if ($separationPage -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "The separation workbench is missing $control."
    }
}
if ($separationPage -match 'Emoji|Segoe UI Symbol' -or
    $separationPage -notmatch 'VocalSeparationController\.inputInfo' -or
    $separationPage -notmatch 'VocalSeparationController\.history' -or
    $separationPage -notmatch 'VocalSeparationController\.canStart' -or
    $separationPage -notmatch 'VocalSeparationController\.ModelFailed' -or
    $separationPage -notmatch 'VocalSeparationController\.JobFailed' -or
    $separationPage -notmatch 'downloadProgress') {
    throw 'The separation workbench must bind real controller data and shipped icons.'
}
if ($settingsPage -notmatch 'designRole:\s*"settingsCategoryRail"' -or
    $settingsPage -notmatch 'designRole:\s*"settingsContentSurface"' -or
    $settingsPage -match 'Segoe UI Symbol') {
    throw 'Settings must retain the shared category/content layout.'
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
