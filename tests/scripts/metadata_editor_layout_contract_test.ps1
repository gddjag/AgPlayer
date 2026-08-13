param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$page = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/MetadataEditPage.qml')

foreach ($control in @(
    'metadataFilePanel', 'metadataInspectorPanel', 'metadataBottomBar',
    'metadataStatusFilter',
    'metadataExportCurrentListButton', 'metadataPreflightDecisionDialog',
    'metadataPreflightButton', 'metadataExportResultsButton',
    'metadataApplyButton', 'metadataCoverSection', 'metadataChangePreview')) {
    if ($page -notmatch ('objectName:\s*(?:index\s*===\s*0\s*\?\s*)?"' + $control + '"')) {
        throw "Missing reference metadata control: $control"
    }
}

if ($page -match 'metadataSearchField') {
    throw 'Metadata status filters must be directly visible without a search field.'
}
if ($page -notmatch 'objectName:\s*"metadataStatusFilter"[\s\S]{0,480}text:\s*qsTr\("任务列表："\)') {
    throw 'Metadata status filters must sit directly above the task table with a task-list label.'
}

foreach ($field in @('title', 'artist', 'album', 'albumArtist', 'genre',
                      'year', 'date', 'composer', 'bpm')) {
    if ($page -notmatch ('key:\s*"' + $field + '"')) {
        throw "Missing canonical metadata field: $field"
    }
}

foreach ($obsoleteControl in @('metadataModeButton_', 'metadataThreeStateHelp',
                                'coverModeBox')) {
    if ($page -match [regex]::Escape($obsoleteControl)) {
        throw "Obsolete metadata mode control remains: $obsoleteControl"
    }
}
foreach ($directEditPhrase in @('不改动即保留', '留空即清除', '多个值')) {
    if ($page -notmatch [regex]::Escape($directEditPhrase)) {
        throw "Missing direct-edit metadata semantics: $directEditPhrase"
    }
}

if ($page -notmatch 'Layout\.preferredWidth:\s*Math\.max\(480, page\.width \* 0\.36\)') {
    throw 'The metadata inspector must preserve the reference desktop width.'
}
if ($page -notmatch 'MetadataEditor\.applyMetadata\(payload, targets\)') {
    throw 'The apply button must invoke the metadata transaction controller.'
}
if ($page -notmatch 'MetadataEditor\.preflightMetadata\(payload, targets\)') {
    throw 'The preflight button must invoke asynchronous capability checking.'
}
if ($page -notmatch 'MetadataEditor\.cancel\(\)') {
    throw 'The cancel action must remain wired to the controller.'
}

Write-Output 'Metadata editor reference layout contract passed.'
