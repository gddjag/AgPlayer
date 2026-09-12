[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/msvc-release",
    [string]$OutputDirectory = "build/qa/final-ui-matrix",
    [ValidateSet("zh", "en")]
    [string[]]$Languages = @("zh", "en"),
    [ValidateSet("dark", "light", "system")]
    [string[]]$Themes = @("dark", "light", "system"),
    [ValidateSet("1", "1.25", "1.5", "2")]
    [string[]]$ScaleFactors = @("1"),
    [ValidateSet(
        "startup", "playback", "mini", "settings", "list",
        "tool-0", "tool-1", "tool-2", "tool-3", "tool-4", "tool-5"
    )]
    [string[]]$Surfaces = @(
        "startup", "playback", "mini", "settings", "list",
        "tool-0", "tool-1", "tool-2", "tool-3", "tool-4", "tool-5"
    )
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot "qa-runtime.ps1")
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

$runtimePaths = Get-AgPlayerRuntimePaths `
    -BuildRoot $buildRoot -CachePath $cachePath

$originalPath = $env:Path
$originalScaleFactor = $env:QT_SCALE_FACTOR
$results = [System.Collections.Generic.List[object]]::new()
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

function Get-SurfaceExpectation {
    param([string]$Surface)

    switch -Regex ($Surface) {
        "^startup$|^playback$" {
            return [pscustomobject]@{ Width = 863; Height = 266 }
        }
        "^mini$" {
            return [pscustomobject]@{ Width = 588; Height = 186 }
        }
        "^settings$" {
            return [pscustomobject]@{ Width = 860; Height = 900 }
        }
        "^list$" {
            # A fresh detached list uses its compact 590px geometry. Older
            # persisted QA state may still restore the previous 604px height,
            # while the desktop host may expand it to the 906px available height;
            # all three are real supported window states.
            return [pscustomobject]@{ Width = 863; Heights = @(590, 604, 888, 906) }
        }
        "^tool-\d+$" {
            return [pscustomobject]@{ Width = 1672; Height = 941 }
        }
        default {
            throw "No screenshot expectation configured for surface '$Surface'"
        }
    }
}

function Measure-Screenshot {
    param(
        [string]$Path,
        [string]$Surface,
        [double]$ScaleFactor = 1
    )

    $expected = Get-SurfaceExpectation $Surface
    if ($Surface -eq 'tool-5') {
        $expected.Width = [int][Math]::Round($expected.Width * $ScaleFactor)
        $expected.Height = [int][Math]::Round($expected.Height * $ScaleFactor)
    }
    $bitmap = [System.Drawing.Bitmap]::new($Path)
    try {
        $validHeights = if ($null -ne $expected.Heights) {
            @($expected.Heights)
        } else {
            @($expected.Height)
        }
        if ($bitmap.Width -ne $expected.Width -or
            $bitmap.Height -notin $validHeights) {
            throw ("{0} has size {1}x{2}; expected {3}x({4})" -f
                $Surface, $bitmap.Width, $bitmap.Height,
                $expected.Width, ($validHeights -join "|"))
        }

        $colors = [System.Collections.Generic.HashSet[int]]::new()
        $opaqueSamples = 0
        $sampleStride = 8
        for ($y = 0; $y -lt $bitmap.Height; $y += $sampleStride) {
            for ($x = 0; $x -lt $bitmap.Width; $x += $sampleStride) {
                $pixel = $bitmap.GetPixel($x, $y)
                [void]$colors.Add($pixel.ToArgb())
                if ($pixel.A -gt 0) {
                    $opaqueSamples++
                }
            }
        }

        if ($colors.Count -lt 32 -or $opaqueSamples -lt 100) {
            throw "$Surface appears blank or visually incomplete"
        }

        $cornerAlpha = @(
            $bitmap.GetPixel(0, 0).A
            $bitmap.GetPixel($bitmap.Width - 1, 0).A
            $bitmap.GetPixel(0, $bitmap.Height - 1).A
            $bitmap.GetPixel($bitmap.Width - 1, $bitmap.Height - 1).A
        )
        $outerCornerAlpha = if ($Surface -match "^(list|tool-\d+)$") {
            # Borderless list and audio-tool workspaces intentionally fill
            # their native rectangles; no corner-alpha contract applies.
            @()
        }
        elseif ($Surface -eq "playback") {
            # Playback is captured with the list window docked below it. The
            # shared bottom edge is intentionally square; the outer top edge
            # must still preserve the transparent window corners.
            $cornerAlpha[0..1]
        }
        else {
            $cornerAlpha
        }
        if ($outerCornerAlpha.Count -gt 0 -and
            ($outerCornerAlpha | Measure-Object -Maximum).Maximum -ne 0) {
            throw "$Surface does not preserve transparent outer corners"
        }

        return [pscustomobject]@{
            Width = $bitmap.Width
            Height = $bitmap.Height
            UniqueSampledColors = $colors.Count
            OpaqueSamples = $opaqueSamples
            CornerAlpha = ($cornerAlpha -join "/")
        }
    }
    finally {
        $bitmap.Dispose()
    }
}

function Measure-ThemeDifference {
    param(
        [string]$DarkPath,
        [string]$LightPath
    )

    $dark = [System.Drawing.Bitmap]::new($DarkPath)
    $light = [System.Drawing.Bitmap]::new($LightPath)
    try {
        if ($dark.Width -ne $light.Width -or $dark.Height -ne $light.Height) {
            throw "Theme screenshots have different dimensions"
        }

        [long]$difference = 0
        [long]$samples = 0
        for ($y = 0; $y -lt $dark.Height; $y += 16) {
            for ($x = 0; $x -lt $dark.Width; $x += 16) {
                $a = $dark.GetPixel($x, $y)
                $b = $light.GetPixel($x, $y)
                if ($a.A -eq 0 -and $b.A -eq 0) {
                    continue
                }
                $difference += [Math]::Abs([int]$a.R - [int]$b.R)
                $difference += [Math]::Abs([int]$a.G - [int]$b.G)
                $difference += [Math]::Abs([int]$a.B - [int]$b.B)
                $samples += 3
            }
        }
        if ($samples -eq 0) {
            return 0
        }
        return [Math]::Round($difference / $samples, 2)
    }
    finally {
        $dark.Dispose()
        $light.Dispose()
    }
}


function New-QALibraryPath {
    param(
        [string]$StateRoot,
        [string]$Surface
    )

    # TagModel persists tags.json beside the library file. Give every capture
    # its own directory so one seeded surface cannot contaminate another.
    $surfaceRoot = Join-Path $StateRoot $Surface
    New-Item -ItemType Directory -Force -Path $surfaceRoot | Out-Null
    return Join-Path $surfaceRoot "library.json"
}

function Invoke-Capture {
    param(
        [string]$Language,
        [string]$Theme,
        [string]$ScaleFactor,
        [string]$Surface,
        [string[]]$Arguments
    )

    $stem = Get-CaptureStem -Language $Language -Theme $Theme `
        -ScaleFactor $ScaleFactor -Surface $Surface
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
        -WindowStyle Hidden -Wait -PassThru
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
    $metrics = Measure-Screenshot -Path $screenshot -Surface $Surface -ScaleFactor ([double]$ScaleFactor)
    $logText = Get-Content -Raw -Encoding UTF8 -LiteralPath $log
    if ($logText -match "\[(WARN|ERROR|FATAL)\]" -or
        $logText -match "QQml|ReferenceError|TypeError") {
        throw "$stem recorded runtime problems:`n$logText"
    }
    $results.Add([pscustomobject]@{
        Language = $Language
        Theme = $Theme
        ScaleFactor = $ScaleFactor
        Surface = $Surface
        Bytes = (Get-Item -LiteralPath $screenshot).Length
        Width = $metrics.Width
        Height = $metrics.Height
        UniqueSampledColors = $metrics.UniqueSampledColors
        OpaqueSamples = $metrics.OpaqueSamples
        CornerAlpha = $metrics.CornerAlpha
        Result = "PASS"
    })
}

