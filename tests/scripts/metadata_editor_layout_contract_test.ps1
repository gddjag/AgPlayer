param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
$page = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $SourceRoot 'app/qml/AgPlayer/components/tools/MetadataEditPage.qml')
$controller = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $SourceRoot 'qt/src/metadata_editor.cpp')

foreach ($control in @(
    'metadataToolbar', 'metadataFilePanel', 'metadataFileHeader',
    'metadataFileList', 'metadataFileFooter', 'metadataInspectorPanel',
    'metadataEditorHeader', 'metadataCoverSection', 'metadataChangePreview',
    'metadataCoverSummaryLabel',
    'metadataActionBar', 'metadataApplyButton', 'metadataCancelButton',
    'metadataSearchField', 'metadataStatusFilter',
    'metadataScopeBox', 'metadataProcessingModeBox',
    'metadataThreeStateHelp', 'metadataConversionSettingsButton',
    'metadataExportCurrentListButton',
    'metadataExportResultsButton', 'metadataPreflightDecisionDialog',
    'metadataErrorDialog')) {
    if ($page -notmatch ('objectName:\s*(?:index\s*===\s*0\s*\?\s*)?"' + $control + '"')) {
        throw "Missing reference metadata control: $control"
    }
}

foreach ($field in @('title', 'artist', 'album', 'albumArtist', 'genre',
                      'composer', 'date', 'customTag', 'bpm')) {
    if ($page -notmatch ('key:\s*"' + $field + '"')) {
        throw "Missing canonical metadata field: $field"
    }
}
if ($page -match 'key:\s*"year"') {
    throw 'Year must not remain an editable metadata field.'
}
$tagHeaderText = [string]::Concat([char]0x6807, [char]0x7b7e)
$customTagLabel = [string]::Concat([char]0x81ea, [char]0x5b9a, [char]0x4e49,
                                    [char]0x6807, [char]0x7b7e)
$tagHeaderPattern = 'ColumnHeader\s*\{\s*objectName:\s*"metadataTableTagHeader"\s*;\s*text:\s*qsTr\("' +
                    [regex]::Escape($tagHeaderText) +
                    '"\)\s*;\s*sortField:\s*"customTag"'
if ($page -notmatch $tagHeaderPattern) {
    throw 'The metadata table must expose the custom tag column as the custom tag label.'
}
$customTagFieldPattern = '\{\s*key:\s*"customTag",\s*label:\s*qsTr\("' +
                         [regex]::Escape($customTagLabel) + '"\)\s*\}'
if ($page -notmatch $customTagFieldPattern) {
    throw 'The editable custom tag field must have the Chinese custom tag label.'
}
if ($page -notmatch 'objectName:\s*"metadataValueField_"\s*\+\s*fieldRow\.fieldKey') {
    throw 'Metadata fields must remain directly addressable.'
}

foreach ($mode in @('keep', 'set', 'clear')) {
    if ($page -notmatch ('value:\s*"' + $mode + '"')) {
        throw "Missing metadata three-state action: $mode"
    }
}

foreach ($obsoleteField in @('track', 'disc', 'comment', 'lyrics')) {
    if ($page -match ('key:\s*"' + $obsoleteField + '"')) {
        throw "Out-of-scope metadata field remains: $obsoleteField"
    }
}

foreach ($obsoleteControl in @('metadataPreflightButton')) {
    if ($page -match ([regex]::Escape($obsoleteControl))) {
        throw "Obsolete metadata control remains visible in the reference layout: $obsoleteControl"
    }
}

if ($page -notmatch 'readonly property real inspectorRatio:\s*0\.44') {
    throw 'The desktop metadata inspector must use the reference 44 percent width.'
}
if ($page -notmatch 'readonly property real desktopMinimumWidth:\s*1206') {
    throw 'Responsive mode must switch at the real combined panel minimum width.'
}
if ($page -notmatch 'MetadataEditor\.aggregateMetadata\(aggregateTargetIndices\(\)\)') {
    throw 'Field and cover previews must aggregate the active task scope.'
}
if ($page -notmatch 'Layout\.preferredHeight:\s*Theme\.controlHeightProminent') {
    throw 'Metadata toolbar actions must use the shared prominent control height.'
}
if ($page -notmatch 'MetadataEditor\.applyMetadata\(payload, targets\)') {
    throw 'The apply button must invoke the metadata transaction controller.'
}
if ($page -notmatch 'MetadataEditor\.cancel\(\)') {
    throw 'The cancel action must remain wired to the controller while busy.'
}
if ($page -notmatch 'FormatConverter\.setMetadataEditPlanForFiles\(') {
    throw 'Conversion mode must reuse a target-scoped metadata edit plan.'
}
if ($page -notmatch 'encodeURI\(localPath\)\.replace\(/#/g,\s*"%23"\)') {
    throw 'Conversion targets must preserve legal Windows filenames containing #.'
}
if ($page -notmatch 'function onErrorOccurred\(message\)') {
    throw 'Controller errors must be visible instead of silently ignored.'
}
if ($controller -notmatch '(?s)catch \(const std::exception& exception\).*?writeResult\.error_code\s*=\s*agplayer::MetadataErrorCode::InternalError.*?catch \(\.\.\.\).*?writeResult\.error_code\s*=\s*agplayer::MetadataErrorCode::InternalError') {
    throw 'Metadata writer exceptions must expose InternalError through the result row.'
}
if ($controller -notmatch 'failedCount_\s*=\s*summary\.failureCount') {
    throw 'Preflight internal failures must propagate to the public failed count.'
}

Write-Output 'Metadata editor pixel-reference layout contract passed.'
