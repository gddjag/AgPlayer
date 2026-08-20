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
if ($page -notmatch 'plan\.error\s*\|\|\s*plan\.reason') {
    throw 'Every preflight rejection must surface the backend reason.'
}
if ($combined -notmatch 'cancelTask\(' -or
    $combined -notmatch 'copyText\(' -or
    $combined -notmatch 'removeFile\(' -or
    $combined -notmatch 'retryFailed\(') {
    throw 'The task context menu must reuse real cancel, copy, and remove APIs.'
}
if ($page -match 'objectName:\s*"formatFooterParallelJobs"' -or
    $page -match 'objectName:\s*"formatFooterOutputDirectory"') {
    throw 'The reference footer must not expose parallel-jobs or output-directory controls.'
}
