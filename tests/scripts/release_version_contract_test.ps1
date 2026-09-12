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
    foreach ($directory in @('cmake', 'app', 'qt\src', 'installer', 'scripts', 'deployment\updates')) {
        New-Item -ItemType Directory -Path (Join-Path $scratch $directory) -Force | Out-Null
    }
    foreach ($relativePath in @(
        'CMakeLists.txt',
        'cmake\AgPlayerVersion.cmake',
        'deployment\updates\latest.json',
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

    $manifestPath = Join-Path $scratch 'deployment\updates\latest.json'
    if ((Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json).version -ne '2.3.4') {
        throw 'The website update manifest must follow the release version'
    }

    $fixtureTool = Join-Path $scratch 'scripts\release-version.ps1'
    $extendedManifest = "{`n  `"schemaVersion`": 1,`n  `"version`": `"2.3.4`",`n  `"downloadUrl`": `"https://example.test/download`",`n  `"notes`": [`"unchanged`"]`n}`n"
    [IO.File]::WriteAllText($manifestPath, $extendedManifest)
    & $fixtureTool -SourceRoot $scratch -Version '2.3.5' | Out-Null
    if ([IO.File]::ReadAllText($manifestPath) -cne $extendedManifest.Replace('2.3.4', '2.3.5')) {
        throw 'Updating a release must preserve every other manifest field and its formatting'
    }
    foreach ($case in @(
        @{ Bump = 'Patch'; Expected = '2.3.5' },
        @{ Bump = 'Minor'; Expected = '2.4.0' },
        @{ Bump = 'Major'; Expected = '3.0.0' }
    )) {
        & $fixtureTool -SourceRoot $scratch -Version '2.3.4' | Out-Null
        $bumped = (& $fixtureTool -SourceRoot $scratch -Bump $case.Bump | Out-String).Trim()
        if ($bumped -ne "AgPlayer release version: $($case.Expected)" -or
            (Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json).version -ne $case.Expected) {
            throw "Automatic $($case.Bump) increment did not synchronize both version sources"
        }
    }
    & $fixtureTool -SourceRoot $scratch -Version '2.3.4' | Out-Null
    $firstRelease = (& $fixtureTool -SourceRoot $scratch -Bump Patch -FromVersion '2.3.4' | Out-String).Trim()
    $retryRelease = (& $fixtureTool -SourceRoot $scratch -Bump Patch -FromVersion '2.3.4' | Out-String).Trim()
    if ($firstRelease -ne 'AgPlayer release version: 2.3.5' -or $retryRelease -ne $firstRelease) {
        throw 'Repeating the same release baseline must not increment the version again'
    }
    $wrongBaselineRejected = $false
    try { & $fixtureTool -SourceRoot $scratch -Bump Patch -FromVersion '1.0.0' | Out-Null }
    catch { $wrongBaselineRejected = $_.Exception.Message -match 'baseline mismatch' }
    if (-not $wrongBaselineRejected) { throw 'A stale release baseline must be rejected' }
    & $fixtureTool -SourceRoot $scratch -Version '65535.0.0' | Out-Null
    $overflowRejected = $false
    try { & $fixtureTool -SourceRoot $scratch -Bump Major | Out-Null }
    catch { $overflowRejected = $_.Exception.Message -match 'Windows PE version range' }
    if (-not $overflowRejected -or
        (Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json).version -ne '65535.0.0') {
        throw 'A release increment must not overflow Windows PE version components'
    }
    & $fixtureTool -SourceRoot $scratch -Version '2.3.4' | Out-Null

    # Command-line rejection must happen before external packaging tools run.
    foreach ($invalidArguments in @(
        @{ NewRelease = $true },
        @{ NewRelease = $true; FromVersion = '2.3.4'; SkipBuild = $true },
        @{ FromVersion = '2.3.4' }
    )) {
        $rejected = $false
        try { & (Join-Path $scratch 'scripts\package-windows.ps1') @invalidArguments | Out-Null }
        catch { $rejected = $_.Exception.Message -match 'NewRelease.*FromVersion|NewRelease.*SkipBuild|FromVersion.*NewRelease' }
        if (-not $rejected) { throw 'Invalid new-release arguments must be rejected before packaging preflight' }
    }

    $validated = (& (Join-Path $scratch 'scripts\release-version.ps1') `
        -SourceRoot $scratch | Out-String).Trim()
    if ($validated -ne 'AgPlayer release version: 2.3.4') {
        throw "Updated release version did not validate: $validated"
    }

    $versionPath = Join-Path $scratch 'cmake\AgPlayerVersion.cmake'
    $beforeVersion = [IO.File]::ReadAllBytes($versionPath)
    $beforeManifest = [IO.File]::ReadAllBytes($manifestPath)
    $lockedManifest = [IO.File]::Open($manifestPath, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $lockedWriteRejected = $false
    try {
        try { & $fixtureTool -SourceRoot $scratch -Bump Patch | Out-Null }
        catch { $lockedWriteRejected = $true }
    } finally { $lockedManifest.Dispose() }
    if (-not $lockedWriteRejected -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($versionPath)) -ne [Convert]::ToBase64String($beforeVersion) -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($manifestPath)) -ne [Convert]::ToBase64String($beforeManifest)) {
        throw 'A failed manifest replacement must roll back the CMake version source'
    }
    Set-Content -LiteralPath (Join-Path $scratch 'app\agplayer.rc.in') -Value 'broken contract'
    $rejected = $false
    try { & $fixtureTool -SourceRoot $scratch -Version '9.8.7' | Out-Null } catch { $rejected = $true }
    if (-not $rejected -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($versionPath)) -ne [Convert]::ToBase64String($beforeVersion) -or
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($manifestPath)) -ne [Convert]::ToBase64String($beforeManifest)) {
        throw 'A failed release contract validation must leave both files byte-for-byte unchanged'
    }
} finally {
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
