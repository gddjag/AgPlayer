[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/release",
    [string]$AudioPath = "build/qa/frequency-color-fixture/frequency-color-synthetic.wav",
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot '../qa-runtime.ps1')
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildRoot = (Resolve-Path (Join-Path $repoRoot $BuildDirectory)).Path
$output = [IO.Path]::GetFullPath($OutputDirectory).Replace('\', '/')
if (Test-Path -LiteralPath $output) { throw "Use a new output directory: $output" }
New-Item -ItemType Directory -Path "$output/cache" -Force | Out-Null
$audio = (Resolve-Path -LiteralPath $AudioPath).Path.Replace('\', '/')
@{ audioPath = $audio; cachePath = "$output/cache"; imagePath = "$output/contour.png" } |
    ConvertTo-Json | Set-Content -LiteralPath "$output/config.json" -Encoding UTF8
$executable = Join-Path $buildRoot 'tests/qml_waveform_test.exe'
$variables = @('Path', 'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'QSG_RHI_BACKEND', 'QT_SCALE_FACTOR', 'QML_XHR_ALLOW_FILE_READ')
$original = @{}
foreach ($name in $variables) { $original[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $runtime = Get-AgPlayerRuntimePaths -BuildRoot $buildRoot -CachePath (Join-Path $buildRoot 'CMakeCache.txt')
    $env:Path = ($runtime -join ';') + ';' + $original['Path']
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QUICK_BACKEND = 'rhi'
    $env:QSG_RHI_BACKEND = 'd3d11'
    $env:QT_SCALE_FACTOR = '1'
    $env:QML_XHR_ALLOW_FILE_READ = '1'
    $arguments = @('-input', (Join-Path $PSScriptRoot 'tst_waveform_contour.qml'), '-o', "$output/test.log,txt") |
        ForEach-Object { '"' + $_ + '"' }
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(150000)) { $process.Kill(); throw 'Contour capture timed out' }
    $log = Get-Content -LiteralPath "$output/test.log" -Raw
    $manifest = [ordered]@{
        Executable = $executable
        ExecutableSha256 = (Get-FileHash -LiteralPath $executable).Hash
        ExecutableLastWriteTime = (Get-Item -LiteralPath $executable).LastWriteTime.ToString('o')
        Audio = $audio
        AudioSha256 = (Get-FileHash -LiteralPath $audio).Hash
        FixtureDescription = 'Existing deterministic synthetic 24 s frequency fixture; not a music recording'
        Result = $process.ExitCode
        PixelAssertion = 'Exact non-background occupancy and upper/lower raster edge at every column for modes 0/1/3'
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath "$output/manifest.json" -Encoding UTF8
    $log -split "`r?`n" | Select-String 'WAVEFORM_CONTOUR_METRICS|Totals:|FAIL|QWARN|QFATAL'
    if ($process.ExitCode -ne 0 -or $log -match 'FAIL!|QWARN|QFATAL') {
        throw "Contour parity failed; inspect $output/test.log and PNGs"
    }
    Write-Output "Artifacts: $output"
}
finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $original[$name], 'Process') }
}
