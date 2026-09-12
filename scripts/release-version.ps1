[CmdletBinding(DefaultParameterSetName = 'Read')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Set')]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version,
    [Parameter(Mandatory = $true, ParameterSetName = 'Bump')]
    [ValidateSet('Patch', 'Minor', 'Major')]
    [string]$Bump,
    [Parameter(ParameterSetName = 'Bump')]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$FromVersion,
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
$versionMatches = [regex]::Matches($versionSource, $versionPattern)
if ($versionMatches.Count -ne 1) {
    throw 'cmake/AgPlayerVersion.cmake must contain one semantic release version'
}
$versionMatch = $versionMatches[0]
$currentVersion = $versionMatch.Groups[1].Value
$targetVersion = $currentVersion

if ($PSCmdlet.ParameterSetName -eq 'Set') {
    $targetVersion = $Version
} elseif ($PSCmdlet.ParameterSetName -eq 'Bump') {
    $baseVersion = if ($FromVersion) { $FromVersion } else { $currentVersion }
    $parts = $baseVersion.Split('.') | ForEach-Object { [long]$_ }
    switch ($Bump) {
        'Patch' { $parts[2]++ }
        'Minor' { $parts[1]++; $parts[2] = 0 }
        'Major' { $parts[0]++; $parts[1] = 0; $parts[2] = 0 }
    }
    $targetVersion = $parts -join '.'
    if ($FromVersion -and $currentVersion -ne $FromVersion -and $currentVersion -ne $targetVersion) {
        throw "Release baseline mismatch: source is $currentVersion, expected $FromVersion or retry target $targetVersion"
    }
}
foreach ($part in $targetVersion.Split('.')) {
    if ([long]$part -gt 65535) {
        throw 'Release version components must fit the Windows PE version range 0..65535'
    }
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

$manifestPath = Join-Path $SourceRoot 'deployment\updates\latest.json'
$manifestSource = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath
$manifest = $manifestSource | ConvertFrom-Json
$manifestPattern = '(?m)^(\s*"version"\s*:\s*")[^"]*(")'
if ($manifest.schemaVersion -ne 1 -or $manifest.version -isnot [string] -or
    [regex]::Matches($manifestSource, $manifestPattern).Count -ne 1) {
    throw 'The website update manifest must contain one version field and schemaVersion 1'
}
if ($PSCmdlet.ParameterSetName -eq 'Read') {
    if ($manifest.version -ne $currentVersion) {
        throw "Website manifest version $($manifest.version) does not match release version $currentVersion"
    }
} else {
    $updatedSource = [regex]::Replace($versionSource, $versionPattern,
        "set(AGPLAYER_VERSION `"$targetVersion`")")
    $updatedManifest = [regex]::Replace($manifestSource, $manifestPattern,
        ('${1}' + $targetVersion + '${2}'))
    # Stage both writes before replacing either source. Preserve every other
    # manifest field and restore the original version source if replacement fails.
    $originalVersion = [IO.File]::ReadAllBytes($versionPath)
    $temporaryVersion = "$versionPath.$([guid]::NewGuid().ToString('N')).tmp"
    $temporaryManifest = "$manifestPath.$([guid]::NewGuid().ToString('N')).tmp"
    $versionReplaced = $false
    try {
        $utf8 = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::WriteAllText($temporaryVersion, $updatedSource, $utf8)
        [IO.File]::WriteAllText($temporaryManifest, $updatedManifest, $utf8)
        [IO.File]::Replace($temporaryVersion, $versionPath, [NullString]::Value)
        $versionReplaced = $true
        [IO.File]::Replace($temporaryManifest, $manifestPath, [NullString]::Value)
    } catch {
        if ($versionReplaced) { [IO.File]::WriteAllBytes($versionPath, $originalVersion) }
        throw
    } finally {
        foreach ($temporary in @($temporaryVersion, $temporaryManifest)) {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
}

Write-Output "AgPlayer release version: $targetVersion"
