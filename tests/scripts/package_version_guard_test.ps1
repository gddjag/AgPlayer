param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$AppPath
)

$ErrorActionPreference = 'Stop'
$packageScript = Join-Path $SourceRoot 'scripts\package-windows.ps1'
$versionTool = Join-Path $SourceRoot 'scripts\release-version.ps1'
$currentVersionOutput = (& $versionTool -SourceRoot $SourceRoot | Out-String).Trim()
if ($currentVersionOutput -notmatch '^AgPlayer release version: ([0-9]+\.[0-9]+\.[0-9]+)$') {
    throw "Unable to read current release version: $currentVersionOutput"
}
$currentVersion = $Matches[1]

$scratch = Join-Path ([IO.Path]::GetTempPath()) `
    ('agplayer-package-version-' + [guid]::NewGuid().ToString('N'))
try {
    foreach ($directory in @(
        'app', 'assets\licenses', 'cmake', 'installer', 'qt\src', 'scripts',
        'build\stale\app'
    )) {
        New-Item -ItemType Directory -Path (Join-Path $scratch $directory) `
            -Force | Out-Null
    }
    foreach ($relativePath in @(
        'CMakeLists.txt',
        'THIRD-PARTY-NOTICES.md',
        'app\CMakeLists.txt',
        'app\agplayer.manifest.in',
        'app\agplayer.rc.in',
        'app\main.cpp',
        'assets\licenses\AgPlayer-Icons-License.txt',
        'assets\licenses\Lucide-Icons-License.txt',
        'assets\licenses\RemixIcon-Apache-2.0.txt',
        'cmake\AgPlayerVersion.cmake',
        'installer\AgPlayer.iss',
        'qt\src\settings_controller.cpp',
        'scripts\package-windows.ps1',
        'scripts\release-version.ps1'
    )) {
        Copy-Item -LiteralPath (Join-Path $SourceRoot $relativePath) `
            -Destination (Join-Path $scratch $relativePath)
    }
    $sourceBuild = Join-Path $SourceRoot $BuildDirectory
    Copy-Item -LiteralPath (Join-Path $sourceBuild 'CMakeCache.txt') `
        -Destination (Join-Path $scratch 'build\stale\CMakeCache.txt')
    Copy-Item -LiteralPath $AppPath `
        -Destination (Join-Path $scratch 'build\stale\app\AgPlayer.exe')
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $AppPath) `
        'AgSeparationWorker.exe') `
        -Destination (Join-Path $scratch 'build\stale\app\AgSeparationWorker.exe')

    $staleVersion = if ($currentVersion -eq '2.3.4') { '2.3.5' } else { '2.3.4' }
    & (Join-Path $scratch 'scripts\release-version.ps1') `
        -SourceRoot $scratch -Version $staleVersion | Out-Null

    $mismatchRejected = $false
    try {
        & (Join-Path $scratch 'scripts\package-windows.ps1') `
            -BuildDirectory 'build\stale' -Configuration Release -SkipBuild `
            | Out-Null
    } catch {
        if ($_.Exception.Message -notmatch 'does not match release version') {
            throw
        }
        $mismatchRejected = $true
    }
    if (-not $mismatchRejected) {
        throw 'SkipBuild accepted a stale executable with a different PE version'
    }

    & (Join-Path $scratch 'scripts\release-version.ps1') `
        -SourceRoot $scratch -Version $currentVersion | Out-Null
    $matchingVersionPassedGuard = $false
    try {
        & (Join-Path $scratch 'scripts\package-windows.ps1') `
            -BuildDirectory 'build\stale' -Configuration Release -SkipBuild `
            | Out-Null
    } catch {
        if ($_.Exception.Message -notmatch 'Native runtime dependency not found') {
            throw
        }
        $matchingVersionPassedGuard = $true
    }
    if (-not $matchingVersionPassedGuard) {
        throw 'Matching PE version did not proceed past the version guard'
    }
} finally {
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
