param(
    [Parameter(Mandatory = $true)]
    [string]$AppPath
)

$ErrorActionPreference = 'Stop'
$appDirectory = Split-Path -Parent $AppPath
foreach ($relativePath in @(
    'Qt6Cored.dll',
    'Qt6Guid.dll',
    'Qt6Widgetsd.dll',
    'Qt6Qmld.dll',
    'Qt6Quickd.dll',
    'Qt6QuickControls2d.dll'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $appDirectory $relativePath))) {
        throw "Missing deployed debug runtime dependency: $relativePath"
    }
}
