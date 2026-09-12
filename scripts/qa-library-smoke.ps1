[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-debug"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qa-runtime.ps1")
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$appPath = Join-Path $buildRoot "app/AgPlayer.exe"
$fixturesPath = Join-Path $buildRoot "tests/fixtures/formats"
$cachePath = Join-Path $buildRoot "CMakeCache.txt"

foreach ($requiredPath in @($appPath, $fixturesPath, $cachePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required library-smoke input not found: $requiredPath"
    }
}

$expectedExtensions = @(".wav", ".mp3", ".flac", ".aac",
    ".m4a", ".ogg", ".opus", ".wma")
$fixtureFiles = @(Get-ChildItem -LiteralPath $fixturesPath -File |
    Where-Object { $expectedExtensions -contains $_.Extension.ToLowerInvariant() })
if ($fixtureFiles.Count -ne $expectedExtensions.Count) {
    throw "Expected 8 format fixtures, found $($fixtureFiles.Count)"
}

$runtimePaths = Get-AgPlayerRuntimePaths `
    -BuildRoot $buildRoot -CachePath $cachePath

$smokeRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("agplayer-library-smoke-" + [Guid]::NewGuid().ToString("N"))
$libraryPath = Join-Path $smokeRoot "library.json"
$firstScreenshot = Join-Path $smokeRoot "imported-list.png"
$secondScreenshot = Join-Path $smokeRoot "restored-list.png"
$firstLog = Join-Path $smokeRoot "import.log"
$secondLog = Join-Path $smokeRoot "restore.log"
$originalPath = $env:Path

function Invoke-LibraryRun {
    param(
        [string[]]$Arguments,
        [string]$Screenshot,
        [string]$Log
    )

    $process = Start-Process -FilePath $appPath `
        -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id -Force
        throw "AgPlayer library smoke timed out"
    }
    if ($process.ExitCode -ne 0) {
        throw "AgPlayer library smoke exited with code $($process.ExitCode)"
    }
    if (-not (Test-Path -LiteralPath $Screenshot) -or
        (Get-Item -LiteralPath $Screenshot).Length -lt 10000) {
        throw "List-window screenshot was not created: $Screenshot"
    }
    if (-not (Test-Path -LiteralPath $Log)) {
        throw "QA log was not created: $Log"
    }
    $logText = Get-Content -Raw -Encoding UTF8 -LiteralPath $Log
    if ($logText -match "\[(WARN|ERROR|FATAL)\]") {
        throw "Runtime warnings or errors were recorded:`n$logText"
    }
}

function Convert-CanonicalPath {
    param([string]$Path)
    return ([System.IO.Path]::GetFullPath($Path) -replace "\\", "/").ToLowerInvariant()
}

New-Item -ItemType Directory -Force -Path $smokeRoot | Out-Null

try {
    $env:Path = ($runtimePaths -join ";") + ";" + $originalPath
    Invoke-LibraryRun -Screenshot $firstScreenshot -Log $firstLog -Arguments @(
        "--qa-test-mode",
        "--qa-log", $firstLog,
        "--qa-library", $libraryPath,
        "--qa-import-folder", $fixturesPath,
        "--qa-screenshot-list", $firstScreenshot
    )

    if (-not (Test-Path -LiteralPath $libraryPath)) {
        throw "Library JSON was not persisted: $libraryPath"
    }
    $tracks = Get-Content -Raw -Encoding UTF8 -LiteralPath $libraryPath |
        ConvertFrom-Json
    if ($tracks.Count -ne $expectedExtensions.Count) {
        throw "Expected 8 persisted tracks, found $($tracks.Count)"
    }

    $expectedPaths = @($fixtureFiles |
        ForEach-Object { Convert-CanonicalPath $_.FullName } | Sort-Object)
    $persistedPaths = @($tracks |
        ForEach-Object { Convert-CanonicalPath ([string]$_.path) } | Sort-Object)
    if (Compare-Object $expectedPaths $persistedPaths) {
        throw "Persisted library paths do not match the format fixtures"
    }
    if (@($tracks | Where-Object { [string]::IsNullOrWhiteSpace($_.format) }).Count -gt 0) {
        throw "One or more persisted tracks have no detected format"
    }

    Invoke-LibraryRun -Screenshot $secondScreenshot -Log $secondLog -Arguments @(
        "--qa-test-mode",
        "--qa-log", $secondLog,
        "--qa-library", $libraryPath,
        "--qa-screenshot-list", $secondScreenshot
    )

    [pscustomobject]@{
        Executable = $appPath
        Library = $libraryPath
        Tracks = $tracks.Count
        ImportedScreenshot = $firstScreenshot
        RestoredScreenshot = $secondScreenshot
        Result = "PASS"
    }
}
finally {
    $env:Path = $originalPath
}
