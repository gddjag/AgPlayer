param(
    [Parameter(Mandatory = $true)]
    [string]$AppPath
)

$ErrorActionPreference = 'Stop'
$appDirectory = Split-Path -Parent $AppPath
foreach ($relativePath in @(
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'Qt6Qml.dll',
    'Qt6Quick.dll',
    'Qt6QuickControls2.dll',
    'avcodec-62.dll',
    'avformat-62.dll',
    'avutil-60.dll',
    'swresample-6.dll',
    'swscale-9.dll',
    'platforms\qwindows.dll'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $appDirectory $relativePath))) {
        throw "Missing deployed runtime dependency: $relativePath"
    }
}
