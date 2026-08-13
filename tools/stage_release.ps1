[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build\msvc-release'),
    [string]$PackageDirectory = (Join-Path $PSScriptRoot '..\build\package\AgPlayer')
)

$ErrorActionPreference = 'Stop'
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$appDirectory = Join-Path $BuildDirectory 'app'
$executable = Join-Path $appDirectory 'AgPlayer.exe'

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Release executable not found: $executable"
}
if (-not $PackageDirectory.StartsWith(([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\build'))),
                                      [StringComparison]::OrdinalIgnoreCase)) {
    throw 'PackageDirectory must remain under this repository build directory'
}

if (Test-Path -LiteralPath $PackageDirectory) {
    Remove-Item -LiteralPath $PackageDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $PackageDirectory -Force | Out-Null

# Keep only verified application runtime files.  A broad *.dll copy can pull
# in Windows system DLLs during local development and makes an installer huge.
$runtimePatterns = @(
    'AgPlayer.exe', 'vc_redist.x64.exe',
    'Qt6*.dll', 'avcodec-*.dll', 'avformat-*.dll', 'avutil-*.dll',
    'swresample-*.dll', 'libmp3lame.DLL', 'ogg.dll', 'opus.dll',
    'vorbis.dll', 'vorbisenc.dll', 'dxcompiler.dll', 'dxil.dll',
    'd3dcompiler_47.dll', 'opengl32sw.dll', 'msvcp140*.dll',
    'vcruntime140*.dll', 'vccorlib140.dll', 'concrt140.dll'
)
Get-ChildItem -LiteralPath $appDirectory -File | Where-Object {
    $fileName = $_.Name
    $runtimePatterns | Where-Object { $fileName -like $_ } | Select-Object -First 1
} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $PackageDirectory -Force
}

foreach ($directoryName in @(
    'platforms', 'imageformats', 'iconengines', 'generic', 'networkinformation',
    'styles', 'tls', 'qml'
)) {
    $source = Join-Path $appDirectory $directoryName
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $PackageDirectory $directoryName) -Recurse -Force
    }
}

foreach ($required in @(
    'AgPlayer.exe', 'Qt6Core.dll', 'Qt6Widgets.dll', 'Qt6Quick.dll',
    'avcodec-62.dll', 'avformat-62.dll', 'platforms\qwindows.dll'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory $required))) {
        throw "Release staging is incomplete: $required"
    }
}

$size = (Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File |
    Measure-Object -Property Length -Sum).Sum
Write-Output ("Staged {0} files ({1:N1} MB) at {2}" -f
    (Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File).Count,
    ($size / 1MB), $PackageDirectory)
