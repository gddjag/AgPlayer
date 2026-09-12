param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$page = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/FormatConvertPage.qml')
$table = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/FormatTaskTable.qml')
$settings = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/FormatSettingsPanel.qml')
$fixture = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'tests/qml/FormatConverterVisualFixture.qml')
$combined = $page + "`n" + $table + "`n" + $settings

foreach ($control in @(
    'formatToolbar', 'formatSelectAllCheck',
    'formatTaskPanel', 'formatSettingsPanel', 'formatBottomBar',
    'formatEncoderBox', 'formatOutputDirectoryRow', 'formatSummaryCard',
    'formatLocalProcessingHint', 'formatSettingsAdvancedToggle',
    'formatTaskContextMenu')) {
    if ($combined -notmatch [regex]::Escape($control)) {
        throw "The reference format-conversion workbench is missing $control."
    }
}

if ($page -match 'formatFilterButton' -or $page -match 'filter-3-line') {
    throw 'The format toolbar must not restore the removed search/filter control.'
}
if ($page -notmatch '\*\.aif\s+\*\.aiff') {
    throw 'The format converter picker must accept AIFF input files.'
}
if ($settings -notmatch 'objectName:\s*"formatBitDepthBox"' -or
    $settings -notmatch 'property\s+string\s+bitDepth') {
    throw 'Lossless output settings must expose a friendly bit-depth selector.'
}

if ($combined -notmatch 'key:\s*"Converting"') {
    throw 'The reference task filters must expose the converting state.'
}
if ($combined -notmatch 'model\.status\s*===\s*"Converting"') {
    throw 'The converting filter must be backed by actual task state.'
}
if ($combined -notmatch 'modelData\.available') {
    throw 'Output format buttons must still bind availability to the backend catalog.'
}
if ($page -notmatch 'SettingsController\.defaultOutputDirectory') {
    throw 'The output directory must initialize from and persist through SettingsController.'
}
if ($settings -notmatch 'objectName:\s*"formatSettingsScroll"[\s\S]*?ScrollBar\.vertical\.policy:\s*ScrollBar\.AsNeeded' -or
    $settings -notmatch 'objectName:\s*"formatAdvancedSettings"[\s\S]*?Layout\.topMargin:\s*Theme\.spacingLg') {
    throw 'Advanced concurrency controls must remain reachable in the independently scrollable settings panel without artificial fold spacing.'
}
if ($settings -notmatch 'objectName:\s*"formatLocalProcessingHint"[\s\S]*?Layout\.topMargin:\s*Theme\.spacingMd') {
    throw 'The local-processing hint must use the compact design-system spacing.'
}
if ($settings -notmatch 'objectName:\s*"formatOutputFormatGrid"[\s\S]*?Layout\.preferredWidth:\s*384' -or
    $settings -notmatch 'objectName:\s*"formatOutputFormatButton-"\s*\+\s*modelData\.key' -or
    $settings -notmatch 'Layout\.minimumWidth:\s*0[\s\S]*?Layout\.preferredWidth:\s*90[\s\S]*?Layout\.maximumWidth:\s*90' -or
    $settings -notmatch 'columnSpacing:\s*8') {
    throw 'The output format grid must retain the 384px four-column reference geometry.'
}
if ($settings -notmatch 'id:\s*encodingGroup[\s\S]*?columnSpacing:\s*11' -or
    $settings -notmatch 'id:\s*outputOptions[\s\S]*?columnSpacing:\s*11' -or
    ([regex]::Matches($settings, 'Layout\.preferredWidth:\s*122')).Count -lt 8) {
    throw 'Encoding and output-option labels must share the 122px reference column.'
}
if ($settings -notmatch 'component\s+ReferenceComboBox\s*:\s*ComboBox' -or
    $settings -notmatch 'Theme\.icon\("arrow-down-s-line"\)' -or
    $settings -match 'SpinBox') {
    throw 'Format setting combo boxes must use one official down-chevron rather than spin indicators.'
}
if ($page -notmatch 'objectName:\s*"cancelAllButton"[\s\S]{0,1000}?Theme\.icon\("checkbox-blank-fill"\)' -or
    $page -match 'objectName:\s*"cancelAllButton"[\s\S]{0,1000}?Theme\.icon\("close-fill"\)') {
    throw 'Cancel all must use the filled stop-square icon instead of a close icon.'
}
if ($page -notmatch 'SettingsController\.parallelJobs') {
    throw 'The progress-row parallel control must synchronize through SettingsController.'
}
if ($page -notmatch 'plan\.error\s*\|\|\s*plan\.reason') {
    throw 'Every preflight rejection must surface the backend reason.'
}
if ($page -notmatch 'requestedQuality' -or
    $page -notmatch 'bitDepth:\s*settingsPanel\.bitDepth') {
    throw 'Preflight requests must carry the selected quality/compression level and bit depth.'
}
if ($page -notmatch '\*\.aif\s+\*\.aiff' -or $page -notmatch '\*\.m4a') {
    throw 'The import picker must accept AIFF and keep accepting M4A source files.'
}
if ($settings -notmatch 'objectName:\s*"formatChannelBox"') {
    throw 'Channel selection must remain available in the parameter matrix.'
}
if ($settings -notmatch 'property\s+string\s+sampleFormat:\s*bitDepthBox\.visible' -or
    $settings -notmatch 'property\s+string\s+bitrateMode:\s*converter') {
    throw 'Bit-depth keys must not leak into sampleFormat, and bitrate mode must be empty when unused.'
}
if ($fixture -notmatch 'key:\s*"aiff",\s*label:\s*"AIFF"' -or
    $fixture -match 'key:\s*"m4a",\s*label:\s*"M4A"') {
    throw 'The eight-format output catalog must expose AIFF instead of M4A.'
}
if ($combined -notmatch 'taskId' -or
    $combined -notmatch 'cancelTask\(' -or
    $combined -notmatch 'copyText\(' -or
    $combined -notmatch 'removeFile\(' -or
    $combined -notmatch 'retryFailed\(') {
    throw 'The task context menu must reuse real cancel, copy, and remove APIs.'
}
if ($table -notmatch 'columnWidths:\s*\[\s*62,\s*260,\s*120,\s*100,\s*115,\s*130,\s*120,\s*120,\s*190\s*\]') {
    throw 'The reference-width task table must fit its progress percentage before the settings boundary.'
}
if ($table -notmatch 'objectName:[^\n]*"formatHeaderCell-"\s*\+\s*index' -or
    $table -notmatch 'anchors\.leftMargin:\s*index\s*===\s*1\s*\?\s*30\s*:\s*10') {
    throw 'The filename header must align with the reference column without moving task rows.'
}
if ($page -notmatch 'ColumnLayout\s*\{\s*anchors\.fill:\s*parent\s*spacing:\s*Theme\.spacingXs' -or
    $page -notmatch 'RowLayout\s*\{\s*Layout\.fillWidth:\s*true\s*Layout\.fillHeight:\s*true\s*spacing:\s*Theme\.spacingSm') {
    throw 'The main row must move up while preserving the task/settings horizontal gap.'
}
if ($table -notmatch 'formatTaskFirstFileIconBadge' -or
    $table -notmatch 'formatTaskFirstFileIcon' -or
    $table -notmatch 'Theme\.icon\("file-music-fill"\)') {
    throw 'Task file icons must use the official music asset inside the colored badge component.'
}
if ($table -notmatch 'component\s+ReferenceCheckBox\s*:\s*CheckBox' -or
    $settings -notmatch 'component\s+ReferenceCheckBox\s*:\s*CheckBox' -or
    $combined -notmatch 'Theme\.icon\("check-line"\)' -or
    $combined -notmatch 'width:\s*20' -or
    $combined -notmatch 'radius:\s*3' -or
    $combined -notmatch 'color:\s*control\.checked\s*\?\s*Theme\.accent') {
    throw 'Task and settings checkboxes must use the reference blue indicator with the official check asset.'
}
if (-not (Test-Path -LiteralPath (Join-Path $SourceRoot 'assets/icons/check-line.svg'))) {
    throw 'Missing approved check-line icon asset for the reference checkbox indicator.'
}
if ($page -notmatch 'objectName:\s*"formatTotalProgress"[\s\S]{0,3000}?objectName:\s*"converterParallelJobsGroup"[\s\S]{0,800}?objectName:\s*"converterParallelJobsBox"[\s\S]{0,1000}?objectName:\s*"formatSummaryCard"' -or
    $page -match 'formatOutputDirectoryRow') {
    throw 'The reference footer must place parallel jobs in the right-side action area before the summary without duplicating output-directory controls.'
}
foreach ($asset in @(
    'arrow-up-s-line.svg', 'checkbox-circle-line.svg',
    'arrow-down-s-line.svg', 'checkbox-blank-fill.svg', 'error-warning-line.svg',
    'file-music-fill.svg')) {
    if (-not (Test-Path -LiteralPath (Join-Path $SourceRoot "assets/icons/$asset"))) {
        throw "Missing approved format-workbench icon asset: $asset"
    }
}

