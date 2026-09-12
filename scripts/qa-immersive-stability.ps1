[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/release',
    [ValidateRange(75, 86400)][int]$DurationSeconds = 1800,
    [string]$AudioPath,
    [ValidateNotNullOrEmpty()][string]$ThemeId = 'ink-wash',
    [string]$OutputDirectory,
    [string]$ValidateLog
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'qa-runtime.ps1')
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Assert-StabilityLog([string]$Path, [int]$Seconds) {
    $records = @()
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match 'QA immersive stability: phase=(sample|pre-exit) elapsedMs=(\d+) present=(\d+) active=(\d+) exposed=(\d+) frames=(\d+) itemLive=(\d+) resources=(\d+) items=(\d+)') {
            $records += [pscustomobject]@{
                Phase = $Matches[1]; ElapsedMs = [long]$Matches[2]
                Present = [int]$Matches[3]; Active = [int]$Matches[4]; Exposed = [int]$Matches[5]
                Frames = [long]$Matches[6]; ItemLive = [int]$Matches[7]; Resources = [long]$Matches[8]
                Items = [int]$Matches[9]
            }
        }
    }
    if ($records.Count -lt 3 -or $records[-1].Phase -ne 'pre-exit') {
        throw 'Missing periodic renderer samples or the normal pre-exit sample.'
    }
    if ($records[-1].ElapsedMs -lt ($Seconds * 1000 - 2000)) {
        throw 'Application exited before the requested observation duration.'
    }
    $previous = $null
    foreach ($record in $records) {
        if ($record.Present -ne 1 -or $record.Active -ne 1 -or $record.Exposed -ne 1 -or
            $record.Items -ne 1 -or $record.ItemLive -ne 1 -or $record.Resources -lt 1 -or $record.Frames -lt 3) {
            throw "Renderer was absent, gated or unhealthy at $($record.ElapsedMs) ms."
        }
        if ($null -ne $previous) {
            if ($record.Frames -le $previous.Frames) { throw 'Renderer stopped presenting new frames.' }
            if ($record.Resources -ne $previous.Resources) { throw 'Resource generation changed during the fixed-scene run.' }
            if ($record.ElapsedMs - $previous.ElapsedMs -gt 75000) { throw 'Telemetry stalled for more than 75 seconds.' }
        }
        $previous = $record
    }
    return $records
}

