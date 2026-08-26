[CmdletBinding()]
param(
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version,
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
} else {
    $SourceRoot = (Resolve-Path $SourceRoot).Path
}

$versionPath = Join-Path $SourceRoot 'cmake\AgPlayerVersion.cmake'
if (-not (Test-Path -LiteralPath $versionPath)) {
    throw "Release version source not found: $versionPath"
}
$versionSource = Get-Content -Raw -Encoding UTF8 -LiteralPath $versionPath
$versionPattern = 'set\(AGPLAYER_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"\)'
$versionMatch = [regex]::Match($versionSource, $versionPattern)
if (-not $versionMatch.Success) {
    throw 'cmake/AgPlayerVersion.cmake must contain one semantic release version'
}

if (-not [string]::IsNullOrWhiteSpace($Version)) {
    $versionSource = [regex]::Replace(
        $versionSource,
        $versionPattern,
        "set(AGPLAYER_VERSION `"$Version`")",
        1)
    Set-Content -LiteralPath $versionPath -Value $versionSource.TrimEnd() `
        -Encoding UTF8
}

$currentVersion = if ([string]::IsNullOrWhiteSpace($Version)) {
    $versionMatch.Groups[1].Value
} else {
    $Version
}

function Assert-ReleaseContract {
    param([string]$RelativePath, [string[]]$Patterns)
    $path = Join-Path $SourceRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Release surface not found: $RelativePath"
    }
    $content = Get-Content -Raw -Encoding UTF8 -LiteralPath $path
    foreach ($pattern in $Patterns) {
        if ($content -notmatch $pattern) {
            throw "Release version is not derived in ${RelativePath}: $pattern"
        }
    }
}

Assert-ReleaseContract 'CMakeLists.txt' @(
    'include\(cmake/AgPlayerVersion\.cmake\)',
    'project\(AgPlayer\s+VERSION\s+"\$\{AGPLAYER_VERSION\}"')
Assert-ReleaseContract 'app\CMakeLists.txt' @(
    'configure_file\(agplayer\.manifest\.in',
    'configure_file\(agplayer\.rc\.in')
Assert-ReleaseContract 'app\agplayer.rc.in' @(
    'FILEVERSION\s+@PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0',
    'ProductVersion",\s+"@PROJECT_VERSION@\.0')
Assert-ReleaseContract 'app\agplayer.manifest.in' @(
    'version="@PROJECT_VERSION@\.0"')
Assert-ReleaseContract 'app\main.cpp' @(
    'setApplicationVersion',
    'agplayer::version::kVersion')
Assert-ReleaseContract 'qt\src\settings_controller.cpp' @(
    'agplayer::version::kVersion')
Assert-ReleaseContract 'installer\AgPlayer.iss' @(
    '#ifndef AppVersion',
    'AppVersion=\{#AppVersion\}',
    'OutputBaseFilename=AgPlayer-Setup-\{#AppVersion\}-x64')
Assert-ReleaseContract 'scripts\package-windows.ps1' @(
    'release-version\.ps1',
    '/DAppVersion=')

Write-Output "AgPlayer release version: $currentVersion"
