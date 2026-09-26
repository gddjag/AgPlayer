[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/release",
    [Parameter(Mandatory = $true)][string]$AudioPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$FixtureDescription = "User-supplied audio; see source path and hash",
    [ValidateSet("1", "1.25", "1.5", "2")][string]$ScaleFactor = "1"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "../qa-runtime.ps1")
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$audio = (Resolve-Path -LiteralPath $AudioPath).Path.Replace('\', '/')
$output = [System.IO.Path]::GetFullPath($OutputDirectory).Replace('\', '/')
if (Test-Path -LiteralPath $output) {
    throw "Use a new output directory to guarantee an empty, isolated cold cache: $output"
}
New-Item -ItemType Directory -Path $output | Out-Null
$cacheDirectory = "$output/cache"
New-Item -ItemType Directory -Path $cacheDirectory | Out-Null
$executable = Join-Path $buildRoot "tests/qml_waveform_test.exe"
$runtimePaths = Get-AgPlayerRuntimePaths -BuildRoot $buildRoot -CachePath (Join-Path $buildRoot "CMakeCache.txt")
$originalPath = $env:Path
$originalPlatform = $env:QT_QPA_PLATFORM
$originalScale = $env:QT_SCALE_FACTOR
$originalRead = $env:QML_XHR_ALLOW_FILE_READ
$originalBackend = $env:QT_QUICK_BACKEND
$originalRhi = $env:QSG_RHI_BACKEND
try {
    $env:Path = ($runtimePaths -join ';') + ';' + $originalPath
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QUICK_BACKEND = 'rhi'
    $env:QSG_RHI_BACKEND = 'd3d11'
    $env:QT_SCALE_FACTOR = $ScaleFactor
    $env:QML_XHR_ALLOW_FILE_READ = '1'
    foreach ($phase in @('cold', 'warm')) {
        $phaseDirectory = "$output/$phase"
        New-Item -ItemType Directory -Path $phaseDirectory | Out-Null
        @{ audioPath = $audio; cachePath = $cacheDirectory; imagePath = "$phaseDirectory/waveform.png" } |
            ConvertTo-Json | Set-Content -LiteralPath "$phaseDirectory/config.json" -Encoding UTF8
        $arguments = @('-input', (Join-Path $PSScriptRoot 'tst_waveform_quality.qml'), '-o', "$phaseDirectory/test.log,txt")
        $quotedArguments = $arguments | ForEach-Object { '"' + $_ + '"' }
        # Avoid a helper console; the Qt test host manages its own render surface.
        $process = Start-Process -FilePath $executable -ArgumentList $quotedArguments -PassThru -WindowStyle Hidden
        if (-not $process.WaitForExit(150000)) {
            $process.Kill()
            throw "$phase capture timed out; see $phaseDirectory/test.log"
        }
        if ($process.ExitCode -ne 0) {
            throw "$phase capture failed ($($process.ExitCode)); see $phaseDirectory/test.log"
        }
        if (-not (Test-Path -LiteralPath "$phaseDirectory/waveform.png")) {
            throw "$phase screenshot was not produced"
        }
        $log = Get-Content -LiteralPath "$phaseDirectory/test.log" -Raw
        if ($log -match 'QWARN|QFATAL|FAIL!') {
            throw "$phase emitted a runtime warning or failure; inspect $phaseDirectory/test.log"
        }
        Add-Type -AssemblyName System.Drawing
        $bitmap = [System.Drawing.Bitmap]::new("$phaseDirectory/waveform.png")
        try {
            $coloredPixels = 0
            for ($x = 20; $x -lt [Math]::Min(1180, $bitmap.Width); $x += 4) {
                $pixel = $bitmap.GetPixel($x, 98)
                if ([Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B)) -gt 50) {
                    $coloredPixels++
                }
            }
            if ($coloredPixels -lt 10) {
                throw "$phase screenshot has no usable waveform pixels; a saved PNG alone is not render acceptance"
            }
        }
        finally { $bitmap.Dispose() }
        Get-Content -LiteralPath "$phaseDirectory/test.log" | Select-String 'WAVEFORM_QA_METRICS|Totals:'
    }
    $manifest = [ordered]@{
        RecordedAt = (Get-Date).ToString('o')
        Executable = $executable
        ExecutableLastWriteTime = (Get-Item -LiteralPath $executable).LastWriteTime.ToString('o')
        ExecutableSha256 = (Get-FileHash -LiteralPath $executable).Hash
        SourceHead = (& git -C $repoRoot rev-parse HEAD)
        SourceStatus = (& git -C $repoRoot status --short)
        AudioPath = $audio
        AudioSha256 = (Get-FileHash -LiteralPath $audio).Hash
        FixtureDescription = $FixtureDescription
        ScaleFactor = $ScaleFactor
        CachePath = $cacheDirectory
        Note = 'Binary hash/time records provenance; existing binary is not proof of current source. Cold/warm measure provider-ready wall time, not CPU time or actual listening. maxGuiTimerGapMs is a timer responsiveness sample; guiTimerSamples=0 means unavailable, not zero latency.'
    }
    $manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$output/manifest.json" -Encoding UTF8
    Write-Output "Artifacts: $output"
}
finally {
    $env:Path = $originalPath
    $env:QT_QPA_PLATFORM = $originalPlatform
    $env:QT_SCALE_FACTOR = $originalScale
    $env:QML_XHR_ALLOW_FILE_READ = $originalRead
    $env:QT_QUICK_BACKEND = $originalBackend
    $env:QSG_RHI_BACKEND = $originalRhi
}
