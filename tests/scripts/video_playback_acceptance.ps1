[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$AppPath,
    [Parameter(Mandatory = $true)][string]$Mp4H264AacPath,
    [Parameter(Mandatory = $true)][string]$MkvPath,
    [Parameter(Mandatory = $true)][string]$WebmPath,
    [Parameter(Mandatory = $true)][string]$MovPath,
    [Parameter(Mandatory = $true)][string]$AviPath,
    [Parameter(Mandatory = $true)][string]$M4vPath,
    [Parameter(Mandatory = $true)][string]$VideoOnlyPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateRange(3000, 120000)][int]$ExitAfterMs = 20000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-RequiredFile([string]$Label, [string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required $Label sample is missing: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Quote-ProcessArgument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

$resolvedApp = Resolve-RequiredFile 'Release application' $AppPath
$samples = [ordered]@{
    'mp4-h264-aac' = Resolve-RequiredFile 'MP4/H.264/AAC' $Mp4H264AacPath
    'mkv'           = Resolve-RequiredFile 'MKV' $MkvPath
    'webm'          = Resolve-RequiredFile 'WebM' $WebmPath
    'mov'           = Resolve-RequiredFile 'MOV' $MovPath
    'avi'           = Resolve-RequiredFile 'AVI' $AviPath
    'm4v'           = Resolve-RequiredFile 'M4V' $M4vPath
    'video-only'    = Resolve-RequiredFile 'video-only' $VideoOnlyPath
}

$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$unicodeDirectory = Join-Path $outputRoot '输入 样本'
[System.IO.Directory]::CreateDirectory($unicodeDirectory) | Out-Null
$unicodeSample = Join-Path $unicodeDirectory '视频 样本.mp4'
Copy-Item -LiteralPath $samples['mp4-h264-aac'] -Destination $unicodeSample -Force
$samples['unicode-space-path'] = $unicodeSample

$results = [System.Collections.Generic.List[object]]::new()
$runs = [System.Collections.Generic.List[object]]::new()
foreach ($entry in $samples.GetEnumerator()) {
    $runs.Add([pscustomobject]@{ Name = $entry.Key; Path = $entry.Value; Fullscreen = $false })
}
$runs.Add([pscustomobject]@{
    Name = 'mp4-h264-aac-fullscreen'
    Path = $samples['mp4-h264-aac']
    Fullscreen = $true
})

foreach ($run in $runs) {
    $logPath = Join-Path $outputRoot ($run.Name + '.log')
    $screenshotPath = Join-Path $outputRoot ($run.Name + '.png')
    Remove-Item -LiteralPath $logPath, $screenshotPath -Force -ErrorAction SilentlyContinue

    $arguments = @(
        '--qa-test-mode',
        '--qa-play', (Quote-ProcessArgument $run.Path),
        '--qa-log', (Quote-ProcessArgument $logPath),
        '--qa-screenshot-main', (Quote-ProcessArgument $screenshotPath),
        '--qa-exit-after-ms', $ExitAfterMs
    )
    if ($run.Fullscreen) {
        $arguments += '--qa-video-fullscreen'
    }

    $process = Start-Process -FilePath $resolvedApp `
        -ArgumentList ($arguments -join ' ') -PassThru
    $finished = $process.WaitForExit($ExitAfterMs + 15000)
    if (-not $finished) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Acceptance run timed out: $($run.Name)"
    }
    if ($process.ExitCode -ne 0) {
        throw "Acceptance run failed ($($process.ExitCode)): $($run.Name); log=$logPath"
    }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        throw "Acceptance run produced no log: $($run.Name)"
    }
    if (-not (Test-Path -LiteralPath $screenshotPath -PathType Leaf) -or
        (Get-Item -LiteralPath $screenshotPath).Length -lt 1024) {
        throw "Acceptance run produced no usable screenshot: $($run.Name)"
    }

    $log = Get-Content -LiteralPath $logPath -Raw
    $diagnostic = [regex]::Match(
        $log,
        'QA video diagnostics:\s+visible=\s*(true|false)\s+workerRunning=\s*(true|false)\s+queuedFrameCount=\s*(\d+)\s+queuedFrameBytes=\s*(\d+)\s+frameSerial=\s*(\d+)\s+fullscreen=\s*(true|false)',
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $diagnostic.Success) {
        throw "Acceptance log lacks video diagnostics: $($run.Name)"
    }

    $visible = [bool]::Parse($diagnostic.Groups[1].Value)
    $workerRunning = [bool]::Parse($diagnostic.Groups[2].Value)
    $queuedFrames = [int]$diagnostic.Groups[3].Value
    $queuedBytes = [int64]$diagnostic.Groups[4].Value
    $frameSerial = [uint64]$diagnostic.Groups[5].Value
    $fullscreen = [bool]::Parse($diagnostic.Groups[6].Value)
    if (-not $visible -or -not $workerRunning -or $frameSerial -eq 0) {
        throw "Video did not reach a visible moving-frame state: $($run.Name)"
    }
    if ($queuedFrames -gt 3 -or $queuedBytes -gt 67108864) {
        throw "Video queue exceeded its bound: $($run.Name) frames=$queuedFrames bytes=$queuedBytes"
    }
    if ($run.Fullscreen -and -not $fullscreen) {
        throw "Fullscreen acceptance run did not enter fullscreen"
    }

    $results.Add([pscustomobject]@{
        name = $run.Name
        path = $run.Path
        fullscreen = $fullscreen
        workerRunning = $workerRunning
        queuedFrameCount = $queuedFrames
        queuedFrameBytes = $queuedBytes
        frameSerial = $frameSerial
        screenshot = $screenshotPath
        log = $logPath
        exitCode = $process.ExitCode
    })
}

$summaryPath = Join-Path $outputRoot 'summary.json'
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host "Video playback acceptance passed: $($results.Count) runs"
Write-Host "Summary: $summaryPath"
