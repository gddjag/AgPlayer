param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$page = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/MetadataEditPage.qml')

foreach ($control in @(
    'metadataFilePanel', 'metadataInspectorPanel', 'metadataBottomBar',
    'metadataSearchField', 'metadataStatusFilter',
    'metadataExportCurrentListButton', 'metadataPreflightDecisionDialog',
    'metadataPreflightButton', 'metadataExportResultsButton',
    'metadataApplyButton', 'metadataCoverSection', 'metadataChangePreview')) {
    if ($page -notmatch ('objectName:\s*"' + $control + '"')) {
        throw "Missing reference metadata control: $control"
    }
}

foreach ($field in @('title', 'artist', 'album', 'albumArtist', 'genre',
                      'year', 'date', 'composer', 'bpm')) {
    if ($page -notmatch ('key:\s*"' + $field + '"')) {
        throw "Missing canonical metadata field: $field"
    }
}

foreach ($mode in @('keep', 'set', 'clear')) {
    if ($page -notmatch ('value:\s*"' + $mode + '"')) {
        throw "Missing metadata three-state action: $mode"
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
