param([Parameter(Mandatory=$true)][string]$SourceRoot)

$main = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'app/main.cpp')
$required = @(
    'LoadImageW\s*\(',
    'constexpr int kAgPlayerIconResourceId\s*=\s*101',
    'MAKEINTRESOURCEW\s*\(\s*kAgPlayerIconResourceId\s*\)',
    'WM_SETICON',
    'ICON_BIG',
    'ICON_SMALL',
    'WM_GETICON',
    'IPropertyStore.*GetValue|properties->GetValue',
    'PropVariantClear',
    'PKEY_AppUserModel_RelaunchCommand',
    'PKEY_AppUserModel_RelaunchDisplayNameResource',
    'PKEY_AppUserModel_RelaunchIconResource',
    'AgPlayer\.Desktop'
)
foreach ($pattern in $required) {
    if ($main -notmatch $pattern) {
        throw "Missing native taskbar icon contract: $pattern"
    }
}

$waveformIcon = Get-Content -Raw -LiteralPath (
    Join-Path $SourceRoot 'assets/icons/waveform-switch.svg')
if ($waveformIcon -notmatch 'M584\.1 851\.5l-145-592\.7' -or
    $waveformIcon -notmatch 'fill="currentColor"') {
    throw 'The shared waveform switch icon must use the approved uploaded artwork and theme color.'
}

$emptyFavoriteIcon = Get-Content -Raw -LiteralPath (
    Join-Path $SourceRoot 'assets/icons/heart-line.svg')
if ($emptyFavoriteIcon -notmatch 'M707\.584 93\.184c-77\.312' -or
    $emptyFavoriteIcon -notmatch 'fill="currentColor"') {
    throw 'The empty favorite icon must use the approved uploaded artwork and theme color.'
}