$cmake = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/CMakeLists.txt')
foreach ($asset in @('checkbox-circle-line.svg', 'error-warning-line.svg')) {
    if ($cmake -notmatch [regex]::Escape("assets/icons/$asset")) {
        throw "The footer icon $asset is not registered as an application resource."
    }
}
if ($cmake -notmatch [regex]::Escape('assets/icons/check-line.svg')) {
    throw 'The checkbox check-line icon is not registered as an application resource.'
}
foreach ($asset in @('arrow-up-s-line.svg', 'arrow-down-s-line.svg',
    'checkbox-blank-fill.svg')) {
    if ($cmake -notmatch [regex]::Escape("assets/icons/$asset")) {
        throw "The format icon $asset is not registered as an application resource."
    }
}
if ($page -notmatch 'Theme\.icon\("checkbox-circle-line"\)' -or
    $page -notmatch 'Theme\.icon\("error-warning-line"\)') {
    throw 'The footer summary must render the approved check-circle and warning icons.'
}

$iconNotice = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'assets/licenses/AgPlayer-Icons-License.txt')
$remixLicense = Join-Path $SourceRoot 'assets/licenses/RemixIcon-Apache-2.0.txt'
if ($iconNotice -notmatch 'Remix Icon v4\.6\.0' -or
    $iconNotice -notmatch 'arrow-up-s-line\.svg' -or
    $iconNotice -notmatch 'check-line\.svg' -or
    $iconNotice -notmatch 'arrow-down-s-line\.svg' -or
    $iconNotice -notmatch 'checkbox-blank-fill\.svg' -or
    $iconNotice -match 'does not redistribute any Remix') {
    throw 'The icon notice must accurately list the redistributed Remix Icon v4.6.0 assets.'
}
if (-not (Test-Path -LiteralPath $remixLicense) -or
    (Get-Content -Raw -LiteralPath $remixLicense) -notmatch 'Apache License\s+Version 2\.0') {
    throw 'The Remix Icon v4.6.0 Apache-2.0 license copy is missing or invalid.'
}
