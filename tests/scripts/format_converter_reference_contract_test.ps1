param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$page = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/FormatConvertPage.qml')
$table = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/FormatTaskTable.qml')
$settings = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/FormatSettingsPanel.qml')
$combined = $page + "`n" + $table + "`n" + $settings

foreach ($control in @(
    'formatToolbar', 'formatFilterButton', 'formatSelectAllCheck',
    'formatTaskPanel', 'formatSettingsPanel', 'formatBottomBar',
    'formatEncoderBox', 'formatOutputDirectoryRow', 'formatSummaryCard',
    'formatLocalProcessingHint', 'formatSettingsAdvancedToggle',
    'formatTaskContextMenu')) {
    if ($combined -notmatch [regex]::Escape($control)) {
        throw "The reference format-conversion workbench is missing $control."
    }
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
if ($settings -notmatch 'objectName:\s*"formatAdvancedSettings"[\s\S]*?Layout\.topMargin:\s*(?:2[4-9]|[3-9]\d)') {
    throw 'Advanced concurrency controls must begin below the default reference fold.'
}
if ($page -notmatch 'SettingsController\.parallelJobs' -or
    $settings -notmatch 'SettingsController\.parallelJobs') {
    throw 'Parallel jobs must synchronize through SettingsController.'
}
if ($page -notmatch 'plan\.error\s*\|\|\s*plan\.reason') {
    throw 'Every preflight rejection must surface the backend reason.'
}
if ($combined -notmatch 'taskId' -or
    $combined -notmatch 'cancelTask\(' -or
    $combined -notmatch 'copyText\(' -or
    $combined -notmatch 'removeFile\(' -or
    $combined -notmatch 'retryFailed\(') {
    throw 'The task context menu must reuse real cancel, copy, and remove APIs.'
}
if ($page -match 'converterParallelJobsBox' -or
    $page -match 'formatOutputDirectoryRow') {
    throw 'The reference footer must not expose parallel-jobs or output-directory controls.'
}
foreach ($asset in @(
    'filter-3-line.svg', 'arrow-up-s-line.svg', 'checkbox-circle-line.svg',
    'error-warning-line.svg', 'file-music-fill.svg')) {
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
if ($page -notmatch 'Theme\.icon\("checkbox-circle-line"\)' -or
    $page -notmatch 'Theme\.icon\("error-warning-line"\)') {
    throw 'The footer summary must render the approved check-circle and warning icons.'
}

$iconNotice = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'assets/licenses/AgPlayer-Icons-License.txt')
$remixLicense = Join-Path $SourceRoot 'assets/licenses/RemixIcon-Apache-2.0.txt'
if ($iconNotice -notmatch 'Remix Icon v4\.6\.0' -or
    $iconNotice -notmatch 'arrow-up-s-line\.svg' -or
    $iconNotice -match 'does not redistribute any Remix') {
    throw 'The icon notice must accurately list the redistributed Remix Icon v4.6.0 assets.'
}
if (-not (Test-Path -LiteralPath $remixLicense) -or
    (Get-Content -Raw -LiteralPath $remixLicense) -notmatch 'Apache License\s+Version 2\.0') {
    throw 'The Remix Icon v4.6.0 Apache-2.0 license copy is missing or invalid.'
}
