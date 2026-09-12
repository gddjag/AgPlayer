[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-debug"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qa-runtime.ps1")
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$appPath = Join-Path $buildRoot "app/AgPlayer.exe"
$fixturePath = Join-Path $buildRoot "tests/fixtures/sine-440hz.wav"
$cachePath = Join-Path $buildRoot "CMakeCache.txt"

foreach ($requiredPath in @($appPath, $fixturePath, $cachePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required smoke-test input not found: $requiredPath"
    }
}

$smokeRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("agplayer-main-smoke-" + [Guid]::NewGuid().ToString("N"))
$screenshotPath = Join-Path $smokeRoot "main.png"
$logPath = Join-Path $smokeRoot "agplayer.log"
New-Item -ItemType Directory -Force -Path $smokeRoot | Out-Null

$process = Start-AgPlayerProcess `
    -BuildRoot $buildRoot `
    -CachePath $cachePath `
    -AppPath $appPath `
    -ArgumentList @(
            "--qa-test-mode",
            "--qa-log", $logPath,
            "--qa-play", $fixturePath,
            "--qa-screenshot-main", $screenshotPath
        ) `
    -Wait

if ($process.ExitCode -ne 0) {
    throw "AgPlayer smoke test exited with code $($process.ExitCode)"
}
if (-not (Test-Path -LiteralPath $screenshotPath) -or
    (Get-Item -LiteralPath $screenshotPath).Length -lt 10000) {
    throw "Main-window screenshot was not created: $screenshotPath"
}
if (-not (Test-Path -LiteralPath $logPath)) {
    throw "Explicit QA log was not created: $logPath"
}

$logText = Get-Content -Raw -Encoding UTF8 -LiteralPath $logPath
if ($logText -match "\[(WARN|ERROR|FATAL)\]") {
    throw "Runtime warnings or errors were recorded:`n$logText"
}

[pscustomobject]@{
    Executable = $appPath
    Fixture = $fixturePath
    Screenshot = $screenshotPath
    ScreenshotBytes = (Get-Item -LiteralPath $screenshotPath).Length
    Log = $logPath
    Result = "PASS"
}
