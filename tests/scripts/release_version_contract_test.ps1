param([Parameter(Mandatory=$true)][string]$SourceRoot)

$ErrorActionPreference = 'Stop'
$versionTool = Join-Path $SourceRoot 'scripts\release-version.ps1'
if (-not (Test-Path -LiteralPath $versionTool)) {
    throw 'Missing repository release version updater/validator'
}

$current = (& $versionTool -SourceRoot $SourceRoot | Out-String).Trim()
if ($current -notmatch '^AgPlayer release version: [0-9]+\.[0-9]+\.[0-9]+$') {
    throw "Release version validation failed: $current"
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('agplayer-version-contract-' + [guid]::NewGuid())
try {
    foreach ($directory in @('cmake', 'app', 'qt\src', 'installer', 'scripts')) {
        New-Item -ItemType Directory -Path (Join-Path $scratch $directory) -Force | Out-Null
    }
    foreach ($relativePath in @(
        'CMakeLists.txt',
        'cmake\AgPlayerVersion.cmake',
        'app\CMakeLists.txt',
        'app\agplayer.rc.in',
        'app\agplayer.manifest.in',
        'app\main.cpp',
        'qt\src\settings_controller.cpp',
        'installer\AgPlayer.iss',
        'scripts\package-windows.ps1',
        'scripts\release-version.ps1'
    )) {
        Copy-Item -LiteralPath (Join-Path $SourceRoot $relativePath) `
            -Destination (Join-Path $scratch $relativePath)
    }

    $updated = (& (Join-Path $scratch 'scripts\release-version.ps1') `
        -SourceRoot $scratch -Version '2.3.4' | Out-String).Trim()
    if ($updated -ne 'AgPlayer release version: 2.3.4') {
        throw "Release version bump failed: $updated"
    }
    $versionSource = Get-Content -Raw -LiteralPath (Join-Path $scratch 'cmake\AgPlayerVersion.cmake')
    if ($versionSource -notmatch 'set\(AGPLAYER_VERSION\s+"2\.3\.4"\)') {
        throw 'Version bump must update the single CMake version source'
    }

    $validated = (& (Join-Path $scratch 'scripts\release-version.ps1') `
        -SourceRoot $scratch | Out-String).Trim()
    if ($validated -ne 'AgPlayer release version: 2.3.4') {
        throw "Updated release version did not validate: $validated"
    }
} finally {
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
