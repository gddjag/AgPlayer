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
    'AgPlayer\.Desktop'
)
foreach ($pattern in $required) {
    if ($main -notmatch $pattern) {
        throw "Missing native taskbar icon contract: $pattern"
    }
}