if ($ValidateLog) {
    Assert-StabilityLog $ValidateLog $DurationSeconds
    return
}
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
}
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$appPath = Join-Path $buildRoot 'app/AgPlayer.exe'
if (!(Test-Path -LiteralPath $appPath)) { throw "Missing application: $appPath" }
if ($AudioPath) { $AudioPath = (Resolve-Path -LiteralPath $AudioPath).Path }
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot ('build/qa/stability-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$logPath = Join-Path $outputRoot 'runtime.log'
if (Test-Path -LiteralPath $logPath) { throw 'Use a fresh output directory to avoid mixing previous evidence.' }
$qtPrefix = Get-AgPlayerQtPrefix -CachePath $cachePath
$vcpkgLine = Select-String -LiteralPath $cachePath -Pattern '^VCPKG_INSTALLED_DIR:[^=]*=(.+)$' | Select-Object -First 1
$runtimePaths = @((Join-Path $qtPrefix 'bin'))
if ($vcpkgLine) {
    $dependencyRoot = $vcpkgLine.Matches[0].Groups[1].Value
    $debugBuild = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_BUILD_TYPE:[^=]*=Debug$'
    if ($debugBuild) { $runtimePaths += Join-Path $dependencyRoot 'x64-windows/debug/bin' }
    $runtimePaths += Join-Path $dependencyRoot 'x64-windows/bin'
}
$arguments = @('--qa-test-mode', '--qa-instance-key', ('stability-' + [guid]::NewGuid().ToString('N')),
    '--qa-log', ('"' + $logPath + '"'), '--qa-immersive',
    '--qa-width', '1920', '--qa-height', '1080',
    '--qa-immersive-stability', '--qa-exit-after-ms', ($DurationSeconds * 1000).ToString())
if ($ThemeId -notmatch '^[a-z0-9]+(?:-[a-z0-9]+)*$') { throw 'ThemeId must be a built-in theme id.' }
$arguments += @('--qa-immersive-theme', $ThemeId)
if ($AudioPath) { $arguments += @('--qa-play', ('"' + $AudioPath + '"')) }
else { $arguments += '--qa-immersive-synthetic' }
[pscustomobject]@{ ThemeId = $ThemeId
    AudioMode = $(if ($AudioPath) { 'file' } else { 'synthetic' })
    AudioPath = $AudioPath; DurationSeconds = $DurationSeconds
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'run.json') -Encoding utf8
$savedPath = $env:PATH
$savedPlatform = $env:QT_QPA_PLATFORM
$savedBackend = $env:QT_QUICK_BACKEND
$savedRhi = $env:QSG_RHI_BACKEND
$process = $null
try {
    $env:PATH = ($runtimePaths -join ';') + ';' + $savedPath
    $env:QT_QPA_PLATFORM = 'windows'; $env:QT_QUICK_BACKEND = ''; $env:QSG_RHI_BACKEND = 'd3d11'
    $process = Start-Process -FilePath $appPath -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $started = Get-Date
    $processRecords = @()
    while (!$process.WaitForExit(1000)) {
        $process.Refresh()
        if ($processRecords.Count -eq 0 -or ((Get-Date) - $lastSample).TotalSeconds -ge 60) {
            $lastSample = Get-Date
            $processRecords += [pscustomobject]@{ ElapsedSeconds = ($lastSample - $started).TotalSeconds
                PrivateBytes = $process.PrivateMemorySize64; Handles = $process.HandleCount
                CpuSeconds = $process.TotalProcessorTime.TotalSeconds }
        }
        if (((Get-Date) - $started).TotalSeconds -gt $DurationSeconds + 90) {
            throw "Normal timed exit failed; the owned process $($process.Id) will be cleaned up. Logs remain at $outputRoot."
        }
    }
    $processRecords | Export-Csv -LiteralPath (Join-Path $outputRoot 'process.csv') -NoTypeInformation
    if ($process.ExitCode -ne 0) { throw "Application exited with code $($process.ExitCode)." }
    $samples = @(Assert-StabilityLog $logPath $DurationSeconds)
    $samples | Export-Csv -LiteralPath (Join-Path $outputRoot 'renderer.csv') -NoTypeInformation
    # Per-item telemetry is observed before Qt teardown. Normal process exit
    # does not separately certify GPU resource release or global renderer count.
    [pscustomobject]@{ ExitCode = $process.ExitCode; Samples = $samples.Count
        DurationSeconds = $samples[-1].ElapsedMs / 1000; Evidence = $outputRoot
        ThemeId = $ThemeId
        Scope = 'Running-item counters and process exit only; GPU teardown not measured'
        AudioMode = $(if ($AudioPath) { 'file - verify duration and playback separately' } else { 'synthetic' }) }
}
finally {
    $env:PATH = $savedPath; $env:QT_QPA_PLATFORM = $savedPlatform
    $env:QT_QUICK_BACKEND = $savedBackend; $env:QSG_RHI_BACKEND = $savedRhi
    if ($null -ne $process) {
        try {
            # Use only the Process returned by this invocation's Start-Process;
            # never enumerate AgPlayer instances or terminate a process tree.
            if (!$process.HasExited) {
                $process.Kill()
                if (!$process.WaitForExit(5000)) {
                    Write-Warning "Owned QA process $($process.Id) did not exit after cleanup."
                }
            }
        }
        catch {
            Write-Warning "Could not clean up owned QA process: $($_.Exception.Message)"
        }
        finally { $process.Dispose() }
    }
}