function Get-CaptureStem {
    param(
        [string]$Language,
        [string]$Theme,
        [string]$ScaleFactor = "1",
        [string]$Surface
    )

    $parts = [System.Collections.Generic.List[string]]::new()
    $parts.Add($Language)
    $parts.Add($Theme)
    $parts.Add($Surface)
    if ($ScaleFactor -ne "1") {
        $parts.Add("scale-" + $ScaleFactor.Replace(".", ""))
    }
    return $parts -join "-"
}

try {
    $env:Path = ($runtimePaths -join ";") + ";" + $originalPath

    foreach ($language in $Languages) {
        foreach ($theme in $Themes) {
          foreach ($scaleFactor in $ScaleFactors) {
            $env:QT_SCALE_FACTOR = $scaleFactor
            $stateRoot = Join-Path $outputPath (
                "state-{0}-{1}-{2}-{3}" -f $language, $theme, $scaleFactor,
                [Guid]::NewGuid().ToString("N"))
            New-Item -ItemType Directory -Force -Path $stateRoot | Out-Null

            if ($Surfaces -contains "startup") {
                Invoke-Capture $language $theme $scaleFactor "startup" @(
                    "--qa-library", (New-QALibraryPath $stateRoot "startup"),
                    "--qa-screenshot-main"
                )
            }
            if ($Surfaces -contains "playback") {
                Invoke-Capture $language $theme $scaleFactor "playback" @(
                    "--qa-library", (New-QALibraryPath $stateRoot "playback"),
                    "--qa-play", $playFixture,
                    "--qa-screenshot-main"
                )
            }
            if ($Surfaces -contains "mini") {
                Invoke-Capture $language $theme $scaleFactor "mini" @(
                    "--qa-library", (New-QALibraryPath $stateRoot "mini"),
                    "--qa-play", $playFixture,
                    "--qa-screenshot-mini"
                )
            }
            if ($Surfaces -contains "settings") {
                Invoke-Capture $language $theme $scaleFactor "settings" @(
                    "--qa-library", (New-QALibraryPath $stateRoot "settings"),
                    "--qa-open-settings",
                    "--qa-settings-section", "2",
                    "--qa-screenshot-main"
                )
            }
            if ($Surfaces -contains "list") {
                Invoke-Capture $language $theme $scaleFactor "list" @(
                    "--qa-library", (New-QALibraryPath $stateRoot "list"),
                    "--qa-import-folder", $formatFixtures,
                    "--qa-screenshot-list"
                )
            }
            foreach ($tool in @(0, 4, 1, 2, 3, 5)) {
                $toolSurface = "tool-{0}" -f $tool
                if ($Surfaces -notcontains $toolSurface) {
                    continue
                }
                Invoke-Capture $language $theme $scaleFactor $toolSurface @(
                    "--qa-library", (New-QALibraryPath $stateRoot $toolSurface),
                    "--qa-tool", [string]$tool,
                    "--qa-screenshot-tools"
                )
            }
          }
        }
    }

    if ($Themes -contains "dark" -and $Themes -contains "light") {
        foreach ($language in $Languages) {
          foreach ($scaleFactor in $ScaleFactors) {
            foreach ($surface in $Surfaces) {
                $darkPath = Join-Path $outputPath (
                    (Get-CaptureStem -Language $language -Theme "dark" `
                        -ScaleFactor $scaleFactor -Surface $surface) + ".png")
                $lightPath = Join-Path $outputPath (
                    (Get-CaptureStem -Language $language -Theme "light" `
                        -ScaleFactor $scaleFactor -Surface $surface) + ".png")
                $difference = Measure-ThemeDifference $darkPath $lightPath
                if ($difference -lt 12) {
                    throw (("{0}-{1} dark/light difference is only {2}; " +
                        "theme coverage may be incomplete") -f
                        $language, $surface, $difference)
                }
            }
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
        ScaleFactors = $ScaleFactors.Count
        Surfaces = $Surfaces.Count
        Output = $outputPath
        Result = "PASS"
    }
}
finally {
    $env:Path = $originalPath
    if ($null -eq $originalScaleFactor) {
        Remove-Item Env:QT_SCALE_FACTOR -ErrorAction SilentlyContinue
    } else {
        $env:QT_SCALE_FACTOR = $originalScaleFactor
    }
}
