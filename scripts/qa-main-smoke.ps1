[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-debug"
)

$ErrorActionPreference = "Stop"
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

$prefixLine = Select-String -LiteralPath $cachePath `
    -Pattern "^CMAKE_PREFIX_PATH:[^=]*=(.+)$" | Select-Object -First 1
if ($null -eq $prefixLine) {
    throw "CMAKE_PREFIX_PATH was not found in $cachePath"
}

$qtPrefix = $prefixLine.Matches[0].Groups[1].Value
$runtimePaths = @(
    (Join-Path $qtPrefix "bin"),
    (Join-Path $buildRoot "vcpkg_installed/x64-windows/debug/bin"),
    (Join-Path $buildRoot "vcpkg_installed/x64-windows/bin")
) | Where-Object { Test-Path -LiteralPath $_ }

$smokeRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("agplayer-main-smoke-" + [Guid]::NewGuid().ToString("N"))
$screenshotPath = Join-Path $smokeRoot "main.png"
$logPath = Join-Path $smokeRoot "agplayer.log"
$originalPath = $env:Path

New-Item -ItemType Directory -Force -Path $smokeRoot | Out-Null

try {
    $env:Path = ($runtimePaths -join ";") + ";" + $originalPath
    $process = Start-Process -FilePath $appPath `
        -ArgumentList @(
            "--qa-test-mode",
            "--qa-log", $logPath,
            "--qa-play", $fixturePath,
            "--qa-screenshot-main", $screenshotPath
        ) `
        -Wait -PassThru

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
}
finally {
    $env:Path = $originalPath
}
