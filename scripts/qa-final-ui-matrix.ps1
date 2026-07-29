[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-release",
    [string]$OutputDirectory = "build/qa/final-ui-matrix",
    [ValidateSet("zh", "en", "th", "vi")]
    [string[]]$Languages = @("zh", "en", "th", "vi"),
    [ValidateSet("dark", "light")]
    [string[]]$Themes = @("dark", "light")
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$appPath = Join-Path $buildRoot "app/AgPlayer.exe"
$cachePath = Join-Path $buildRoot "CMakeCache.txt"
$playFixture = Join-Path $buildRoot "tests/fixtures/sine-440hz.wav"
$formatFixtures = Join-Path $buildRoot "tests/fixtures/formats"
$outputPath = Join-Path $repoRoot $OutputDirectory

foreach ($requiredPath in @(
    $appPath, $cachePath, $playFixture, $formatFixtures
)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required UI-matrix input not found: $requiredPath"
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
    (Join-Path $buildRoot "vcpkg_installed/x64-windows/bin"),
    (Join-Path $buildRoot "vcpkg_installed/x64-windows/debug/bin")
) | Where-Object { Test-Path -LiteralPath $_ }

$originalPath = $env:Path
$results = [System.Collections.Generic.List[object]]::new()
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

function Invoke-Capture {
    param(
        [string]$Language,
        [string]$Theme,
        [string]$Surface,
        [string[]]$Arguments
    )

    $stem = "{0}-{1}-{2}" -f $Language, $Theme, $Surface
    $screenshot = Join-Path $outputPath ($stem + ".png")
    $log = Join-Path $outputPath ($stem + ".log")
    Remove-Item -LiteralPath $screenshot, $log -Force -ErrorAction SilentlyContinue
    $common = @(
        "--qa-test-mode",
        "--qa-log", $log,
        "--qa-language", $Language,
        "--qa-theme", $Theme
    )
    $process = Start-Process -FilePath $appPath `
        -ArgumentList ($common + $Arguments + @($screenshot)) `
        -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "$stem exited with code $($process.ExitCode)"
    }
    if (-not (Test-Path -LiteralPath $screenshot) -or
        (Get-Item -LiteralPath $screenshot).Length -lt 10000) {
        throw "$stem did not create a valid screenshot"
    }
    if (-not (Test-Path -LiteralPath $log)) {
        throw "$stem did not create a runtime log"
    }
    $logText = Get-Content -Raw -Encoding UTF8 -LiteralPath $log
    if ($logText -match "\[(WARN|ERROR|FATAL)\]" -or
        $logText -match "QQml|ReferenceError|TypeError") {
        throw "$stem recorded runtime problems:`n$logText"
    }
    $results.Add([pscustomobject]@{
        Language = $Language
        Theme = $Theme
        Surface = $Surface
        Bytes = (Get-Item -LiteralPath $screenshot).Length
        Result = "PASS"
    })
}

try {
    $env:Path = ($runtimePaths -join ";") + ";" + $originalPath

    foreach ($language in $Languages) {
        foreach ($theme in $Themes) {
            $stateRoot = Join-Path $outputPath (
                "state-{0}-{1}-{2}" -f $language, $theme,
                [Guid]::NewGuid().ToString("N"))
            New-Item -ItemType Directory -Force -Path $stateRoot | Out-Null

            Invoke-Capture $language $theme "startup" @(
                "--qa-library", (Join-Path $stateRoot "startup.json"),
                "--qa-screenshot-main"
            )
            Invoke-Capture $language $theme "playback" @(
                "--qa-library", (Join-Path $stateRoot "playback.json"),
                "--qa-play", $playFixture,
                "--qa-screenshot-main"
            )
            Invoke-Capture $language $theme "mini" @(
                "--qa-library", (Join-Path $stateRoot "mini.json"),
                "--qa-play", $playFixture,
                "--qa-screenshot-mini"
            )
            Invoke-Capture $language $theme "settings" @(
                "--qa-library", (Join-Path $stateRoot "settings.json"),
                "--qa-open-settings",
                "--qa-screenshot-main"
            )
            Invoke-Capture $language $theme "list" @(
                "--qa-library", (Join-Path $stateRoot "list.json"),
                "--qa-import-folder", $formatFixtures,
                "--qa-screenshot-list"
            )
            foreach ($tool in 0..4) {
                Invoke-Capture $language $theme ("tool-{0}" -f $tool) @(
                    "--qa-library", (Join-Path $stateRoot ("tool-{0}.json" -f $tool)),
                    "--qa-tool", [string]$tool,
                    "--qa-screenshot-tools"
                )
            }
        }
    }

    $csvPath = Join-Path $outputPath "matrix.csv"
    $results | Export-Csv -NoTypeInformation -Encoding UTF8 -LiteralPath $csvPath
    [pscustomobject]@{
        Executable = $appPath
        Captures = $results.Count
        Languages = $Languages.Count
        Themes = $Themes.Count
        Output = $outputPath
        Result = "PASS"
    }
}
finally {
    $env:Path = $originalPath
}
