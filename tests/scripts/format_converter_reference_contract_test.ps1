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
    'formatToolbar', 'formatStatusFilters', 'formatSelectAllCheck',
    'formatTaskPanel', 'formatSettingsPanel', 'formatBottomBar',
    'formatEncoderBox', 'formatOutputDirectoryRow', 'formatSummaryCard')) {
    if ($combined -notmatch [regex]::Escape($control)) {
        throw "The reference format-conversion workbench is missing $control."
    }
}

if ($page -match 'formatSearchField|converterParallelJobsBox') {
    throw 'Search and concurrency controls must not remain in the format workbench.'
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
