# tools/memory_probe.ps1
# Starts the AgPlayer app with --qa-play on the supplied audio file, waits for
# the working set to stabilise, and reports the stable working set in MB.
# Exits nonzero when the stable working set is >= 60 MB.
param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$AudioFile,
    [string]$QtBinDir
)

# Redirect LOCALAPPDATA to a writable temp directory so the sandbox does not
# block RuntimeLog (GenericDataLocation) or LibraryStore (AppDataLocation).
# QStandardPaths on Windows uses SHGetKnownFolderPath (not LOCALAPPDATA), so
# also set AGPLAYER_LOG_DIR which RuntimeLog honors explicitly.
$qaDataDir = Join-Path $env:TEMP 'AgPlayer-qa'
New-Item -ItemType Directory -Force -Path $qaDataDir | Out-Null
$env:LOCALAPPDATA = $qaDataDir
$env:AGPLAYER_LOG_DIR = $qaDataDir

# Ensure Qt6 DLLs are reachable. When AgPlayer.exe is built without
# windeployqt the Qt6 bin directory must be on PATH or the loader shows a
# DLL-not-found dialog and the working-set measurement becomes invalid.
if ($QtBinDir) {
    $env:PATH = "$QtBinDir;$env:PATH"
} elseif (Test-Path 'D:\Qt\6.7.0\msvc2019_64\bin\Qt6Core.dll') {
    $env:PATH = "D:\Qt\6.7.0\msvc2019_64\bin;$env:PATH"
}

$process = Start-Process -PassThru -FilePath $Executable -ArgumentList @('--qa-play', $AudioFile)
try {
    Start-Sleep -Seconds 15
    if ($process.HasExited) {
        Write-Error "AgPlayer exited early with code $($process.ExitCode); cannot measure memory."
        [pscustomobject]@{
            StableWorkingSetMB = 0
            LimitMB             = 60
            Passed              = $false
            Error               = "process exited before measurement"
        } | ConvertTo-Json
        exit 2
    }
    $process.Refresh()
    $mb = [math]::Round($process.WorkingSet64 / 1MB, 2)
    [pscustomobject]@{
        StableWorkingSetMB = $mb
        LimitMB             = 60
        Passed              = ($mb -lt 60)
    } | ConvertTo-Json
    if ($mb -ge 60) { exit 1 }
}
finally {
    if (!$process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        $process.WaitForExit(5000) | Out-Null
        if (!$process.HasExited) { $process.Kill() }
    }
}
